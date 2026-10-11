/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 텔레메트리 동의(옵트인) 단위 테스트
 * 수행범위: 동의 전 무수집·무식별자, 동의 영속화, 철회 시 수집분·식별자 삭제,
 *           같은 초 이벤트 덮어쓰기 방지
 * 의존방향: runtime/services/telemetry/telemetry.c (ZYL_TEST), gio
 * SOLID: SRP — 동의 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#define TELEMETRY_DIR "/tmp/zyl-telemetry-consent-test"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

/* Pull the daemon in with its main() compiled out */
#include "../runtime/services/telemetry/telemetry.c"

static int tests_run;
static int tests_pass;

#define RUN_TEST(fn) do { \
    tests_run++; \
    printf("  %-52s ", #fn); \
    reset_dir(); \
    fn(); \
    tests_pass++; \
    printf("PASS\n"); \
} while (0)

static void reset_dir(void) {
    DIR *d = opendir(TELEMETRY_DIR);
    if (d) {
        struct dirent *e;
        char p[512];
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            snprintf(p, sizeof(p), "%s/%s", TELEMETRY_DIR, e->d_name);
            unlink(p);
        }
        closedir(d);
    }
    mkdir(TELEMETRY_DIR, 0700);
    g_device_uuid[0] = '\0';
}

static int exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int count_pending(void) {
    int n = 0;
    DIR *d = opendir(TELEMETRY_DIR);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (strncmp(e->d_name, "pending_", 8) == 0) n++;
    closedir(d);
    return n;
}

/* GDPR: processing that is not essential needs opt-in. Never asked = no. */
static void test_never_asked_means_no_collection(void) {
    assert(consent_granted() == 0);
    queue_event("boot", "{\"boot_count\":1}");
    assert(count_pending() == 0);
    assert(!exists(DEVICE_UUID_FILE));
}

/* The device ID is personal data (an online identifier): it must not exist
   before consent, not even as a side effect of asking for it. */
static void test_no_identifier_before_consent(void) {
    queue_event("crash", "");
    assert(!exists(DEVICE_UUID_FILE));
    assert(g_device_uuid[0] == '\0');
}

static void test_consent_enables_collection_and_identifier(void) {
    assert(write_consent(1) == 0);
    assert(consent_granted() == 1);
    queue_event("boot", "{\"boot_count\":1}");
    assert(count_pending() == 1);
    assert(exists(DEVICE_UUID_FILE));
}

/* The old flag lived in memory and defaulted to on, so a refusal was undone
   by every restart. The file is the state now. */
static void test_consent_survives_restart(void) {
    write_consent(0);
    g_device_uuid[0] = '\0';           /* a restart loses all memory state */
    assert(consent_granted() == 0);
    write_consent(1);
    assert(consent_granted() == 1);
}

static void test_withdrawal_erases_collected_data_and_identifier(void) {
    write_consent(1);
    queue_event("boot", "");
    queue_event("ota", "");
    increment_boot_count();
    assert(count_pending() == 2);
    assert(exists(DEVICE_UUID_FILE));
    assert(exists(BOOT_COUNT_FILE));

    write_consent(0);
    purge_collected_data();

    assert(count_pending() == 0);
    assert(!exists(DEVICE_UUID_FILE));
    assert(!exists(BOOT_COUNT_FILE));
    assert(g_device_uuid[0] == '\0');
}

static void test_nothing_collected_after_withdrawal(void) {
    write_consent(1);
    queue_event("boot", "");
    write_consent(0);
    purge_collected_data();
    queue_event("crash", "");
    assert(count_pending() == 0);
    assert(!exists(DEVICE_UUID_FILE));
}

/* Two events in one second used to share pending_<time>.json */
static void test_same_second_events_do_not_overwrite(void) {
    write_consent(1);
    for (int i = 0; i < 5; i++) queue_event("burst", "");
    assert(count_pending() == 5);
}

/* A garbled or empty consent file is not consent */
static void test_unreadable_consent_is_refusal(void) {
    FILE *f = fopen(CONSENT_FILE, "w");
    fputs("yes please\n", f);
    fclose(f);
    assert(consent_granted() == 0);
    f = fopen(CONSENT_FILE, "w");
    fclose(f);
    assert(consent_granted() == 0);
}

int main(void) {
    printf("=== Zyl OS Telemetry Consent Tests ===\n");
    RUN_TEST(test_never_asked_means_no_collection);
    RUN_TEST(test_no_identifier_before_consent);
    RUN_TEST(test_consent_enables_collection_and_identifier);
    RUN_TEST(test_consent_survives_restart);
    RUN_TEST(test_withdrawal_erases_collected_data_and_identifier);
    RUN_TEST(test_nothing_collected_after_withdrawal);
    RUN_TEST(test_same_second_events_do_not_overwrite);
    RUN_TEST(test_unreadable_consent_is_refusal);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
