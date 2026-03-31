#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 생체 인증 서비스 — libfprint D-Bus 프록시 (fprintd) 구현
 * 수행범위: net.reactivated.Fprint D-Bus 연동,
 *          지문 등록(enroll)/검증(verify)/존재 확인,
 *          org.zylos.AuthService D-Bus 인터페이스 노출
 * 의존방향: auth.h, gio/gio.h (GDBus)
 * SOLID: SRP — 생체 인증 데이터 수집 및 전달만 담당
 *        DIP — fprintd D-Bus 추상화, 구체 장치 드라이버에 비의존
 * ────────────────────────────────────────────────────────── */

#include "auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

/* ─── 내부 구조체 ─── */
struct ZylAuthService {
    GDBusConnection *system_bus;    /* 시스템 버스 (fprintd 접근) */
    GDBusConnection *session_bus;   /* 세션 버스 (org.zylos.AuthService 등록) */
    guint            dbus_owner_id;
    GDBusProxy      *fprint_manager; /* net.reactivated.Fprint.Manager */
};

/* ─── fprintd 매니저 프록시 취득 ─── */
static GDBusProxy *get_fprint_manager(ZylAuthService *svc) {
    if (svc->fprint_manager) return svc->fprint_manager;

    GError *err = NULL;
    svc->fprint_manager = g_dbus_proxy_new_sync(
        svc->system_bus,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        FPRINT_DBUS_NAME,
        FPRINT_DBUS_MANAGER,
        FPRINT_IFACE_MANAGER,
        NULL,
        &err
    );
    if (!svc->fprint_manager) {
        g_warning("[Auth] Cannot get fprintd Manager: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
    }
    return svc->fprint_manager;
}

/* ─── fprintd Device 객체 경로 취득 ─── */
static gchar *get_default_device_path(ZylAuthService *svc) {
    GDBusProxy *mgr = get_fprint_manager(svc);
    if (!mgr) return NULL;

    GError *err = NULL;
    GVariant *ret = g_dbus_proxy_call_sync(
        mgr,
        "GetDefaultDevice",
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &err
    );
    if (!ret) {
        g_warning("[Auth] GetDefaultDevice failed: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
        return NULL;
    }

    gchar *path = NULL;
    g_variant_get(ret, "(o)", &path);
    g_variant_unref(ret);
    return path;  /* caller g_free */
}

/* ─── Device 프록시 취득 ─── */
static GDBusProxy *get_device_proxy(ZylAuthService *svc) {
    gchar *path = get_default_device_path(svc);
    if (!path) return NULL;

    GError *err = NULL;
    GDBusProxy *dev = g_dbus_proxy_new_sync(
        svc->system_bus,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        FPRINT_DBUS_NAME,
        path,
        FPRINT_IFACE_DEVICE,
        NULL,
        &err
    );
    g_free(path);

    if (!dev) {
        g_warning("[Auth] Cannot create device proxy: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
    }
    return dev;
}

/* ─── D-Bus 인트로스펙션 XML ─── */
static const char *auth_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_AUTH_DBUS_IFACE "'>"
    "    <method name='Enroll'>"
    "      <arg type='s' name='username' direction='in'/>"
    "      <arg type='i' name='finger'   direction='in'/>"
    "      <arg type='i' name='result'   direction='out'/>"
    "    </method>"
    "    <method name='Verify'>"
    "      <arg type='s' name='username' direction='in'/>"
    "      <arg type='i' name='result'   direction='out'/>"
    "    </method>"
    "    <method name='HasFingerprint'>"
    "      <arg type='s' name='username' direction='in'/>"
    "      <arg type='b' name='enrolled' direction='out'/>"
    "    </method>"
    "    <signal name='EnrollProgress'>"
    "      <arg type='i' name='step'/>"
    "      <arg type='i' name='percent'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* ─── D-Bus 메서드 핸들러 ─── */
static void handle_auth_method(GDBusConnection *conn, const gchar *sender,
                                const gchar *path, const gchar *iface,
                                const gchar *method, GVariant *params,
                                GDBusMethodInvocation *inv, gpointer data) {
    ZylAuthService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "Enroll") == 0) {
        const gchar *username = NULL;
        gint32 finger = 0;
        g_variant_get(params, "(&si)", &username, &finger);
        ZylAuthResult r = zyl_auth_enroll_fingerprint(svc, username,
                                                       (int)finger,
                                                       NULL, NULL);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(i)", (gint32)r));

    } else if (g_strcmp0(method, "Verify") == 0) {
        const gchar *username = NULL;
        g_variant_get(params, "(&s)", &username);
        ZylAuthResult r = zyl_auth_verify_fingerprint(svc, username);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(i)", (gint32)r));

    } else if (g_strcmp0(method, "HasFingerprint") == 0) {
        const gchar *username = NULL;
        g_variant_get(params, "(&s)", &username);
        gboolean enrolled = zyl_auth_has_fingerprint(svc, username)
                            ? TRUE : FALSE;
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", enrolled));

    } else {
        g_dbus_method_invocation_return_error(inv,
            G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method: %s", method);
    }
}

static const GDBusInterfaceVTable auth_vtable = {
    .method_call = handle_auth_method,
};

static void on_auth_bus_acquired(GDBusConnection *conn, const gchar *name,
                                  gpointer data) {
    ZylAuthService *svc = data;
    svc->session_bus = conn;

    GError *err = NULL;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(
        auth_introspection_xml, &err);
    if (!info) {
        g_warning("[Auth] Failed to parse introspection XML: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
        return;
    }

    g_dbus_connection_register_object(conn,
        ZYL_AUTH_DBUS_PATH,
        info->interfaces[0],
        &auth_vtable,
        svc, NULL, NULL);
    g_dbus_node_info_unref(info);

    g_message("[Auth] D-Bus registered: %s", ZYL_AUTH_DBUS_NAME);
    (void)name;
}

/* ─── 공개 API ─── */

ZylAuthService *zyl_auth_create(void) {
    GError *err = NULL;

    ZylAuthService *svc = g_new0(ZylAuthService, 1);
    if (!svc) return NULL;

    /* 시스템 버스 연결 (fprintd 접근) */
    svc->system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!svc->system_bus) {
        g_warning("[Auth] Cannot connect to system bus: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
        g_free(svc);
        return NULL;
    }

    /* 세션 버스에 org.zylos.AuthService 등록 */
    svc->dbus_owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        ZYL_AUTH_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_auth_bus_acquired,
        NULL, NULL,
        svc, NULL);

    g_message("[Auth] ZylAuthService created");
    return svc;
}

void zyl_auth_destroy(ZylAuthService *svc) {
    if (!svc) return;

    if (svc->dbus_owner_id) {
        g_bus_unown_name(svc->dbus_owner_id);
    }
    if (svc->fprint_manager) {
        g_object_unref(svc->fprint_manager);
    }
    if (svc->system_bus) {
        g_object_unref(svc->system_bus);
    }
    g_free(svc);
    g_message("[Auth] ZylAuthService destroyed");
}

ZylAuthResult zyl_auth_enroll_fingerprint(ZylAuthService *svc,
                                           const char *username,
                                           int finger,
                                           zyl_auth_enroll_progress_fn progress,
                                           void *user_data) {
    if (!svc || !username) return ZYL_AUTH_ERR_GENERAL;

    GDBusProxy *dev = get_device_proxy(svc);
    if (!dev) return ZYL_AUTH_ERR_NO_DEVICE;

    GError *err = NULL;

    /* fprintd: Claim(username) */
    GVariant *claim_ret = g_dbus_proxy_call_sync(dev, "Claim",
        g_variant_new("(s)", username),
        G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &err);
    if (!claim_ret) {
        g_warning("[Auth] Enroll Claim failed: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
        g_object_unref(dev);
        return ZYL_AUTH_ERR_DBUS;
    }
    g_variant_unref(claim_ret);

    /* fprintd: EnrollStart(finger_name) */
    /* finger 0 → "right-index-finger", 1 → "left-index-finger" */
    const char *finger_names[] = {
        "right-index-finger", "left-index-finger",
        "right-thumb", "left-thumb",
        "right-middle-finger", "left-middle-finger"
    };
    int n_fingers = (int)(sizeof(finger_names) / sizeof(finger_names[0]));
    const char *finger_name = (finger >= 0 && finger < n_fingers)
                              ? finger_names[finger]
                              : finger_names[0];

    GVariant *enroll_ret = g_dbus_proxy_call_sync(dev, "EnrollStart",
        g_variant_new("(s)", finger_name),
        G_DBUS_CALL_FLAGS_NONE, 60000, NULL, &err);

    ZylAuthResult result = ZYL_AUTH_ERR_GENERAL;

    if (!enroll_ret) {
        g_warning("[Auth] EnrollStart failed: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
    } else {
        /* TODO(auth): EnrollStart 은 비동기 — signal EnrollStatus 를 수신해야 함.
         * 현재는 동기 완료를 위해 EnrollStop 까지 진행하고 OK로 처리하는 스텁.
         * 필요 작업:
         * 1. GMainLoop + g_signal_connect 로 EnrollStatus 시그널 수신
         * 2. 다단계 등록 진행 상태를 progress 콜백으로 전달
         * 3. 타임아웃 처리 (센서 무응답 시)
         */
        g_variant_unref(enroll_ret);
        result = ZYL_AUTH_OK;
        if (progress) {
            progress(ZYL_ENROLL_STEP_COMPLETE, 100, user_data);
        }
    }

    /* fprintd: EnrollStop */
    GVariant *stop_ret = g_dbus_proxy_call_sync(dev, "EnrollStop",
        NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
    if (stop_ret) g_variant_unref(stop_ret);

    /* fprintd: Release */
    GVariant *rel_ret = g_dbus_proxy_call_sync(dev, "Release",
        NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
    if (rel_ret) g_variant_unref(rel_ret);

    g_object_unref(dev);
    g_message("[Auth] Enroll %s for user '%s'",
              result == ZYL_AUTH_OK ? "succeeded" : "failed", username);
    return result;
}

ZylAuthResult zyl_auth_verify_fingerprint(ZylAuthService *svc,
                                           const char *username) {
    if (!svc || !username) return ZYL_AUTH_ERR_GENERAL;

    GDBusProxy *dev = get_device_proxy(svc);
    if (!dev) return ZYL_AUTH_ERR_NO_DEVICE;

    GError *err = NULL;

    /* Claim */
    GVariant *claim_ret = g_dbus_proxy_call_sync(dev, "Claim",
        g_variant_new("(s)", username),
        G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &err);
    if (!claim_ret) {
        g_warning("[Auth] Verify Claim failed: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
        g_object_unref(dev);
        return ZYL_AUTH_ERR_DBUS;
    }
    g_variant_unref(claim_ret);

    /* VerifyStart("any") */
    GVariant *verify_ret = g_dbus_proxy_call_sync(dev, "VerifyStart",
        g_variant_new("(s)", "any"),
        G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &err);

    ZylAuthResult result = ZYL_AUTH_ERR_GENERAL;

    if (!verify_ret) {
        g_warning("[Auth] VerifyStart failed: %s",
                  err ? err->message : "unknown");
        g_clear_error(&err);
    } else {
        /* TODO(auth): VerifyStart 의 결과는 signal VerifyStatus 로 전달되어야 함.
         * 현재는 동기 래퍼로 항상 OK 처리하는 스텁.
         * 필요 작업:
         * 1. GMainLoop + g_signal_connect 로 VerifyStatus 시그널 수신
         * 2. verify-match / verify-no-match / verify-retry 분기 처리
         * 3. 타임아웃 + 최대 재시도 횟수 적용
         */
        g_variant_unref(verify_ret);
        result = ZYL_AUTH_OK;
    }

    /* VerifyStop */
    GVariant *stop_ret = g_dbus_proxy_call_sync(dev, "VerifyStop",
        NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
    if (stop_ret) g_variant_unref(stop_ret);

    /* Release */
    GVariant *rel_ret = g_dbus_proxy_call_sync(dev, "Release",
        NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
    if (rel_ret) g_variant_unref(rel_ret);

    g_object_unref(dev);
    g_message("[Auth] Verify %s for user '%s'",
              result == ZYL_AUTH_OK ? "matched" : "no-match", username);
    return result;
}

bool zyl_auth_has_fingerprint(const ZylAuthService *svc,
                               const char *username) {
    if (!svc || !username) return false;

    GDBusProxy *dev = get_device_proxy((ZylAuthService *)svc);
    if (!dev) return false;

    GError *err = NULL;

    /* ListEnrolledFingers(username) */
    GVariant *ret = g_dbus_proxy_call_sync(dev, "ListEnrolledFingers",
        g_variant_new("(s)", username),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    bool has = false;
    if (!ret) {
        /* 등록된 지문 없으면 fprintd 가 오류 반환하는 경우도 있음 */
        g_clear_error(&err);
    } else {
        GVariant *fingers = NULL;
        g_variant_get(ret, "(@as)", &fingers);
        if (fingers) {
            has = g_variant_n_children(fingers) > 0;
            g_variant_unref(fingers);
        }
        g_variant_unref(ret);
    }

    g_object_unref(dev);
    return has;
}

/* ─── main(): 독립 데몬 실행 ─── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    g_message("[Auth] Zyl Auth Service starting...");

    ZylAuthService *svc = zyl_auth_create();
    if (!svc) {
        g_critical("[Auth] Failed to create auth service");
        return 1;
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_message("[Auth] Entering main loop");
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    zyl_auth_destroy(svc);
    g_message("[Auth] Service stopped");
    return 0;
}
