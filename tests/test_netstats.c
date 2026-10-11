/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 데이터 사용량 서비스 단위 테스트
 * 수행범위: 인터페이스 분류, 증분 누적, 데몬 재시작·재부팅·인터페이스 재생성
 *           시 이중 집계 방지, 청구 주기 리셋, 경고/한도와 모바일 데이터 차단·
 *           재개, 한도 상태 영속, D-Bus 계약
 * 의존방향: runtime/services/netstats/netstats.c (ZYL_TEST), gio
 * SOLID: SRP — 사용량 회계 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#define ROOT             "/tmp/zyl-netstats-test"
#define NETSTATS_DIR     ROOT "/data"
#define NETSTATS_SYSFS   ROOT "/sys"
#define NETSTATS_BOOT_ID ROOT "/boot_id"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pull the daemon in with its main() compiled out */
#include "../runtime/services/netstats/netstats.c"

static int tests_run;
static int tests_pass;

static void reset_tree(void) {
    g_assert_true(system("rm -rf " ROOT " && mkdir -p " ROOT "/sys " ROOT "/data") == 0);
    g_file_set_contents(NETSTATS_BOOT_ID, "boot-1\n", -1, NULL);
    g_test_mobile_calls = 0;
    g_test_mobile_enabled = TRUE;
}

#define RUN_TEST(fn) do { \
    tests_run++; \
    printf("  %-56s ", #fn); \
    reset_tree(); \
    fn(); \
    tests_pass++; \
    printf("PASS\n"); \
} while (0)

static void mkpath(const char *rel) {
    char *p = g_build_filename(NETSTATS_SYSFS, rel, NULL);
    g_mkdir_with_parents(p, 0755);
    g_free(p);
}

static void put(const char *rel, const char *text) {
    char *p = g_build_filename(NETSTATS_SYSFS, rel, NULL);
    char *dir = g_path_get_dirname(p);
    g_mkdir_with_parents(dir, 0755);
    g_assert_true(g_file_set_contents(p, text, -1, NULL));
    g_free(dir);
    g_free(p);
}

/* marker: "wireless", "device", "" (virtual) */
static void add_iface(const char *name, const char *marker) {
    char rel[256];
    snprintf(rel, sizeof(rel), "%s/statistics", name);
    mkpath(rel);
    if (marker[0]) {
        snprintf(rel, sizeof(rel), "%s/%s", name, marker);
        mkpath(rel);
    }
}

static void set_counters(const char *name, guint64 rx, guint64 tx) {
    char rel[256], val[32];
    snprintf(rel, sizeof(rel), "%s/statistics/rx_bytes", name);
    snprintf(val, sizeof(val), "%" G_GUINT64_FORMAT "\n", rx);
    put(rel, val);
    snprintf(rel, sizeof(rel), "%s/statistics/tx_bytes", name);
    snprintf(val, sizeof(val), "%" G_GUINT64_FORMAT "\n", tx);
    put(rel, val);
}

static time_t at(int year, int mon, int day, int hour) {
    struct tm tm = { 0 };
    tm.tm_year = year - 1900; tm.tm_mon = mon - 1; tm.tm_mday = day;
    tm.tm_hour = hour; tm.tm_isdst = -1;
    return mktime(&tm);
}

static NetStats fresh(time_t now) {
    NetStats ns = { 0 };
    netstats_load(&ns, now);
    return ns;
}

static void test_only_physical_links_count(void) {
    add_iface("lo", "device");
    add_iface("wwan0", "");                 /* mobile by name, no device link */
    add_iface("wlan0", "wireless");
    add_iface("eth0", "device");
    add_iface("tun0", "");                  /* VPN: already counted on the link below */
    add_iface("usb0", "device");
    put("usb0/uevent", "DEVTYPE=wwan\nINTERFACE=usb0\n");
    assert(classify("lo") == -1);
    assert(classify("wwan0") == NET_MOBILE);
    assert(classify("usb0") == NET_MOBILE);
    assert(classify("wlan0") == NET_WIFI);
    assert(classify("eth0") == NET_ETHERNET);
    assert(classify("tun0") == -1);
}

static void test_counts_what_moved_since_last_read(void) {
    time_t now = at(2026, 10, 11, 12);
    add_iface("wwan0", "");
    add_iface("wlan0", "wireless");
    set_counters("wwan0", 1000, 200);
    set_counters("wlan0", 5000, 700);
    NetStats ns = fresh(now);
    netstats_poll(&ns, now);
    assert(ns.rx[NET_MOBILE] == 1000 && ns.tx[NET_MOBILE] == 200);
    set_counters("wwan0", 1500, 260);
    netstats_poll(&ns, now);
    netstats_poll(&ns, now);                /* nothing moved: nothing added */
    assert(ns.rx[NET_MOBILE] == 1500 && ns.tx[NET_MOBILE] == 260);
    assert(ns.rx[NET_WIFI] == 5000 && ns.tx[NET_WIFI] == 700);
}

static void test_daemon_restart_does_not_count_twice(void) {
    time_t now = at(2026, 10, 11, 12);
    add_iface("wwan0", "");
    set_counters("wwan0", 1000, 0);
    NetStats ns = fresh(now);
    netstats_poll(&ns, now);
    netstats_save(&ns);
    NetStats again = fresh(now);            /* same boot */
    netstats_poll(&again, now);
    assert(again.rx[NET_MOBILE] == 1000);
}

static void test_reboot_starts_counters_from_zero(void) {
    time_t now = at(2026, 10, 11, 12);
    add_iface("wwan0", "");
    set_counters("wwan0", 1000, 0);
    NetStats ns = fresh(now);
    netstats_poll(&ns, now);
    netstats_save(&ns);
    /* After a reboot the kernel counter restarted and has already climbed past
       the old value; subtracting the old baseline would lose 1000 bytes */
    g_file_set_contents(NETSTATS_BOOT_ID, "boot-2\n", -1, NULL);
    set_counters("wwan0", 1200, 0);
    NetStats after = fresh(now);
    netstats_poll(&after, now);
    assert(after.rx[NET_MOBILE] == 2200);
}

static void test_recreated_interface_restarts_at_zero(void) {
    time_t now = at(2026, 10, 11, 12);
    add_iface("wwan0", "");
    set_counters("wwan0", 1000, 0);
    NetStats ns = fresh(now);
    netstats_poll(&ns, now);
    set_counters("wwan0", 300, 0);          /* modem reset */
    netstats_poll(&ns, now);
    assert(ns.rx[NET_MOBILE] == 1300);
}

static void test_cycle_starts_on_cycle_day(void) {
    assert(cycle_start_for(15, at(2026, 10, 11, 12)) == at(2026, 9, 15, 0));
    assert(cycle_start_for(15, at(2026, 10, 20, 12)) == at(2026, 10, 15, 0));
    assert(cycle_start_for(15, at(2026, 10, 15, 0)) == at(2026, 10, 15, 0));
    assert(cycle_start_for(20, at(2026, 1, 5, 12)) == at(2025, 12, 20, 0));
}

static void test_new_cycle_resets_and_lifts_the_limit(void) {
    time_t now = at(2026, 10, 11, 12);
    add_iface("wwan0", "");
    set_counters("wwan0", 3000, 0);
    NetStats ns = fresh(now);
    assert(netstats_set_policy(&ns, 15, 1000, 2000, now));
    assert(ns.warned && ns.limited && !g_test_mobile_enabled);
    time_t next = at(2026, 10, 15, 1);
    set_counters("wwan0", 3100, 0);
    netstats_poll(&ns, next);
    assert(ns.cycle_start == at(2026, 10, 15, 0));
    assert(ns.rx[NET_MOBILE] == 100);       /* only what moved after the reset */
    assert(!ns.warned && !ns.limited && g_test_mobile_enabled);
}

static void test_limit_stops_mobile_data_only_once(void) {
    time_t now = at(2026, 10, 11, 12);
    add_iface("wwan0", "");
    add_iface("wlan0", "wireless");
    set_counters("wlan0", 999999, 0);       /* Wi-Fi never counts toward the limit */
    set_counters("wwan0", 900, 0);
    NetStats ns = fresh(now);
    netstats_set_policy(&ns, 1, 500, 1000, now);
    assert(ns.warned && !ns.limited && g_test_mobile_calls == 0);
    set_counters("wwan0", 1000, 50);
    netstats_poll(&ns, now);
    assert(ns.limited && !g_test_mobile_enabled && g_test_mobile_calls == 1);
    netstats_poll(&ns, now);                /* no traffic: no repeated nmcli */
    assert(g_test_mobile_calls == 1);
    set_counters("wwan0", 1100, 50);        /* modem reconnected on its own */
    netstats_poll(&ns, now);
    assert(g_test_mobile_calls == 2 && !g_test_mobile_enabled);
}

static void test_raising_the_limit_resumes_mobile_data(void) {
    time_t now = at(2026, 10, 11, 12);
    add_iface("wwan0", "");
    set_counters("wwan0", 1500, 0);
    NetStats ns = fresh(now);
    netstats_set_policy(&ns, 1, 0, 1000, now);
    assert(ns.limited && !g_test_mobile_enabled);
    netstats_set_policy(&ns, 1, 0, 5000, now);
    assert(!ns.limited && g_test_mobile_enabled);
    netstats_set_policy(&ns, 1, 0, 1000, now);
    netstats_set_policy(&ns, 1, 0, 0, now); /* limit off */
    assert(!ns.limited && g_test_mobile_enabled);
}

static void test_limit_survives_restart(void) {
    time_t now = at(2026, 10, 11, 12);
    add_iface("wwan0", "");
    set_counters("wwan0", 1500, 0);
    NetStats ns = fresh(now);
    netstats_set_policy(&ns, 1, 0, 1000, now);
    netstats_save(&ns);
    g_file_set_contents(NETSTATS_BOOT_ID, "boot-2\n", -1, NULL);
    NetStats after = fresh(now);
    assert(after.limited && after.limit_bytes == 1000 && after.cycle_day == 1);
    assert(after.rx[NET_MOBILE] == 1500);
}

static void test_cycle_day_must_exist_in_every_month(void) {
    time_t now = at(2026, 10, 11, 12);
    NetStats ns = fresh(now);
    assert(!netstats_set_policy(&ns, 0, 0, 0, now));
    assert(!netstats_set_policy(&ns, 29, 0, 0, now));
    assert(netstats_set_policy(&ns, 28, 0, 0, now));
}

static void test_dbus_contract(void) {
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(netstats_introspection_xml, NULL);
    assert(info);
    GDBusMethodInfo *m = g_dbus_interface_info_lookup_method(info->interfaces[0], "SetPolicy");
    /* The bridge fills these from JS cycleDay / warningBytes / limitBytes */
    assert(m && m->in_args[0] && strcmp(m->in_args[0]->name, "cycle_day") == 0);
    assert(strcmp(m->in_args[1]->name, "warning_bytes") == 0);
    assert(strcmp(m->in_args[2]->name, "limit_bytes") == 0 && !m->in_args[3]);
    g_dbus_node_info_unref(info);

    NetStats ns = fresh(at(2026, 10, 11, 12));
    GVariant *u = usage_variant(&ns);
    g_variant_ref_sink(u);
    assert(g_variant_is_of_type(u, G_VARIANT_TYPE("(a{sv})")));
    GVariant *dict = g_variant_get_child_value(u, 0);
    gboolean supported = FALSE;
    assert(g_variant_lookup(dict, "supported", "b", &supported) && supported);
    g_variant_unref(dict);
    g_variant_unref(u);
}

int main(void) {
    printf("=== Zyl OS NetStats Tests ===\n");
    RUN_TEST(test_only_physical_links_count);
    RUN_TEST(test_counts_what_moved_since_last_read);
    RUN_TEST(test_daemon_restart_does_not_count_twice);
    RUN_TEST(test_reboot_starts_counters_from_zero);
    RUN_TEST(test_recreated_interface_restarts_at_zero);
    RUN_TEST(test_cycle_starts_on_cycle_day);
    RUN_TEST(test_new_cycle_resets_and_lifts_the_limit);
    RUN_TEST(test_limit_stops_mobile_data_only_once);
    RUN_TEST(test_raising_the_limit_resumes_mobile_data);
    RUN_TEST(test_limit_survives_restart);
    RUN_TEST(test_cycle_day_must_exist_in_every_month);
    RUN_TEST(test_dbus_contract);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
