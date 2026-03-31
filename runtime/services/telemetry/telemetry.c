/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Service
 *
 * 역할: Telemetry 서비스 — 익명 OTA/크래시/부팅 통계 수집 + 보고
 * 수행범위: 부팅 카운트, 크래시 리포트 전송, OTA 결과 보고.
 *           개인정보 수집 안 함 — 기기 UUID + OS 버전 + 이벤트 종류만.
 * 의존방향: gio/gio.h (D-Bus), curl (HTTP POST)
 * SOLID: SRP — 텔레메트리 수집/전송만 담당
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <gio/gio.h>

#define TELEMETRY_ENDPOINT  "https://telemetry.zylos.dev/v1/report"
#define TELEMETRY_DIR       "/data/telemetry"
#define DEVICE_UUID_FILE    "/data/telemetry/device_uuid"
#define BOOT_COUNT_FILE     "/data/telemetry/boot_count"
#define OS_VERSION          "0.1.0"

static char g_device_uuid[64] = {0};

static void ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) mkdir(path, 0700);
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

    /* Generate UUID-like identifier (not cryptographically random, but sufficient for anon telemetry) */
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    snprintf(g_device_uuid, sizeof(g_device_uuid),
             "%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
             rand() & 0xffff, rand() & 0xffff,
             rand() & 0xffff,
             (rand() & 0x0fff) | 0x4000,
             (rand() & 0x3fff) | 0x8000,
             rand() & 0xffff, rand() & 0xffff, rand() & 0xffff);

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
    char path[512];
    snprintf(path, sizeof(path), "%s/pending_%ld.json", TELEMETRY_DIR, (long)time(NULL));

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

/* ─── D-Bus interface ─── */
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
    "  </interface>"
    "</node>";

static gboolean g_telemetry_enabled = TRUE;

static void handle_telemetry_method(GDBusConnection *conn, const gchar *sender,
                                     const gchar *path, const gchar *iface,
                                     const gchar *method, GVariant *params,
                                     GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;

    if (g_strcmp0(method, "ReportEvent") == 0) {
        const gchar *event_type = NULL;
        const gchar *payload = NULL;
        g_variant_get(params, "(&s&s)", &event_type, &payload);
        if (g_telemetry_enabled) {
            queue_event(event_type, payload);
        }
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "GetDeviceUUID") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(s)", g_device_uuid));
    } else if (g_strcmp0(method, "GetBootCount") == 0) {
        int count = 0;
        FILE *f = fopen(BOOT_COUNT_FILE, "r");
        if (f) { fscanf(f, "%d", &count); fclose(f); }
        g_dbus_method_invocation_return_value(inv, g_variant_new("(i)", count));
    } else if (g_strcmp0(method, "SetEnabled") == 0) {
        gboolean enabled = FALSE;
        g_variant_get(params, "(b)", &enabled);
        g_telemetry_enabled = enabled;
        g_message("[Telemetry] %s", enabled ? "Enabled" : "Disabled");
        g_dbus_method_invocation_return_value(inv, NULL);
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
    load_or_create_uuid();
    int boot_count = increment_boot_count();

    /* Report boot event */
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"boot_count\":%d}", boot_count);
    queue_event("boot", payload);

    g_bus_own_name(G_BUS_TYPE_SESSION,
        "org.zylos.Telemetry", G_BUS_NAME_OWNER_FLAGS_NONE,
        on_telemetry_bus, NULL, NULL, NULL, NULL);

    g_message("[Telemetry] Service started (device=%s, boot=%d)", g_device_uuid, boot_count);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
