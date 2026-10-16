/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Service
 *
 * 역할: Telemetry 서비스 — 사용자 동의 하에서만 OTA/크래시/부팅 통계 수집
 * 수행범위: 동의 영속화(옵트인), 동의 철회 시 수집분·식별자 삭제,
 *           부팅 카운트, 이벤트 큐잉. 기기 UUID + OS 버전 + 이벤트 종류만.
 *           기기 UUID 는 GDPR 상 온라인 식별자(개인정보)이므로 동의 전에는
 *           만들지 않는다.
 * 의존방향: gio/gio.h (D-Bus), curl (HTTP POST)
 * SOLID: SRP — 텔레메트리 수집/전송만 담당
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <sys/random.h>   /* getrandom() */
#endif
#include <gio/gio.h>

#define TELEMETRY_ENDPOINT  "https://telemetry.zyl-os.com/v1/report"
/* Overridable so tests run against a scratch directory */
#ifndef TELEMETRY_DIR
#define TELEMETRY_DIR       "/data/telemetry"
#endif
#define DEVICE_UUID_FILE    TELEMETRY_DIR "/device_uuid"
#define BOOT_COUNT_FILE     TELEMETRY_DIR "/boot_count"
#define CONSENT_FILE        TELEMETRY_DIR "/consent"
#ifndef ZYL_OS_VERSION
#define ZYL_OS_VERSION      "unknown"   /* meson sets it from apps/system/release.js */
#endif
#define OS_VERSION          ZYL_OS_VERSION


/* ─── Consent ───────────────────────────────────────────────
 * Opt-in. Nothing is collected and no identifier exists until the user grants
 * consent (OOBE or Settings > Privacy). No file means never asked, which is a
 * refusal. Read fresh on every use, so a withdrawal takes effect at once and
 * survives a reboot — the in-memory flag this replaces defaulted to enabled
 * and came back on at every service restart.
 */
static int consent_granted(void) {
    FILE *f = fopen(CONSENT_FILE, "r");
    if (!f) return 0;
    int c = fgetc(f);
    fclose(f);
    return c == '1';
}

static int write_consent(int granted) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", CONSENT_FILE);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fputc(granted ? '1' : '0', f);
    fputc('\n', f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return rename(tmp, CONSENT_FILE);     /* atomic: never a half-written state */
}

static char g_device_uuid[64];

/* Withdrawal erases what was collected and the identifier itself. Stopping
 * future collection is not enough while queued events and a stable device ID
 * stay on disk. Also run at startup without consent, which removes data that
 * earlier versions collected before anyone was asked. */
static void purge_collected_data(void) {
    DIR *d = opendir(TELEMETRY_DIR);
    if (d) {
        struct dirent *e;
        char path[512];
        while ((e = readdir(d)) != NULL) {
            if (strncmp(e->d_name, "pending_", 8) == 0) {
                snprintf(path, sizeof(path), "%s/%s", TELEMETRY_DIR, e->d_name);
                unlink(path);
            }
        }
        closedir(d);
    }
    unlink(DEVICE_UUID_FILE);
    unlink(BOOT_COUNT_FILE);
    g_device_uuid[0] = '\0';
}

/* ─── Device UUID: generated once, persisted ─── */
static void load_or_create_uuid(void) {
    FILE *f = fopen(DEVICE_UUID_FILE, "r");
    if (f) {
        if (fgets(g_device_uuid, sizeof(g_device_uuid), f)) {
            size_t len = strlen(g_device_uuid);
            if (len > 0 && g_device_uuid[len - 1] == '\n') g_device_uuid[len - 1] = '\0';
        }
        fclose(f);
        if (g_device_uuid[0]) return;
    }

    /* Generate UUID v4 using getrandom() for cryptographically random bytes */
    {
        uint8_t rnd[16] = {0};
        gboolean have_random = FALSE;
#if defined(__linux__)
        ssize_t got = getrandom(rnd, sizeof(rnd), 0);
        have_random = (got == (ssize_t)sizeof(rnd));
#endif
        if (!have_random) {
            /* Fallback: /dev/urandom */
            FILE *ur = fopen("/dev/urandom", "rb");
            if (ur) {
                size_t rd = fread(rnd, 1, sizeof(rnd), ur);
                fclose(ur);
                have_random = (rd == sizeof(rnd));
            }
        }
        if (!have_random) {
            /* Last resort: time-based — weak but still avoids rand()/global state */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            uint64_t seed = (uint64_t)ts.tv_sec ^ (uint64_t)ts.tv_nsec;
            memcpy(rnd, &seed, sizeof(seed));
            memcpy(rnd + 8, &seed, sizeof(seed));
        }
        /* Set UUID v4 variant bits per RFC 4122 */
        rnd[6] = (rnd[6] & 0x0f) | 0x40;   /* version 4 */
        rnd[8] = (rnd[8] & 0x3f) | 0x80;   /* variant 2 */
        snprintf(g_device_uuid, sizeof(g_device_uuid),
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 rnd[0], rnd[1], rnd[2], rnd[3],
                 rnd[4], rnd[5],
                 rnd[6], rnd[7],
                 rnd[8], rnd[9],
                 rnd[10], rnd[11], rnd[12], rnd[13], rnd[14], rnd[15]);
    }

    f = fopen(DEVICE_UUID_FILE, "w");
    if (f) {
        fprintf(f, "%s\n", g_device_uuid);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
    }
}

/* ─── Boot counter ─── */
static int increment_boot_count(void) {
    int count = 0;
    FILE *f = fopen(BOOT_COUNT_FILE, "r");
    if (f) { fscanf(f, "%d", &count); fclose(f); }
    count++;
    f = fopen(BOOT_COUNT_FILE, "w");
    if (f) { fprintf(f, "%d\n", count); fflush(f); fsync(fileno(f)); fclose(f); }
    return count;
}

/* ─── Queue event to local file (for later batch send) ─── */
static int queue_event(const char *event_type, const char *payload) {
    if (!consent_granted()) return 0;          /* not an error: just not allowed */
    if (!g_device_uuid[0]) load_or_create_uuid();

    /* time + pid + counter: two events in the same second used to share a
       file name, so the second silently overwrote the first */
    static unsigned int seq;
    char path[512];
    snprintf(path, sizeof(path), "%s/pending_%ld_%d_%u.json", TELEMETRY_DIR,
             (long)time(NULL), (int)getpid(), seq++);

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "{\"device\":\"%s\",\"version\":\"%s\",\"event\":\"%s\",\"timestamp\":%ld",
            g_device_uuid, OS_VERSION, event_type, (long)time(NULL));
    if (payload && payload[0]) {
        fprintf(f, ",\"data\":%s", payload);
    }
    fprintf(f, "}\n");
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return 0;
}

/* ─── D-Bus interface: the daemon's (tests call the functions above) ─── */
#ifndef ZYL_TEST
static void ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) mkdir(path, 0700);
}

static const char *telemetry_introspection_xml =
    "<node>"
    "  <interface name='org.zylos.Telemetry'>"
    "    <method name='ReportEvent'>"
    "      <arg type='s' name='event_type' direction='in'/>"
    "      <arg type='s' name='payload' direction='in'/>"
    "    </method>"
    "    <method name='GetDeviceUUID'>"
    "      <arg type='s' name='uuid' direction='out'/>"
    "    </method>"
    "    <method name='GetBootCount'>"
    "      <arg type='i' name='count' direction='out'/>"
    "    </method>"
    "    <method name='SetEnabled'>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "    </method>"
    "    <method name='GetEnabled'>"
    "      <arg type='b' name='enabled' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";


static void handle_telemetry_method(GDBusConnection *conn, const gchar *sender,
                                     const gchar *path, const gchar *iface,
                                     const gchar *method, GVariant *params,
                                     GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;

    if (g_strcmp0(method, "ReportEvent") == 0) {
        const gchar *event_type = NULL;
        const gchar *payload = NULL;
        g_variant_get(params, "(&s&s)", &event_type, &payload);
        queue_event(event_type, payload);       /* checks consent itself */
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "GetDeviceUUID") == 0) {
        /* Without consent there is no identifier to hand out — and asking for
           it must not create one */
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(s)", consent_granted() ? g_device_uuid : ""));
    } else if (g_strcmp0(method, "GetBootCount") == 0) {
        int count = 0;
        FILE *f = fopen(BOOT_COUNT_FILE, "r");
        if (f) { fscanf(f, "%d", &count); fclose(f); }
        g_dbus_method_invocation_return_value(inv, g_variant_new("(i)", count));
    } else if (g_strcmp0(method, "SetEnabled") == 0) {
        gboolean enabled = FALSE;
        g_variant_get(params, "(b)", &enabled);
        if (write_consent(enabled ? 1 : 0) != 0) {
            g_dbus_method_invocation_return_error(inv, G_IO_ERROR, G_IO_ERROR_FAILED,
                "could not persist telemetry consent");
            return;
        }
        if (enabled) load_or_create_uuid();
        else purge_collected_data();
        g_message("[Telemetry] consent %s", enabled ? "granted" : "withdrawn");
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "GetEnabled") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", consent_granted() ? TRUE : FALSE));
    }
}

static const GDBusInterfaceVTable telemetry_vtable = { .method_call = handle_telemetry_method };

static void on_telemetry_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(telemetry_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, "/org/zylos/Telemetry",
            info->interfaces[0], &telemetry_vtable, NULL, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    ensure_dir(TELEMETRY_DIR);
    int boot_count = 0;
    if (consent_granted()) {
        load_or_create_uuid();
        boot_count = increment_boot_count();
        char payload[128];
        snprintf(payload, sizeof(payload), "{\"boot_count\":%d}", boot_count);
        queue_event("boot", payload);
    } else {
        purge_collected_data();
    }

    g_bus_own_name(G_BUS_TYPE_SYSTEM,
        "org.zylos.Telemetry", G_BUS_NAME_OWNER_FLAGS_NONE,
        on_telemetry_bus, NULL, NULL, NULL, NULL);

    /* The device ID stays out of the journal: logs are collected far more
       widely than telemetry and outlive a consent withdrawal */
    g_message("[Telemetry] Service started (consent=%s, boot=%d)",
              consent_granted() ? "granted" : "not granted", boot_count);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
