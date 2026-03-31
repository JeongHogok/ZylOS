#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: NFC 서비스 — neard D-Bus 프록시 구현
 * 수행범위: org.neard D-Bus 연동, NFC 어댑터 관리,
 *          태그 스캔/감지 콜백, NDEF 읽기/쓰기,
 *          org.zylos.NfcService D-Bus 인터페이스 노출
 * 의존방향: nfc.h, gio/gio.h (GDBus)
 * SOLID: SRP — NFC 데이터 수집 및 전달만 담당
 *        DIP — neard D-Bus 추상화, 구체 libnfc에 비의존
 * ────────────────────────────────────────────────────────── */

#include "nfc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <gio/gio.h>

/* ─── 내부 구조체 ─── */
struct ZylNfcService {
    GDBusConnection *system_bus;    /* 시스템 버스 (neard 접근) */
    GDBusConnection *session_bus;   /* 세션 버스 (org.zylos.NfcService 등록) */
    guint            dbus_owner_id;

    /* neard 어댑터 경로 */
    char            *adapter_path;  /* e.g. /org/neard/nfc0 */

    /* 스캔 상태 */
    bool             scanning;
    zyl_nfc_tag_detected_fn scan_cb;
    void            *scan_cb_data;
    guint            tag_added_sig;  /* neard InterfacesAdded 시그널 구독 ID */
    int              scan_timeout_sec;

    /* neard 인터페이스 추가 시그널 구독 */
    guint            interfaces_added_id;

    pthread_mutex_t  lock;
};

/* ─── neard 어댑터 경로 탐색 ─── */
static char *find_neard_adapter(ZylNfcService *svc) {
    if (!svc->system_bus) return NULL;

    GError *err = NULL;
    GDBusProxy *obj_mgr = g_dbus_proxy_new_sync(
        svc->system_bus,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        NEARD_DBUS_NAME,
        NEARD_DBUS_MANAGER,
        "org.freedesktop.DBus.ObjectManager",
        NULL, &err);

    if (!obj_mgr) {
        g_warning("[NFC] Cannot connect to neard: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
        return NULL;
    }

    GVariant *ret = g_dbus_proxy_call_sync(obj_mgr, "GetManagedObjects",
        NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);
    g_object_unref(obj_mgr);

    if (!ret) {
        g_warning("[NFC] GetManagedObjects failed: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
        return NULL;
    }

    /* ret: a{oa{sa{sv}}} — 경로 → 인터페이스 → 프로퍼티 */
    GVariantIter *obj_iter = NULL;
    g_variant_get(ret, "(a{oa{sa{sv}}})", &obj_iter);

    char *adapter = NULL;
    gchar *obj_path = NULL;
    GVariant *ifaces = NULL;

    while (g_variant_iter_next(obj_iter, "{o@a{sa{sv}}}", &obj_path, &ifaces)) {
        GVariantIter *iface_iter = g_variant_iter_new(ifaces);
        gchar *iface_name = NULL;
        GVariant *props = NULL;

        while (g_variant_iter_next(iface_iter, "{s@a{sv}}", &iface_name, &props)) {
            if (g_strcmp0(iface_name, NEARD_IFACE_ADAPTER) == 0) {
                adapter = g_strdup(obj_path);
                g_free(iface_name);
                g_variant_unref(props);
                break;
            }
            g_free(iface_name);
            g_variant_unref(props);
        }
        g_variant_iter_free(iface_iter);
        g_variant_unref(ifaces);
        g_free(obj_path);
        if (adapter) break;
    }

    g_variant_iter_free(obj_iter);
    g_variant_unref(ret);

    if (adapter) {
        g_message("[NFC] Found adapter: %s", adapter);
    } else {
        g_warning("[NFC] No NFC adapter found via neard");
    }
    return adapter;
}

/* ─── InterfacesAdded 시그널 핸들러 (태그 감지) ─── */
static void on_interfaces_added(GDBusConnection *conn, const gchar *sender,
                                 const gchar *path, const gchar *iface,
                                 const gchar *signal_name,
                                 GVariant *params, gpointer data) {
    ZylNfcService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface; (void)signal_name;

    gchar *obj_path = NULL;
    GVariant *ifaces = NULL;
    g_variant_get(params, "(o@a{sa{sv}})", &obj_path, &ifaces);

    if (!obj_path || !ifaces) {
        g_free(obj_path);
        if (ifaces) g_variant_unref(ifaces);
        return;
    }

    /* org.neard.Tag 인터페이스가 있는지 확인 */
    GVariantIter *iter = g_variant_iter_new(ifaces);
    gchar *iface_name = NULL;
    GVariant *props   = NULL;
    bool is_tag = false;

    while (g_variant_iter_next(iter, "{s@a{sv}}", &iface_name, &props)) {
        if (g_strcmp0(iface_name, NEARD_IFACE_TAG) == 0) {
            is_tag = true;
            g_free(iface_name);
            g_variant_unref(props);
            break;
        }
        g_free(iface_name);
        g_variant_unref(props);
    }
    g_variant_iter_free(iter);
    g_variant_unref(ifaces);

    if (!is_tag) {
        g_free(obj_path);
        return;
    }

    g_message("[NFC] Tag detected: %s", obj_path);

    /* 콜백 호출 */
    pthread_mutex_lock(&svc->lock);
    zyl_nfc_tag_detected_fn cb = svc->scan_cb;
    void *cb_data               = svc->scan_cb_data;
    pthread_mutex_unlock(&svc->lock);

    if (cb) {
        ZylNfcTag tag = {0};
        tag.path = obj_path;  /* caller 에게 넘기고 해제 */
        tag.type = ZYL_NFC_TAG_UNKNOWN;
        cb(&tag, cb_data);
        /* obj_path 는 tag.path 이므로 cb 내부에서 복사 사용 필요 */
    }
    g_free(obj_path);
}

/* ─── D-Bus 인트로스펙션 XML ─── */
static const char *nfc_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_NFC_DBUS_IFACE "'>"
    "    <method name='StartScan'>"
    "      <arg type='i' name='timeout_sec' direction='in'/>"
    "      <arg type='i' name='result'      direction='out'/>"
    "    </method>"
    "    <method name='StopScan'/>"
    "    <method name='ReadTag'>"
    "      <arg type='s' name='tag_path' direction='in'/>"
    "      <arg type='i' name='result'   direction='out'/>"
    "      <arg type='s' name='uid'      direction='out'/>"
    "      <arg type='s' name='type'     direction='out'/>"
    "    </method>"
    "    <method name='WriteTag'>"
    "      <arg type='s' name='tag_path'    direction='in'/>"
    "      <arg type='s' name='record_type' direction='in'/>"
    "      <arg type='s' name='payload'     direction='in'/>"
    "      <arg type='i' name='result'      direction='out'/>"
    "    </method>"
    "    <signal name='TagDetected'>"
    "      <arg type='s' name='tag_path'/>"
    "      <arg type='s' name='tag_type'/>"
    "      <arg type='s' name='uid'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* ─── D-Bus 메서드 핸들러 ─── */
static void handle_nfc_method(GDBusConnection *conn, const gchar *sender,
                               const gchar *path, const gchar *iface,
                               const gchar *method, GVariant *params,
                               GDBusMethodInvocation *inv, gpointer data) {
    ZylNfcService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "StartScan") == 0) {
        gint32 timeout = 0;
        g_variant_get(params, "(i)", &timeout);
        ZylNfcResult r = zyl_nfc_start_scan(svc, (int)timeout, NULL, NULL);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(i)", (gint32)r));

    } else if (g_strcmp0(method, "StopScan") == 0) {
        zyl_nfc_stop_scan(svc);
        g_dbus_method_invocation_return_value(inv, NULL);

    } else if (g_strcmp0(method, "ReadTag") == 0) {
        const gchar *tag_path = NULL;
        g_variant_get(params, "(&s)", &tag_path);
        ZylNfcTag out = {0};
        ZylNfcResult r = zyl_nfc_read_tag(svc, tag_path, &out);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(iss)", (gint32)r,
                          out.uid[0] ? out.uid : "",
                          out.type   ? "Type2"  : "Unknown"));
        zyl_nfc_tag_free(&out);

    } else if (g_strcmp0(method, "WriteTag") == 0) {
        const gchar *tag_path = NULL, *rec_type = NULL, *payload = NULL;
        g_variant_get(params, "(&s&s&s)", &tag_path, &rec_type, &payload);
        ZylNdefRecord record = {
            .type        = (char *)rec_type,
            .payload     = (uint8_t *)payload,
            .payload_len = strlen(payload)
        };
        ZylNfcResult r = zyl_nfc_write_tag(svc, tag_path, &record);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(i)", (gint32)r));

    } else {
        g_dbus_method_invocation_return_error(inv,
            G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method: %s", method);
    }
}

static const GDBusInterfaceVTable nfc_vtable = {
    .method_call = handle_nfc_method,
};

static void on_nfc_bus_acquired(GDBusConnection *conn, const gchar *name,
                                 gpointer data) {
    ZylNfcService *svc = data;
    svc->session_bus = conn;

    GError *err = NULL;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(
        nfc_introspection_xml, &err);
    if (!info) {
        g_warning("[NFC] Introspection XML parse failed: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
        return;
    }

    g_dbus_connection_register_object(conn,
        ZYL_NFC_DBUS_PATH,
        info->interfaces[0],
        &nfc_vtable,
        svc, NULL, NULL);
    g_dbus_node_info_unref(info);

    g_message("[NFC] D-Bus registered: %s", ZYL_NFC_DBUS_NAME);
    (void)name;
}

/* ─── 공개 API ─── */

ZylNfcService *zyl_nfc_create(void) {
    GError *err = NULL;

    ZylNfcService *svc = g_new0(ZylNfcService, 1);
    if (!svc) return NULL;

    pthread_mutex_init(&svc->lock, NULL);

    /* 시스템 버스 연결 (neard 접근) */
    svc->system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!svc->system_bus) {
        g_warning("[NFC] Cannot connect to system bus: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
        pthread_mutex_destroy(&svc->lock);
        g_free(svc);
        return NULL;
    }

    /* 어댑터 탐색 */
    svc->adapter_path = find_neard_adapter(svc);

    /* 세션 버스에 org.zylos.NfcService 등록 */
    svc->dbus_owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        ZYL_NFC_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_nfc_bus_acquired,
        NULL, NULL,
        svc, NULL);

    g_message("[NFC] ZylNfcService created (adapter=%s)",
              svc->adapter_path ? svc->adapter_path : "none");
    return svc;
}

void zyl_nfc_destroy(ZylNfcService *svc) {
    if (!svc) return;

    zyl_nfc_stop_scan(svc);

    if (svc->dbus_owner_id) {
        g_bus_unown_name(svc->dbus_owner_id);
    }
    if (svc->interfaces_added_id && svc->system_bus) {
        g_dbus_connection_signal_unsubscribe(svc->system_bus,
                                             svc->interfaces_added_id);
    }
    g_free(svc->adapter_path);

    if (svc->system_bus) {
        g_object_unref(svc->system_bus);
    }

    pthread_mutex_destroy(&svc->lock);
    g_free(svc);
    g_message("[NFC] ZylNfcService destroyed");
}

ZylNfcResult zyl_nfc_start_scan(ZylNfcService *svc,
                                 int timeout_sec,
                                 zyl_nfc_tag_detected_fn callback,
                                 void *user_data) {
    if (!svc) return ZYL_NFC_ERR_GENERAL;
    if (!svc->adapter_path) return ZYL_NFC_ERR_NO_ADAPTER;

    /* 어댑터 프록시 취득 */
    GError *err = NULL;
    GDBusProxy *adapter = g_dbus_proxy_new_sync(
        svc->system_bus,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        NEARD_DBUS_NAME,
        svc->adapter_path,
        NEARD_IFACE_ADAPTER,
        NULL, &err);

    if (!adapter) {
        g_warning("[NFC] Cannot get adapter proxy: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
        return ZYL_NFC_ERR_DBUS;
    }

    /* neard Adapter.StartPollLoop("Initiator") */
    GVariant *ret = g_dbus_proxy_call_sync(adapter, "StartPollLoop",
        g_variant_new("(s)", "Initiator"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);
    g_object_unref(adapter);

    if (!ret) {
        g_warning("[NFC] StartPollLoop failed: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
        return ZYL_NFC_ERR_DBUS;
    }
    g_variant_unref(ret);

    pthread_mutex_lock(&svc->lock);
    svc->scanning         = true;
    svc->scan_cb          = callback;
    svc->scan_cb_data     = user_data;
    svc->scan_timeout_sec = timeout_sec;
    pthread_mutex_unlock(&svc->lock);

    /* neard InterfacesAdded 시그널 구독 (태그 감지) */
    if (svc->interfaces_added_id == 0) {
        svc->interfaces_added_id = g_dbus_connection_signal_subscribe(
            svc->system_bus,
            NEARD_DBUS_NAME,
            "org.freedesktop.DBus.ObjectManager",
            "InterfacesAdded",
            NULL, NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_interfaces_added,
            svc, NULL);
    }

    g_message("[NFC] Scan started (timeout=%ds)", timeout_sec);
    return ZYL_NFC_OK;
}

void zyl_nfc_stop_scan(ZylNfcService *svc) {
    if (!svc || !svc->scanning) return;
    if (!svc->adapter_path) return;

    GError *err = NULL;
    GDBusProxy *adapter = g_dbus_proxy_new_sync(
        svc->system_bus,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        NEARD_DBUS_NAME,
        svc->adapter_path,
        NEARD_IFACE_ADAPTER,
        NULL, &err);

    if (adapter) {
        GVariant *ret = g_dbus_proxy_call_sync(adapter, "StopPollLoop",
            NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL);
        if (ret) g_variant_unref(ret);
        g_object_unref(adapter);
    } else {
        g_clear_error(&err);
    }

    if (svc->interfaces_added_id && svc->system_bus) {
        g_dbus_connection_signal_unsubscribe(svc->system_bus,
                                             svc->interfaces_added_id);
        svc->interfaces_added_id = 0;
    }

    pthread_mutex_lock(&svc->lock);
    svc->scanning = false;
    pthread_mutex_unlock(&svc->lock);

    g_message("[NFC] Scan stopped");
}

ZylNfcResult zyl_nfc_read_tag(ZylNfcService *svc,
                               const char *tag_path,
                               ZylNfcTag *out) {
    if (!svc || !tag_path || !out) return ZYL_NFC_ERR_GENERAL;

    memset(out, 0, sizeof(*out));

    GError *err = NULL;

    /* org.neard.Tag 프록시 */
    GDBusProxy *tag_proxy = g_dbus_proxy_new_sync(
        svc->system_bus,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        NEARD_DBUS_NAME,
        tag_path,
        NEARD_IFACE_TAG,
        NULL, &err);

    if (!tag_proxy) {
        g_warning("[NFC] Cannot get tag proxy '%s': %s",
                  tag_path, err ? err->message : "unknown");
        g_clear_error(&err);
        return ZYL_NFC_ERR_DBUS;
    }

    out->path = strdup(tag_path);

    /* 프로퍼티 읽기: Type, UID */
    GVariant *type_v = g_dbus_proxy_get_cached_property(tag_proxy, "Type");
    GVariant *uid_v  = g_dbus_proxy_get_cached_property(tag_proxy, "UID");

    if (type_v) {
        const gchar *t = g_variant_get_string(type_v, NULL);
        if      (g_strcmp0(t, "Type 1") == 0) out->type = ZYL_NFC_TAG_TYPE1;
        else if (g_strcmp0(t, "Type 2") == 0) out->type = ZYL_NFC_TAG_TYPE2;
        else if (g_strcmp0(t, "Type 3") == 0) out->type = ZYL_NFC_TAG_TYPE3;
        else if (g_strcmp0(t, "Type 4") == 0) out->type = ZYL_NFC_TAG_TYPE4;
        else                                   out->type = ZYL_NFC_TAG_UNKNOWN;
        g_variant_unref(type_v);
    }
    if (uid_v) {
        const gchar *u = g_variant_get_string(uid_v, NULL);
        snprintf(out->uid, sizeof(out->uid), "%s", u ? u : "");
        g_variant_unref(uid_v);
    }

    g_object_unref(tag_proxy);
    g_message("[NFC] Read tag '%s': uid='%s'", tag_path, out->uid);
    return ZYL_NFC_OK;
}

ZylNfcResult zyl_nfc_write_tag(ZylNfcService *svc,
                                const char *tag_path,
                                const ZylNdefRecord *record) {
    if (!svc || !tag_path || !record) return ZYL_NFC_ERR_GENERAL;
    if (!record->payload || record->payload_len == 0) return ZYL_NFC_ERR_GENERAL;

    GError *err = NULL;

    /* neard Tag.Write(a{sv}) */
    GDBusProxy *tag_proxy = g_dbus_proxy_new_sync(
        svc->system_bus,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        NEARD_DBUS_NAME,
        tag_path,
        NEARD_IFACE_TAG,
        NULL, &err);

    if (!tag_proxy) {
        g_warning("[NFC] Cannot get tag proxy for write '%s': %s",
                  tag_path, err ? err->message : "unknown");
        g_clear_error(&err);
        return ZYL_NFC_ERR_DBUS;
    }

    /* NDEF 레코드 딕셔너리 구성 */
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&builder, "{sv}", "Type",
        g_variant_new_string(record->type ? record->type : "text/plain"));
    g_variant_builder_add(&builder, "{sv}", "Representation",
        g_variant_new_string((const char *)record->payload));

    GVariant *ret = g_dbus_proxy_call_sync(tag_proxy, "Write",
        g_variant_new("(a{sv})", &builder),
        G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &err);
    g_object_unref(tag_proxy);

    if (!ret) {
        g_warning("[NFC] Write to '%s' failed: %s",
                  tag_path, err ? err->message : "unknown");
        g_clear_error(&err);
        return ZYL_NFC_ERR_WRITE;
    }

    g_variant_unref(ret);
    g_message("[NFC] Write to '%s' succeeded", tag_path);
    return ZYL_NFC_OK;
}

void zyl_nfc_tag_free(ZylNfcTag *tag) {
    if (!tag) return;
    free(tag->path);
    tag->path = NULL;

    if (tag->records) {
        for (int i = 0; i < tag->record_count; i++) {
            free(tag->records[i].type);
            free(tag->records[i].payload);
        }
        free(tag->records);
        tag->records = NULL;
    }
    tag->record_count = 0;
}

/* ─── main(): 독립 데몬 실행 ─── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    g_message("[NFC] Zyl NFC Service starting...");

    ZylNfcService *svc = zyl_nfc_create();
    if (!svc) {
        g_critical("[NFC] Failed to create NFC service");
        return 1;
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_message("[NFC] Entering main loop");
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    zyl_nfc_destroy(svc);
    g_message("[NFC] Service stopped");
    return 0;
}
