/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 전화 서비스 — ModemManager 연동, 음성통화, SMS, 네트워크 상태
 * 수행범위: ModemManager D-Bus 프록시, 모뎀 열거, SIM/신호 상태, 통화 제어
 * 의존방향: telephony.h, gio/gio.h, ModemManager D-Bus API
 * SOLID: SRP — 전화/메시징 상태 관리만 담당
 * ────────────────────────────────────────────────────────── */

#include "telephony.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <gio/gio.h>

/* ─── 내부 상수 ─── */
#define MAX_MODEM_PATH  256
#define DBUS_PROP_IFACE "org.freedesktop.DBus.Properties"

/* ─── 내부 구조체 ─── */
struct ZylTelephonyService {
    /* 현재 상태 */
    ZylTelephonyState state;
    ZylCallState call_state;
    char active_call_number[20];
    char modem_path[MAX_MODEM_PATH]; /* /org/freedesktop/ModemManager1/Modem/0 */
    bool modem_found;

    /* 콜백 */
    zyl_call_state_fn call_cb;
    void *call_cb_data;
    zyl_sms_received_fn sms_cb;
    void *sms_cb_data;

    /* D-Bus */
    GDBusConnection *system_bus;     /* ModemManager는 system bus */
    GDBusConnection *session_bus;    /* Zyl 서비스는 session bus */
    guint dbus_owner_id;
    guint mm_signal_sub;             /* ModemManager 시그널 구독 */
    guint mm_sms_sub;                /* SMS 시그널 구독 */
};

/* ─── 현재 시각 밀리초 ─── */
static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* ─── 통화 상태 전환 ─── */
static void transition_call_state(ZylTelephonyService *svc,
                                   ZylCallState new_state,
                                   const char *number) {
    if (svc->call_state == new_state) return;
    ZylCallState old = svc->call_state;
    svc->call_state = new_state;

    if (number) {
        snprintf(svc->active_call_number, sizeof(svc->active_call_number),
                 "%s", number);
    }

    g_message("[Telephony] Call: %d -> %d (%s)", old, new_state,
              number ? number : "");

    if (svc->call_cb) {
        svc->call_cb(old, new_state, svc->active_call_number,
                     svc->call_cb_data);
    }

    /* D-Bus 시그널 */
    if (svc->session_bus) {
        g_dbus_connection_emit_signal(svc->session_bus, NULL,
            ZYL_TELEPHONY_DBUS_PATH,
            ZYL_TELEPHONY_DBUS_NAME,
            "CallStateChanged",
            g_variant_new("(iis)", (gint32)old, (gint32)new_state,
                           svc->active_call_number),
            NULL);
    }
}

/* ─── D-Bus 프로퍼티 읽기 유틸 ─── */
static GVariant *get_dbus_property(GDBusConnection *conn,
                                    const char *dest,
                                    const char *path,
                                    const char *iface,
                                    const char *prop) {
    GError *err = NULL;
    GVariant *result = g_dbus_connection_call_sync(conn, dest, path,
        DBUS_PROP_IFACE, "Get",
        g_variant_new("(ss)", iface, prop),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &err);

    if (err) {
        g_clear_error(&err);
        return NULL;
    }

    GVariant *val = NULL;
    g_variant_get(result, "(v)", &val);
    g_variant_unref(result);
    return val;
}

/* ─── 모뎀 탐색 ─── */
static bool find_first_modem(ZylTelephonyService *svc) {
    GError *err = NULL;
    GVariant *result = g_dbus_connection_call_sync(svc->system_bus,
        MM_DBUS_NAME, MM_DBUS_PATH,
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects",
        NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (err) {
        g_message("[Telephony] ModemManager not available: %s", err->message);
        g_clear_error(&err);
        return false;
    }

    GVariantIter *obj_iter;
    g_variant_get(result, "(a{oa{sa{sv}}})", &obj_iter);

    const gchar *obj_path;
    GVariant *ifaces;
    bool found = false;

    while (g_variant_iter_next(obj_iter, "{&o@a{sa{sv}}}", &obj_path, &ifaces)) {
        /* 첫 번째 모뎀 사용 */
        snprintf(svc->modem_path, sizeof(svc->modem_path), "%s", obj_path);
        found = true;
        g_variant_unref(ifaces);
        break;
    }
    g_variant_iter_free(obj_iter);
    g_variant_unref(result);

    if (found) {
        g_message("[Telephony] Found modem: %s", svc->modem_path);
    }
    return found;
}

/* ─── 모뎀 상태 읽기 ─── */
static void read_modem_state(ZylTelephonyService *svc) {
    if (!svc->modem_found) return;

    /* SIM 상태 */
    GVariant *sim_path = get_dbus_property(svc->system_bus,
        MM_DBUS_NAME, svc->modem_path, MM_MODEM_IFACE, "Sim");
    if (sim_path) {
        const char *sp = g_variant_get_string(sim_path, NULL);
        svc->state.sim_present = (sp && strlen(sp) > 1 && strcmp(sp, "/") != 0);
        g_variant_unref(sim_path);
    }

    /* 통신사 이름 */
    GVariant *op_name = get_dbus_property(svc->system_bus,
        MM_DBUS_NAME, svc->modem_path,
        "org.freedesktop.ModemManager1.Modem.Modem3gpp",
        "OperatorName");
    if (op_name) {
        const char *name = g_variant_get_string(op_name, NULL);
        snprintf(svc->state.operator_name, sizeof(svc->state.operator_name),
                 "%s", name ? name : "");
        g_variant_unref(op_name);
    }

    /* 신호 강도 (SignalQuality -> (u, b)) */
    GVariant *sig_qual = get_dbus_property(svc->system_bus,
        MM_DBUS_NAME, svc->modem_path, MM_MODEM_IFACE, "SignalQuality");
    if (sig_qual) {
        guint32 quality = 0;
        gboolean recent = FALSE;
        g_variant_get(sig_qual, "(ub)", &quality, &recent);
        /* 0-100 -> 0-4 bars */
        svc->state.signal_strength = (int)(quality / 25);
        if (svc->state.signal_strength > 4) svc->state.signal_strength = 4;
        g_variant_unref(sig_qual);
    }

    /* IMEI (EquipmentIdentifier) */
    GVariant *imei = get_dbus_property(svc->system_bus,
        MM_DBUS_NAME, svc->modem_path, MM_MODEM_IFACE,
        "EquipmentIdentifier");
    if (imei) {
        const char *s = g_variant_get_string(imei, NULL);
        snprintf(svc->state.imei, sizeof(svc->state.imei),
                 "%s", s ? s : "");
        g_variant_unref(imei);
    }

    /* 접속 기술 (AccessTechnologies bitmask) */
    GVariant *tech = get_dbus_property(svc->system_bus,
        MM_DBUS_NAME, svc->modem_path, MM_MODEM_IFACE,
        "AccessTechnologies");
    if (tech) {
        guint32 t = g_variant_get_uint32(tech);
        /*
         * MM_MODEM_ACCESS_TECHNOLOGY:
         *   GSM/GPRS/EDGE = 2G (bits 1-3)
         *   UMTS/HSPA = 3G (bits 5-8)
         *   LTE = 4G (bit 14)
         *   5GNR (bit 15)
         */
        if (t & (1 << 15))      svc->state.network_type = ZYL_NETWORK_TYPE_5G;
        else if (t & (1 << 14)) svc->state.network_type = ZYL_NETWORK_TYPE_4G;
        else if (t & 0x1E0)     svc->state.network_type = ZYL_NETWORK_TYPE_3G;
        else if (t & 0x0E)      svc->state.network_type = ZYL_NETWORK_TYPE_2G;
        else                    svc->state.network_type = ZYL_NETWORK_TYPE_NONE;
        g_variant_unref(tech);
    }

    /* 전화번호: SIM OwnNumbers */
    if (svc->state.sim_present) {
        GVariant *sim_obj = get_dbus_property(svc->system_bus,
            MM_DBUS_NAME, svc->modem_path, MM_MODEM_IFACE, "Sim");
        if (sim_obj) {
            const char *sim_path_str = g_variant_get_string(sim_obj, NULL);
            if (sim_path_str && strlen(sim_path_str) > 1) {
                GVariant *numbers = get_dbus_property(svc->system_bus,
                    MM_DBUS_NAME, sim_path_str, MM_SIM_IFACE,
                    "EmergencyNumbers");
                /* OwnNumbers 프로퍼티 대체 시도 */
                (void)numbers;
            }
            g_variant_unref(sim_obj);
        }
    }
}

/* ─── ModemManager 시그널: 통화 상태 변경 ─── */
static void on_mm_call_state_changed(GDBusConnection *conn,
                                      const gchar *sender,
                                      const gchar *object_path,
                                      const gchar *interface_name,
                                      const gchar *signal_name,
                                      GVariant *parameters,
                                      gpointer data) {
    ZylTelephonyService *svc = data;
    (void)conn; (void)sender; (void)object_path; (void)interface_name;

    if (g_strcmp0(signal_name, "StateChanged") != 0) return;

    gint32 old_state, new_state;
    guint32 reason;
    g_variant_get(parameters, "(iiu)", &old_state, &new_state, &reason);

    /*
     * MM_CALL_STATE:
     *   0=Unknown, 1=Dialing, 2=RingingOut, 3=RingingIn,
     *   4=Active, 5=Held, 6=Waiting, 7=Terminated
     */
    ZylCallState zyl_state;
    switch (new_state) {
    case 1: case 2: zyl_state = ZYL_CALL_STATE_DIALING; break;
    case 3:         zyl_state = ZYL_CALL_STATE_RINGING; break;
    case 4:         zyl_state = ZYL_CALL_STATE_ACTIVE; break;
    case 5: case 6: zyl_state = ZYL_CALL_STATE_HELD; break;
    default:        zyl_state = ZYL_CALL_STATE_IDLE; break;
    }

    transition_call_state(svc, zyl_state, NULL);
}

/* ─── ModemManager 시그널: SMS 수신 ─── */
static void on_mm_sms_added(GDBusConnection *conn,
                              const gchar *sender,
                              const gchar *object_path,
                              const gchar *interface_name,
                              const gchar *signal_name,
                              GVariant *parameters,
                              gpointer data) {
    ZylTelephonyService *svc = data;
    (void)conn; (void)sender; (void)object_path; (void)interface_name;

    if (g_strcmp0(signal_name, "Added") != 0) return;

    const gchar *sms_path;
    gboolean received;
    g_variant_get(parameters, "(&ob)", &sms_path, &received);

    if (!received) return; /* 발신 SMS 무시 */

    /* SMS 내용 읽기 */
    GVariant *num_v = get_dbus_property(svc->system_bus,
        MM_DBUS_NAME, sms_path,
        "org.freedesktop.ModemManager1.Sms", "Number");
    GVariant *text_v = get_dbus_property(svc->system_bus,
        MM_DBUS_NAME, sms_path,
        "org.freedesktop.ModemManager1.Sms", "Text");

    const char *from = num_v ? g_variant_get_string(num_v, NULL) : "unknown";
    const char *body = text_v ? g_variant_get_string(text_v, NULL) : "";

    g_message("[Telephony] SMS received from %s: %.40s...", from, body);

    /* 콜백 */
    if (svc->sms_cb) {
        svc->sms_cb(from, body, now_ms(), svc->sms_cb_data);
    }

    /* D-Bus 시그널 */
    if (svc->session_bus) {
        g_dbus_connection_emit_signal(svc->session_bus, NULL,
            ZYL_TELEPHONY_DBUS_PATH,
            ZYL_TELEPHONY_DBUS_NAME,
            "SmsReceived",
            g_variant_new("(sst)", from, body, (guint64)now_ms()),
            NULL);
    }

    if (num_v) g_variant_unref(num_v);
    if (text_v) g_variant_unref(text_v);
}

/* ─── ModemManager 시그널 구독 ─── */
static void subscribe_mm_signals(ZylTelephonyService *svc) {
    if (!svc->system_bus || !svc->modem_found) return;

    /* 통화 상태 변경 */
    svc->mm_signal_sub = g_dbus_connection_signal_subscribe(svc->system_bus,
        MM_DBUS_NAME, "org.freedesktop.ModemManager1.Call",
        "StateChanged", NULL, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_mm_call_state_changed, svc, NULL);

    /* SMS 수신 */
    svc->mm_sms_sub = g_dbus_connection_signal_subscribe(svc->system_bus,
        MM_DBUS_NAME, MM_MSG_IFACE,
        "Added", svc->modem_path, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_mm_sms_added, svc, NULL);

    g_message("[Telephony] Subscribed to ModemManager signals");
}

/* ─── Zyl D-Bus 인트로스펙션 ─── */
static const char *telephony_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_TELEPHONY_DBUS_NAME "'>"
    "    <method name='GetState'>"
    "      <arg type='b' name='sim_present' direction='out'/>"
    "      <arg type='s' name='operator_name' direction='out'/>"
    "      <arg type='i' name='network_type' direction='out'/>"
    "      <arg type='i' name='signal_strength' direction='out'/>"
    "      <arg type='s' name='imei' direction='out'/>"
    "      <arg type='s' name='phone_number' direction='out'/>"
    "    </method>"
    "    <method name='GetCallState'>"
    "      <arg type='i' name='call_state' direction='out'/>"
    "      <arg type='s' name='number' direction='out'/>"
    "    </method>"
    "    <method name='Dial'>"
    "      <arg type='s' name='number' direction='in'/>"
    "    </method>"
    "    <method name='Answer'/>"
    "    <method name='Hangup'/>"
    "    <method name='SendSms'>"
    "      <arg type='s' name='number' direction='in'/>"
    "      <arg type='s' name='body' direction='in'/>"
    "    </method>"
    "    <signal name='CallStateChanged'>"
    "      <arg type='i' name='old_state'/>"
    "      <arg type='i' name='new_state'/>"
    "      <arg type='s' name='number'/>"
    "    </signal>"
    "    <signal name='SmsReceived'>"
    "      <arg type='s' name='sender'/>"
    "      <arg type='s' name='body'/>"
    "      <arg type='t' name='timestamp_ms'/>"
    "    </signal>"
    "    <signal name='SignalStrengthChanged'>"
    "      <arg type='i' name='bars'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* ─── Zyl D-Bus 메서드 핸들러 ─── */
static void handle_telephony_method(GDBusConnection *conn, const gchar *sender,
                                     const gchar *path, const gchar *iface,
                                     const gchar *method, GVariant *params,
                                     GDBusMethodInvocation *inv, gpointer data) {
    ZylTelephonyService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "GetState") == 0) {
        read_modem_state(svc);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(bsiiss)",
                           svc->state.sim_present,
                           svc->state.operator_name,
                           (gint32)svc->state.network_type,
                           (gint32)svc->state.signal_strength,
                           svc->state.imei,
                           svc->state.phone_number));
    } else if (g_strcmp0(method, "GetCallState") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(is)", (gint32)svc->call_state,
                           svc->active_call_number));
    } else if (g_strcmp0(method, "Dial") == 0) {
        const gchar *number;
        g_variant_get(params, "(&s)", &number);
        int r = zyl_telephony_dial(svc, number);
        if (r == 0) {
            g_dbus_method_invocation_return_value(inv, NULL);
        } else {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                G_DBUS_ERROR_FAILED, "Dial failed");
        }
    } else if (g_strcmp0(method, "Answer") == 0) {
        zyl_telephony_answer(svc);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "Hangup") == 0) {
        zyl_telephony_hangup(svc);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "SendSms") == 0) {
        const gchar *number, *body;
        g_variant_get(params, "(&s&s)", &number, &body);
        int r = zyl_telephony_send_sms(svc, number, body);
        if (r == 0) {
            g_dbus_method_invocation_return_value(inv, NULL);
        } else {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                G_DBUS_ERROR_FAILED, "SMS send failed");
        }
    }
}

static const GDBusInterfaceVTable telephony_vtable = {
    .method_call = handle_telephony_method,
};

static void on_telephony_bus_acquired(GDBusConnection *conn, const gchar *name,
                                       gpointer data) {
    ZylTelephonyService *svc = data;
    svc->session_bus = conn;

    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(telephony_introspection_xml, NULL);
    g_dbus_connection_register_object(conn, ZYL_TELEPHONY_DBUS_PATH,
        info->interfaces[0], &telephony_vtable, svc, NULL, NULL);
    g_dbus_node_info_unref(info);
    g_message("[Telephony] D-Bus registered: %s", ZYL_TELEPHONY_DBUS_NAME);
}

/* ─── 공개 API ─── */

ZylTelephonyService *zyl_telephony_create(void) {
    ZylTelephonyService *svc = g_new0(ZylTelephonyService, 1);
    svc->call_state = ZYL_CALL_STATE_IDLE;

    /* system bus 연결 (ModemManager) */
    GError *err = NULL;
    svc->system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (err) {
        g_warning("[Telephony] Cannot connect to system bus: %s", err->message);
        g_clear_error(&err);
    }

    /* 모뎀 탐색 */
    if (svc->system_bus) {
        svc->modem_found = find_first_modem(svc);
        if (svc->modem_found) {
            read_modem_state(svc);
            subscribe_mm_signals(svc);
        } else {
            g_message("[Telephony] No modem found — sim_present=false");
            svc->state.sim_present = false;
        }
    }

    /* Zyl D-Bus (session bus) 등록 */
    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
        ZYL_TELEPHONY_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_telephony_bus_acquired, NULL, NULL, svc, NULL);

    return svc;
}

void zyl_telephony_destroy(ZylTelephonyService *svc) {
    if (!svc) return;

    if (svc->system_bus) {
        if (svc->mm_signal_sub)
            g_dbus_connection_signal_unsubscribe(svc->system_bus, svc->mm_signal_sub);
        if (svc->mm_sms_sub)
            g_dbus_connection_signal_unsubscribe(svc->system_bus, svc->mm_sms_sub);
        g_object_unref(svc->system_bus);
    }

    g_bus_unown_name(svc->dbus_owner_id);
    g_free(svc);
}

int zyl_telephony_get_state(const ZylTelephonyService *svc,
                             ZylTelephonyState *out) {
    if (!svc || !out) return -1;
    *out = svc->state;
    return 0;
}

ZylCallState zyl_telephony_get_call_state(const ZylTelephonyService *svc) {
    return svc ? svc->call_state : ZYL_CALL_STATE_IDLE;
}

int zyl_telephony_dial(ZylTelephonyService *svc, const char *number) {
    if (!svc || !number) return -1;
    if (!svc->modem_found || !svc->system_bus) {
        g_warning("[Telephony] No modem — cannot dial");
        return -1;
    }

    g_message("[Telephony] Dialing: %s", number);
    transition_call_state(svc, ZYL_CALL_STATE_DIALING, number);

    /* ModemManager Voice.CreateCall */
    GError *err = NULL;
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&builder, "{sv}", "number",
                           g_variant_new_string(number));

    result = g_dbus_connection_call_sync(svc->system_bus,
        MM_DBUS_NAME, svc->modem_path,
        MM_VOICE_IFACE, "CreateCall",
        g_variant_new("(a{sv})", &builder),
        G_VARIANT_TYPE("(o)"),
        G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &err);

    if (err) {
        g_warning("[Telephony] CreateCall failed: %s", err->message);
        g_clear_error(&err);
        transition_call_state(svc, ZYL_CALL_STATE_IDLE, number);
        return -1;
    }

    /* 생성된 통화 오브젝트에서 Start 호출 */
    const gchar *call_path;
    g_variant_get(result, "(&o)", &call_path);

    GVariant *start_result = g_dbus_connection_call_sync(svc->system_bus,
        MM_DBUS_NAME, call_path,
        "org.freedesktop.ModemManager1.Call", "Start",
        NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &err);

    if (err) {
        g_warning("[Telephony] Call Start failed: %s", err->message);
        g_clear_error(&err);
        transition_call_state(svc, ZYL_CALL_STATE_IDLE, number);
    }

    if (start_result) g_variant_unref(start_result);
    g_variant_unref(result);
    return err ? -1 : 0;
}

int zyl_telephony_answer(ZylTelephonyService *svc) {
    if (!svc || !svc->modem_found || !svc->system_bus) return -1;
    if (svc->call_state != ZYL_CALL_STATE_RINGING) return -1;

    g_message("[Telephony] Answering call");

    /* ListCalls로 현재 통화 목록 조회 후 Accept */
    GError *err = NULL;
    GVariant *result = g_dbus_connection_call_sync(svc->system_bus,
        MM_DBUS_NAME, svc->modem_path,
        MM_VOICE_IFACE, "ListCalls",
        NULL, G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (err) {
        g_warning("[Telephony] ListCalls failed: %s", err->message);
        g_clear_error(&err);
        return -1;
    }

    GVariantIter *iter;
    g_variant_get(result, "(ao)", &iter);
    const gchar *call_path;
    while (g_variant_iter_next(iter, "&o", &call_path)) {
        g_dbus_connection_call_sync(svc->system_bus,
            MM_DBUS_NAME, call_path,
            "org.freedesktop.ModemManager1.Call", "Accept",
            NULL, NULL,
            G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
        break; /* 첫 번째 통화만 응답 */
    }
    g_variant_iter_free(iter);
    g_variant_unref(result);

    transition_call_state(svc, ZYL_CALL_STATE_ACTIVE, NULL);
    return 0;
}

int zyl_telephony_hangup(ZylTelephonyService *svc) {
    if (!svc || !svc->modem_found || !svc->system_bus) return -1;
    if (svc->call_state == ZYL_CALL_STATE_IDLE) return 0;

    g_message("[Telephony] Hanging up");

    /* HangupAll 호출 */
    GError *err = NULL;
    g_dbus_connection_call_sync(svc->system_bus,
        MM_DBUS_NAME, svc->modem_path,
        MM_VOICE_IFACE, "HangupAll",
        NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (err) {
        g_warning("[Telephony] HangupAll failed: %s", err->message);
        g_clear_error(&err);
        /* ListCalls + 개별 Hangup 폴백 */
        GVariant *result = g_dbus_connection_call_sync(svc->system_bus,
            MM_DBUS_NAME, svc->modem_path,
            MM_VOICE_IFACE, "ListCalls",
            NULL, G_VARIANT_TYPE("(ao)"),
            G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
        if (result) {
            GVariantIter *iter;
            g_variant_get(result, "(ao)", &iter);
            const gchar *call_path;
            while (g_variant_iter_next(iter, "&o", &call_path)) {
                g_dbus_connection_call_sync(svc->system_bus,
                    MM_DBUS_NAME, call_path,
                    "org.freedesktop.ModemManager1.Call", "Hangup",
                    NULL, NULL,
                    G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
            }
            g_variant_iter_free(iter);
            g_variant_unref(result);
        }
    }

    transition_call_state(svc, ZYL_CALL_STATE_IDLE, NULL);
    return 0;
}

int zyl_telephony_send_sms(ZylTelephonyService *svc,
                            const char *number, const char *body) {
    if (!svc || !number || !body) return -1;
    if (!svc->modem_found || !svc->system_bus) {
        g_warning("[Telephony] No modem — cannot send SMS");
        return -1;
    }

    g_message("[Telephony] Sending SMS to %s (len=%zu)", number, strlen(body));

    /* Messaging.Create -> SMS 오브젝트 생성 */
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&builder, "{sv}", "number",
                           g_variant_new_string(number));
    g_variant_builder_add(&builder, "{sv}", "text",
                           g_variant_new_string(body));

    GError *err = NULL;
    GVariant *result = g_dbus_connection_call_sync(svc->system_bus,
        MM_DBUS_NAME, svc->modem_path,
        MM_MSG_IFACE, "Create",
        g_variant_new("(a{sv})", &builder),
        G_VARIANT_TYPE("(o)"),
        G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &err);

    if (err) {
        g_warning("[Telephony] SMS Create failed: %s", err->message);
        g_clear_error(&err);
        return -1;
    }

    /* SMS.Send 호출 */
    const gchar *sms_path;
    g_variant_get(result, "(&o)", &sms_path);

    g_dbus_connection_call_sync(svc->system_bus,
        MM_DBUS_NAME, sms_path,
        "org.freedesktop.ModemManager1.Sms", "Send",
        NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &err);

    g_variant_unref(result);

    if (err) {
        g_warning("[Telephony] SMS Send failed: %s", err->message);
        g_clear_error(&err);
        return -1;
    }

    g_message("[Telephony] SMS sent successfully");
    return 0;
}

void zyl_telephony_on_call_state(ZylTelephonyService *svc,
                                  zyl_call_state_fn cb, void *data) {
    if (!svc) return;
    svc->call_cb = cb;
    svc->call_cb_data = data;
}

void zyl_telephony_on_sms_received(ZylTelephonyService *svc,
                                    zyl_sms_received_fn cb, void *data) {
    if (!svc) return;
    svc->sms_cb = cb;
    svc->sms_cb_data = data;
}

/* ─── main(): 독립 데몬 실행 ─── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    g_message("[Telephony] Zyl Telephony Service starting...");

    ZylTelephonyService *svc = zyl_telephony_create();
    if (!svc) {
        g_critical("[Telephony] Failed to create telephony service");
        return 1;
    }

    if (svc->state.sim_present) {
        g_message("[Telephony] SIM present — operator: %s, signal: %d/4",
                  svc->state.operator_name, svc->state.signal_strength);
    } else {
        g_message("[Telephony] No SIM card detected");
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_message("[Telephony] Entering main loop");
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    zyl_telephony_destroy(svc);
    g_message("[Telephony] Service stopped");
    return 0;
}
