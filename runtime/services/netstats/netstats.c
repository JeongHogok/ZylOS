/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Service
 *
 * 역할: 데이터 사용량 서비스 — 망 종류별 사용량 측정, 청구 주기, 경고/한도
 * 수행범위: /sys/class/net 카운터를 모바일·Wi-Fi·유선으로 누적, 재부팅·
 *           인터페이스 재생성에도 이중 집계 없이 이어 세기, 청구 주기일에
 *           리셋, 모바일 한도 도달 시 모바일 데이터 연결만 끊기(통화 유지)
 * 의존방향: gio/gio.h (D-Bus, GKeyFile), nmcli (NetworkManager)
 * SOLID: SRP — 사용량 회계와 한도 집행만 담당. 알림 문구는 OS UI(i18n) 몫
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <gio/gio.h>
#ifndef ZYL_TEST
#include <glib-unix.h>
#endif

/* Overridable so tests run against a scratch tree */
#ifndef NETSTATS_DIR
#define NETSTATS_DIR     "/data/netstats"
#endif
#ifndef NETSTATS_SYSFS
#define NETSTATS_SYSFS   "/sys/class/net"
#endif
#ifndef NETSTATS_BOOT_ID
#define NETSTATS_BOOT_ID "/proc/sys/kernel/random/boot_id"
#endif
#define STATE_FILE       NETSTATS_DIR "/state"
/* shortcut: polling lets a fast link overshoot the limit by up to one interval
 * of traffic (~60 MB at 100 Mbit/s); move to an nftables quota rule when a
 * carrier needs the cut to be exact. */
#define POLL_SECONDS     5
#define SAVE_SECONDS     60

enum { NET_MOBILE, NET_WIFI, NET_ETHERNET, NET_KINDS };
static const char *const KIND_KEYS[NET_KINDS] = { "mobile", "wifi", "ethernet" };

typedef struct {
    /* policy */
    guint    cycle_day;      /* 1..28 — every month has it */
    guint64  warning_bytes;  /* 0 = off */
    guint64  limit_bytes;    /* 0 = off */
    /* current cycle */
    gint64   cycle_start;    /* local midnight of the cycle day */
    guint64  rx[NET_KINDS], tx[NET_KINDS];
    gboolean warned, limited;
    /* raw kernel counters last seen this boot: iface → "rx tx" */
    char     boot_id[64];
    GHashTable *last;
} NetStats;

static NetStats g_ns;

/* ─── Interface classification ──────────────────────────────
 * Only interfaces where traffic leaves the device count. Virtual ones
 * (tun, veth, bridges) carry traffic that is already counted on the
 * physical link underneath — counting both would double a VPN user's usage. */
static gboolean sysfs_exists(const char *iface, const char *leaf) {
    char path[512];
    snprintf(path, sizeof(path), NETSTATS_SYSFS "/%s/%s", iface, leaf);
    return g_file_test(path, G_FILE_TEST_EXISTS);
}

static int classify(const char *iface) {
    if (strcmp(iface, "lo") == 0) return -1;
    static const char *const mobile_prefixes[] = { "wwan", "rmnet", "ccmni", "ppp", NULL };
    for (int i = 0; mobile_prefixes[i]; i++)
        if (g_str_has_prefix(iface, mobile_prefixes[i])) return NET_MOBILE;

    char path[512], *uevent = NULL;
    snprintf(path, sizeof(path), NETSTATS_SYSFS "/%s/uevent", iface);
    if (g_file_get_contents(path, &uevent, NULL, NULL)) {
        gboolean wwan = strstr(uevent, "DEVTYPE=wwan") != NULL;
        g_free(uevent);
        if (wwan) return NET_MOBILE;
    }
    if (sysfs_exists(iface, "wireless") || sysfs_exists(iface, "phy80211")) return NET_WIFI;
    if (sysfs_exists(iface, "device")) return NET_ETHERNET;
    return -1;
}

static guint64 read_counter(const char *iface, const char *name) {
    char path[512], *text = NULL;
    snprintf(path, sizeof(path), NETSTATS_SYSFS "/%s/statistics/%s", iface, name);
    if (!g_file_get_contents(path, &text, NULL, NULL)) return 0;
    guint64 v = g_ascii_strtoull(text, NULL, 10);
    g_free(text);
    return v;
}

static void read_boot_id(char *out, size_t len) {
    char *text = NULL;
    out[0] = '\0';
    if (g_file_get_contents(NETSTATS_BOOT_ID, &text, NULL, NULL)) {
        g_strlcpy(out, g_strstrip(text), len);
        g_free(text);
    }
}

/* ─── Billing cycle ─── */
/* Start of the cycle containing `now`: the latest local midnight on cycle_day */
static gint64 cycle_start_for(guint day, time_t now) {
    struct tm tm;
    localtime_r(&now, &tm);
    int year = tm.tm_year, mon = tm.tm_mon;
    if (tm.tm_mday < (int)day && --mon < 0) { mon = 11; year--; }
    struct tm s = { 0 };
    s.tm_year = year; s.tm_mon = mon; s.tm_mday = (int)day; s.tm_isdst = -1;
    return (gint64)mktime(&s);
}

/* ─── Mobile data on/off ─────────────────────────────────────
 * Disconnects the modem's data connection, not the radio: `nmcli radio wwan
 * off` would power the modem down and drop calls and SMS with it. NM keeps a
 * disconnected device from auto-reconnecting until told to connect. */
#ifdef ZYL_TEST
static int g_test_mobile_calls;
static gboolean g_test_mobile_enabled = TRUE;
static void set_mobile_data(gboolean enabled) {
    g_test_mobile_calls++;
    g_test_mobile_enabled = enabled;
}
#else
static void set_mobile_data(gboolean enabled) {
    char *out = NULL;
    const char *list[] = { "nmcli", "-t", "-f", "DEVICE,TYPE", "device", NULL };
    if (!g_spawn_sync(NULL, (char **)list, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                      NULL, NULL, &out, NULL, NULL, NULL)) {
        g_warning("[NetStats] nmcli unavailable — cannot %s mobile data",
                  enabled ? "resume" : "stop");
        return;
    }
    char **lines = g_strsplit(out, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *sep = strrchr(lines[i], ':');
        if (!sep || strcmp(sep + 1, "gsm") != 0) continue;
        *sep = '\0';
        const char *argv[] = { "nmcli", "device", enabled ? "connect" : "disconnect",
                               lines[i], NULL };
        g_spawn_sync(NULL, (char **)argv, NULL,
                     G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                     NULL, NULL, NULL, NULL, NULL, NULL);
    }
    g_strfreev(lines);
    g_free(out);
    g_message("[NetStats] mobile data %s", enabled ? "resumed" : "stopped (limit reached)");
}
#endif

/* ─── Persistence ─── */
static void netstats_save(const NetStats *ns) {
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_integer(kf, "policy", "cycle_day", (gint)ns->cycle_day);
    g_key_file_set_uint64(kf, "policy", "warning_bytes", ns->warning_bytes);
    g_key_file_set_uint64(kf, "policy", "limit_bytes", ns->limit_bytes);
    g_key_file_set_int64(kf, "cycle", "start", ns->cycle_start);
    g_key_file_set_boolean(kf, "cycle", "warned", ns->warned);
    g_key_file_set_boolean(kf, "cycle", "limited", ns->limited);
    for (int k = 0; k < NET_KINDS; k++) {
        char key[32];
        snprintf(key, sizeof(key), "%s_rx", KIND_KEYS[k]);
        g_key_file_set_uint64(kf, "cycle", key, ns->rx[k]);
        snprintf(key, sizeof(key), "%s_tx", KIND_KEYS[k]);
        g_key_file_set_uint64(kf, "cycle", key, ns->tx[k]);
    }
    g_key_file_set_string(kf, "boot", "id", ns->boot_id);
    GHashTableIter it;
    gpointer iface, val;
    g_hash_table_iter_init(&it, ns->last);
    while (g_hash_table_iter_next(&it, &iface, &val))
        g_key_file_set_string(kf, "iface", iface, val);

    g_mkdir_with_parents(NETSTATS_DIR, 0700);
    GError *err = NULL;
    if (!g_key_file_save_to_file(kf, STATE_FILE, &err)) {   /* atomic replace */
        g_warning("[NetStats] cannot save state: %s", err->message);
        g_error_free(err);
    }
    g_key_file_free(kf);
}

static void netstats_load(NetStats *ns, time_t now) {
    memset(ns->rx, 0, sizeof(ns->rx));
    memset(ns->tx, 0, sizeof(ns->tx));
    ns->cycle_day = 1;
    ns->warning_bytes = ns->limit_bytes = 0;
    ns->warned = ns->limited = FALSE;
    ns->cycle_start = 0;
    if (ns->last) g_hash_table_destroy(ns->last);
    ns->last = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    char boot_id[64];
    read_boot_id(boot_id, sizeof(boot_id));
    g_strlcpy(ns->boot_id, boot_id, sizeof(ns->boot_id));

    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, STATE_FILE, G_KEY_FILE_NONE, NULL)) {
        gint day = g_key_file_get_integer(kf, "policy", "cycle_day", NULL);
        if (day >= 1 && day <= 28) ns->cycle_day = (guint)day;
        ns->warning_bytes = g_key_file_get_uint64(kf, "policy", "warning_bytes", NULL);
        ns->limit_bytes   = g_key_file_get_uint64(kf, "policy", "limit_bytes", NULL);
        ns->cycle_start   = g_key_file_get_int64(kf, "cycle", "start", NULL);
        ns->warned        = g_key_file_get_boolean(kf, "cycle", "warned", NULL);
        ns->limited       = g_key_file_get_boolean(kf, "cycle", "limited", NULL);
        for (int k = 0; k < NET_KINDS; k++) {
            char key[32];
            snprintf(key, sizeof(key), "%s_rx", KIND_KEYS[k]);
            ns->rx[k] = g_key_file_get_uint64(kf, "cycle", key, NULL);
            snprintf(key, sizeof(key), "%s_tx", KIND_KEYS[k]);
            ns->tx[k] = g_key_file_get_uint64(kf, "cycle", key, NULL);
        }
        /* Kernel counters restart at zero on reboot: last-seen values from an
           earlier boot would be subtracted from the wrong baseline */
        char *saved_boot = g_key_file_get_string(kf, "boot", "id", NULL);
        if (saved_boot && boot_id[0] && strcmp(saved_boot, boot_id) == 0) {
            gchar **keys = g_key_file_get_keys(kf, "iface", NULL, NULL);
            for (int i = 0; keys && keys[i]; i++)
                g_hash_table_replace(ns->last, g_strdup(keys[i]),
                                     g_key_file_get_string(kf, "iface", keys[i], NULL));
            g_strfreev(keys);
        }
        g_free(saved_boot);
    }
    g_key_file_free(kf);
    if (!ns->cycle_start) ns->cycle_start = cycle_start_for(ns->cycle_day, now);
}

/* ─── Accounting ─── */
static gboolean netstats_roll_cycle(NetStats *ns, time_t now) {
    gint64 start = cycle_start_for(ns->cycle_day, now);
    if (start == ns->cycle_start) return FALSE;
    if (start > ns->cycle_start) {
        memset(ns->rx, 0, sizeof(ns->rx));
        memset(ns->tx, 0, sizeof(ns->tx));
        ns->warned = FALSE;
        if (ns->limited) {
            ns->limited = FALSE;
            set_mobile_data(TRUE);
        }
    }
    /* An earlier start (cycle day moved back, clock corrected) keeps the
       counts: they are a subset of the new cycle and cannot be rebuilt */
    ns->cycle_start = start;
    return TRUE;
}

static guint64 mobile_used(const NetStats *ns) {
    return ns->rx[NET_MOBILE] + ns->tx[NET_MOBILE];
}

/* Re-evaluates warning and limit. Returns TRUE when a flag changed. */
static gboolean netstats_apply_policy(NetStats *ns, gboolean mobile_moved) {
    guint64 used = mobile_used(ns);
    gboolean changed = FALSE;
    gboolean warn = ns->warning_bytes && used >= ns->warning_bytes;
    if (warn != ns->warned) { ns->warned = warn; changed = TRUE; }

    gboolean over = ns->limit_bytes && used >= ns->limit_bytes;
    if (over && !ns->limited) {
        ns->limited = TRUE;
        set_mobile_data(FALSE);
        changed = TRUE;
    } else if (!over && ns->limited) {          /* user raised or removed the limit */
        ns->limited = FALSE;
        set_mobile_data(TRUE);
        changed = TRUE;
    } else if (over && mobile_moved) {
        set_mobile_data(FALSE);                 /* the modem came back (reboot, NM restart) */
    }
    return changed;
}

/* Reads every counter and adds what moved since the last read.
 * Returns TRUE when something persistent changed besides the counts. */
static gboolean netstats_poll(NetStats *ns, time_t now) {
    gboolean event = netstats_roll_cycle(ns, now);
    gboolean mobile_moved = FALSE;
    DIR *d = opendir(NETSTATS_SYSFS);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            int kind = classify(e->d_name);
            if (kind < 0) continue;
            guint64 rx = read_counter(e->d_name, "rx_bytes");
            guint64 tx = read_counter(e->d_name, "tx_bytes");
            guint64 lrx = 0, ltx = 0;
            const char *last = g_hash_table_lookup(ns->last, e->d_name);
            if (last) sscanf(last, "%" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT, &lrx, &ltx);
            /* A counter below its last value means the interface was
               re-created (modem reset, USB replug) and restarted at zero */
            guint64 drx = rx >= lrx ? rx - lrx : rx;
            guint64 dtx = tx >= ltx ? tx - ltx : tx;
            ns->rx[kind] += drx;
            ns->tx[kind] += dtx;
            if (kind == NET_MOBILE && (drx || dtx)) mobile_moved = TRUE;
            g_hash_table_replace(ns->last, g_strdup(e->d_name),
                g_strdup_printf("%" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT, rx, tx));
        }
        closedir(d);
    }
    return netstats_apply_policy(ns, mobile_moved) || event;
}

static gboolean netstats_set_policy(NetStats *ns, guint day, guint64 warning,
                                    guint64 limit, time_t now) {
    if (day < 1 || day > 28) return FALSE;
    ns->cycle_day = day;
    ns->warning_bytes = warning;
    ns->limit_bytes = limit;
    netstats_poll(ns, now);
    return TRUE;
}

/* ─── D-Bus interface ─── */
static const char *netstats_introspection_xml =
    "<node>"
    "  <interface name='org.zylos.NetStats'>"
    "    <method name='GetUsage'>"
    "      <arg type='a{sv}' name='usage' direction='out'/>"
    "    </method>"
    "    <method name='GetPolicy'>"
    "      <arg type='a{sv}' name='policy' direction='out'/>"
    "    </method>"
    "    <method name='SetPolicy'>"
    "      <arg type='u' name='cycle_day' direction='in'/>"
    "      <arg type='t' name='warning_bytes' direction='in'/>"
    "      <arg type='t' name='limit_bytes' direction='in'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static GVariant *usage_variant(const NetStats *ns) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", "supported", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&b, "{sv}", "mobileRx", g_variant_new_uint64(ns->rx[NET_MOBILE]));
    g_variant_builder_add(&b, "{sv}", "mobileTx", g_variant_new_uint64(ns->tx[NET_MOBILE]));
    g_variant_builder_add(&b, "{sv}", "wifiRx", g_variant_new_uint64(ns->rx[NET_WIFI]));
    g_variant_builder_add(&b, "{sv}", "wifiTx", g_variant_new_uint64(ns->tx[NET_WIFI]));
    g_variant_builder_add(&b, "{sv}", "ethernetRx", g_variant_new_uint64(ns->rx[NET_ETHERNET]));
    g_variant_builder_add(&b, "{sv}", "ethernetTx", g_variant_new_uint64(ns->tx[NET_ETHERNET]));
    g_variant_builder_add(&b, "{sv}", "cycleStart", g_variant_new_int64(ns->cycle_start));
    g_variant_builder_add(&b, "{sv}", "warned", g_variant_new_boolean(ns->warned));
    g_variant_builder_add(&b, "{sv}", "limited", g_variant_new_boolean(ns->limited));
    return g_variant_new("(a{sv})", &b);
}

static GVariant *policy_variant(const NetStats *ns) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", "cycleDay", g_variant_new_uint32(ns->cycle_day));
    g_variant_builder_add(&b, "{sv}", "warningBytes", g_variant_new_uint64(ns->warning_bytes));
    g_variant_builder_add(&b, "{sv}", "limitBytes", g_variant_new_uint64(ns->limit_bytes));
    return g_variant_new("(a{sv})", &b);
}

static void handle_netstats_method(GDBusConnection *conn, const gchar *sender,
                                   const gchar *path, const gchar *iface,
                                   const gchar *method, GVariant *params,
                                   GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;
    time_t now = time(NULL);

    if (g_strcmp0(method, "GetUsage") == 0) {
        if (netstats_poll(&g_ns, now)) netstats_save(&g_ns);
        g_dbus_method_invocation_return_value(inv, usage_variant(&g_ns));
    } else if (g_strcmp0(method, "GetPolicy") == 0) {
        g_dbus_method_invocation_return_value(inv, policy_variant(&g_ns));
    } else if (g_strcmp0(method, "SetPolicy") == 0) {
        guint32 day = 0;
        guint64 warning = 0, limit = 0;
        g_variant_get(params, "(utt)", &day, &warning, &limit);
        if (!netstats_set_policy(&g_ns, day, warning, limit, now)) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                G_DBUS_ERROR_INVALID_ARGS, "cycle_day must be 1-28");
            return;
        }
        netstats_save(&g_ns);
        g_dbus_method_invocation_return_value(inv, NULL);
    }
}

static const GDBusInterfaceVTable netstats_vtable = { .method_call = handle_netstats_method };

#ifndef ZYL_TEST
static void on_netstats_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(netstats_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0])
        g_dbus_connection_register_object(conn, "/org/zylos/NetStats",
            info->interfaces[0], &netstats_vtable, NULL, NULL, NULL);
    if (info) g_dbus_node_info_unref(info);
}

static gboolean on_poll(gpointer data) {
    (void)data;
    static gint64 last_save;
    gint64 mono = g_get_monotonic_time() / G_USEC_PER_SEC;
    /* Flags and cycle changes are written at once; counts only every minute,
       sparing flash from a write every few seconds */
    if (netstats_poll(&g_ns, time(NULL)) || mono - last_save >= SAVE_SECONDS) {
        netstats_save(&g_ns);
        last_save = mono;
    }
    return G_SOURCE_CONTINUE;
}

static gboolean on_term(gpointer loop) {
    netstats_save(&g_ns);
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    time_t now = time(NULL);
    netstats_load(&g_ns, now);
    if (g_ns.limited) set_mobile_data(FALSE);   /* a reboot must not lift the limit */
    netstats_poll(&g_ns, now);
    netstats_save(&g_ns);

    g_bus_own_name(G_BUS_TYPE_SESSION, "org.zylos.NetStats", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_netstats_bus, NULL, NULL, NULL, NULL);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add_seconds(POLL_SECONDS, on_poll, NULL);
    g_unix_signal_add(SIGTERM, on_term, loop);
    g_unix_signal_add(SIGINT, on_term, loop);
    g_message("[NetStats] started (cycle day %u, limit %" G_GUINT64_FORMAT ")",
              g_ns.cycle_day, g_ns.limit_bytes);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
