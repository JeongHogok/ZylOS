/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 전화 서비스 — ModemManager 연동, 음성통화, 통화 기록, SMS, 재난문자, 모바일 데이터
 * 수행범위: ModemManager D-Bus 프록시, 모뎀 열거, SIM/신호 상태, 통화 제어·DTMF,
 *           통화 기록(통화 상태 전이에서 직접 기록 — 부재중 포함, 앱이 쓰지 않음;
 *           판정·보관은 calllog.c),
 *           Cell Broadcast 채널 설정·수신 (분류·기록은 cbs.c),
 *           NetworkManager 모바일 데이터 연결 (APN 자동, IPv4v6, CLAT, 로밍)
 * 의존방향: telephony.h, gio/gio.h, ModemManager D-Bus API
 * SOLID: SRP — 전화/메시징 상태 관리만 담당
 * ────────────────────────────────────────────────────────── */

#include "telephony.h"
#include "cbs.h"
#include "calllog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <gio/gio.h>

/* ─── 내부 상수 ─── */
#define MAX_MODEM_PATH  256
#define DBUS_PROP_IFACE "org.freedesktop.DBus.Properties"
#define MM_CB_IFACE     "org.freedesktop.ModemManager1.Modem.CellBroadcast"   /* MM >= 1.24 */
#define MM_CBM_IFACE    "org.freedesktop.ModemManager1.Cbm"
#define MM_CBM_RECEIVED 2u
#ifndef ZYL_ALERTS_FILE
#define ZYL_ALERTS_FILE "/data/alerts/history"
#endif
#ifndef ZYL_CALLLOG_FILE
#define ZYL_CALLLOG_FILE "/data/telephony/calllog"
#endif
#define MM_CALL_IFACE   "org.freedesktop.ModemManager1.Call"

/* ─── 내부 구조체 ─── */
#define MAX_MODEMS 4  /* Support up to 4 modems / SIM slots */


struct ZylTelephonyService {
    /* 현재 상태 */
    ZylTelephonyState state;
    ZylCallState call_state;
    char active_call_number[20];

    /* Multi-modem / Dual-SIM support */
    char modem_path[MAX_MODEMS][MAX_MODEM_PATH];
    int modem_count;
    int active_modem;              /* Index of currently active modem (0-based) */
    bool modem_found;

    /* 콜백 */

    /* D-Bus */
    GDBusConnection *system_bus;     /* ModemManager는 system bus */
    GDBusConnection *session_bus;    /* Zyl 서비스는 session bus */
    guint dbus_owner_id;
    guint mm_signal_sub;             /* ModemManager 시그널 구독 */
    guint mm_sms_sub;                /* SMS 시그널 구독 */
    guint mm_cbs_sub;                /* Cell Broadcast 수신 */
    guint mm_cbm_props_sub;          /* 여러 조각 CBM 의 수신 완료 */

    ZylCbsStore *cbs;                /* 재난문자 기록 */

    /* The call in progress, for the log */
    bool call_incoming;
    uint64_t call_start_ms, call_answered_ms;
    ZylCallLog *calllog;             /* 통화 기록 */
};

/* ─── 현재 시각 밀리초 ─── */
static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* Same names as apps/system/services/telephony.js uses — one vocabulary for
   the emulator and the device, in replies and in CallStateChanged */
static const char *call_state_name(ZylCallState s) {
    switch (s) {
    case ZYL_CALL_STATE_RINGING: return "RINGING";
    case ZYL_CALL_STATE_ACTIVE:  return "ACTIVE";
    case ZYL_CALL_STATE_HELD:    return "HELD";
    case ZYL_CALL_STATE_DIALING: return "DIALING";
    default:                     return "IDLE";
    }
}

/* ─── 통화 기록 (판정·보관은 calllog.c) ─── */
static GVariant *calllog_variant(const ZylTelephonyService *svc) {
    GVariantBuilder list;
    g_variant_builder_init(&list, G_VARIANT_TYPE("aa{sv}"));
    for (unsigned i = 0; i < zyl_calllog_count(svc->calllog); i++) {   /* newest first */
        const ZylCallLogEntry *e = zyl_calllog_get(svc->calllog, i);
        GVariantBuilder d;
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&d, "{sv}", "id", g_variant_new_uint64(e->id));
        g_variant_builder_add(&d, "{sv}", "time", g_variant_new_int64(e->time_ms));
        g_variant_builder_add(&d, "{sv}", "duration", g_variant_new_uint32(e->duration));
        g_variant_builder_add(&d, "{sv}", "type", g_variant_new_string(e->type));
        g_variant_builder_add(&d, "{sv}", "number", g_variant_new_string(e->number));
        g_variant_builder_add_value(&list, g_variant_builder_end(&d));
    }
    return g_variant_new("(aa{sv})", &list);
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
    } else if (old == ZYL_CALL_STATE_IDLE) {
        svc->active_call_number[0] = '\0';   /* never the previous call's number */
    }
    if (old == ZYL_CALL_STATE_IDLE) {
        svc->call_incoming = new_state == ZYL_CALL_STATE_RINGING;
        svc->call_start_ms = now_ms();
        svc->call_answered_ms = 0;
    }
    if (new_state == ZYL_CALL_STATE_ACTIVE && !svc->call_answered_ms)
        svc->call_answered_ms = now_ms();
    if (new_state == ZYL_CALL_STATE_IDLE)
        zyl_calllog_record(svc->calllog, svc->active_call_number, svc->call_incoming,
                           (int64_t)svc->call_start_ms, (int64_t)svc->call_answered_ms,
                           (int64_t)now_ms());

    g_message("[Telephony] Call: %d -> %d (%s)", old, new_state,
              number ? number : "");

    /* D-Bus 시그널 */
    if (svc->session_bus) {
        g_dbus_connection_emit_signal(svc->session_bus, NULL,
            ZYL_TELEPHONY_DBUS_PATH,
            ZYL_TELEPHONY_DBUS_NAME,
            "CallStateChanged",
            g_variant_new("(sss)", call_state_name(old), call_state_name(new_state),
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

/* ─── 모뎀 탐색 (dual-SIM: 최대 MAX_MODEMS 개 모뎀 열거) ─── */
static bool find_modems(ZylTelephonyService *svc) {
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
    svc->modem_count = 0;

    while (g_variant_iter_next(obj_iter, "{&o@a{sa{sv}}}", &obj_path, &ifaces)) {
        /* Check if this object has the Modem interface */
        bool is_modem = g_variant_lookup(ifaces, MM_MODEM_IFACE,
                                          "a{sv}", NULL);
        if (is_modem && svc->modem_count < MAX_MODEMS) {
            snprintf(svc->modem_path[svc->modem_count],
                     sizeof(svc->modem_path[svc->modem_count]),
                     "%s", obj_path);
            svc->modem_count++;
            g_message("[Telephony] Found modem[%d]: %s",
                      svc->modem_count - 1, obj_path);
        }
        g_variant_unref(ifaces);
    }
    g_variant_iter_free(obj_iter);
    g_variant_unref(result);

    if (svc->modem_count > 0) {
        svc->active_modem = 0;
        /* Legacy single-modem code still uses modem_path[0] */
        return true;
    }
    return false;
}

/* Backward-compat helper: get path of currently active modem */
static const char *active_modem_path(const ZylTelephonyService *svc) {
    if (!svc || svc->modem_count == 0) return "";
    int idx = svc->active_modem;
    if (idx < 0 || idx >= svc->modem_count) idx = 0;
    return svc->modem_path[idx];
}

/* ─── 모뎀 상태 읽기 ─── */
static void read_modem_state(ZylTelephonyService *svc) {
    if (!svc->modem_found) return;

    /* SIM 상태 */
    GVariant *sim_path = get_dbus_property(svc->system_bus,
        MM_DBUS_NAME, active_modem_path(svc), MM_MODEM_IFACE, "Sim");
    if (sim_path) {
        const char *sp = g_variant_get_string(sim_path, NULL);
        svc->state.sim_present = (sp && strlen(sp) > 1 && strcmp(sp, "/") != 0);
        g_variant_unref(sim_path);
    }

    /* 통신사 이름 */
    GVariant *op_name = get_dbus_property(svc->system_bus,
        MM_DBUS_NAME, active_modem_path(svc),
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
        MM_DBUS_NAME, active_modem_path(svc), MM_MODEM_IFACE, "SignalQuality");
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
        MM_DBUS_NAME, active_modem_path(svc), MM_MODEM_IFACE,
        "EquipmentIdentifier");
    if (imei) {
        const char *s = g_variant_get_string(imei, NULL);
        snprintf(svc->state.imei, sizeof(svc->state.imei),
                 "%s", s ? s : "");
        g_variant_unref(imei);
    }

    /* 접속 기술 (AccessTechnologies bitmask) */
    GVariant *tech = get_dbus_property(svc->system_bus,
        MM_DBUS_NAME, active_modem_path(svc), MM_MODEM_IFACE,
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
            MM_DBUS_NAME, active_modem_path(svc), MM_MODEM_IFACE, "Sim");
        if (sim_obj) {
            const char *sim_path_str = g_variant_get_string(sim_obj, NULL);
            if (sim_path_str && strlen(sim_path_str) > 1) {
                GVariant *numbers = get_dbus_property(svc->system_bus,
                    MM_DBUS_NAME, sim_path_str, MM_SIM_IFACE,
                    "EmergencyNumbers");
                /* OwnNumbers 프로퍼티 대체 시도 */
                if (numbers) g_variant_unref(numbers);
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

    /* A call that begins here (an incoming one) brings its number with it */
    char *number = NULL;
    if (svc->call_state == ZYL_CALL_STATE_IDLE && zyl_state != ZYL_CALL_STATE_IDLE) {
        GVariant *v = get_dbus_property(svc->system_bus, MM_DBUS_NAME, object_path,
                                        MM_CALL_IFACE, "Number");
        if (v) {
            number = g_variant_dup_string(v, NULL);
            g_variant_unref(v);
        }
    }
    transition_call_state(svc, zyl_state, number);
    g_free(number);
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

/* ─── Cell Broadcast (재난문자) ─── */
static unsigned cbm_uint(ZylTelephonyService *svc, const char *path, const char *prop) {
    GVariant *v = get_dbus_property(svc->system_bus, MM_DBUS_NAME, path, MM_CBM_IFACE, prop);
    unsigned out = v ? g_variant_get_uint32(v) : 0;
    if (v) g_variant_unref(v);
    return out;
}

/* A complete message: record it and, if it is new, tell the OS */
static void process_cbm(ZylTelephonyService *svc, const char *path) {
    if (cbm_uint(svc, path, "State") != MM_CBM_RECEIVED) return;   /* parts still arriving */
    GVariant *text_v = get_dbus_property(svc->system_bus, MM_DBUS_NAME, path,
                                         MM_CBM_IFACE, "Text");
    const char *text = text_v ? g_variant_get_string(text_v, NULL) : "";
    const ZylCbsAlert *a = zyl_cbs_store_receive(svc->cbs,
        cbm_uint(svc, path, "Channel"), cbm_uint(svc, path, "MessageCode"),
        cbm_uint(svc, path, "Update"), text, (int64_t)(now_ms() / 1000));
    if (a && svc->session_bus) {
        g_message("[Telephony] Emergency alert %" G_GUINT64_FORMAT ": %s (channel %u)",
                  a->id, zyl_cbs_category_name(a->category), a->channel);
        /* The WAM brings up the alerts app on this signal */
        g_dbus_connection_emit_signal(svc->session_bus, NULL,
            ZYL_TELEPHONY_DBUS_PATH, ZYL_TELEPHONY_DBUS_NAME, "EmergencyAlert",
            g_variant_new("(tsus)", (guint64)a->id, zyl_cbs_category_name(a->category),
                          a->channel, a->text), NULL);
    }
    if (text_v) g_variant_unref(text_v);
    /* Recorded here; the modem's copy is no longer needed */
    for (int i = 0; i < svc->modem_count; i++)
        g_dbus_connection_call(svc->system_bus, MM_DBUS_NAME, svc->modem_path[i],
            MM_CB_IFACE, "Delete", g_variant_new("(o)", path), NULL,
            G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL, NULL);
}

static void on_mm_cbm_added(GDBusConnection *conn, const gchar *sender,
                            const gchar *object_path, const gchar *interface_name,
                            const gchar *signal_name, GVariant *parameters, gpointer data) {
    (void)conn; (void)sender; (void)object_path; (void)interface_name; (void)signal_name;
    const gchar *cbm_path;
    g_variant_get(parameters, "(&o)", &cbm_path);
    process_cbm(data, cbm_path);
}

/* A multi-page message is announced while still RECEIVING; it is processed
   when its State turns RECEIVED */
static void on_mm_cbm_props(GDBusConnection *conn, const gchar *sender,
                            const gchar *object_path, const gchar *interface_name,
                            const gchar *signal_name, GVariant *parameters, gpointer data) {
    (void)conn; (void)sender; (void)interface_name; (void)signal_name;
    GVariant *changed = NULL;
    g_variant_get(parameters, "(&s@a{sv}@as)", NULL, &changed, NULL);
    guint32 state = 0;
    if (changed && g_variant_lookup(changed, "State", "u", &state) && state == MM_CBM_RECEIVED)
        process_cbm(data, object_path);
    if (changed) g_variant_unref(changed);
}

static void setup_cell_broadcast(ZylTelephonyService *svc) {
    GVariantBuilder ch;
    g_variant_builder_init(&ch, G_VARIANT_TYPE("a(uu)"));
    for (int i = 0; ZYL_CBS_CHANNELS[i].to; i++)
        g_variant_builder_add(&ch, "(uu)", (guint32)ZYL_CBS_CHANNELS[i].from,
                              (guint32)ZYL_CBS_CHANNELS[i].to);
    GVariant *channels = g_variant_ref_sink(g_variant_builder_end(&ch));
    for (int i = 0; i < svc->modem_count; i++)
        g_dbus_connection_call(svc->system_bus, MM_DBUS_NAME, svc->modem_path[i],
            MM_CB_IFACE, "SetChannels", g_variant_new("(@a(uu))", channels), NULL,
            G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL, NULL);
    g_variant_unref(channels);

    svc->mm_cbs_sub = g_dbus_connection_signal_subscribe(svc->system_bus,
        MM_DBUS_NAME, MM_CB_IFACE, "Added", NULL, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, on_mm_cbm_added, svc, NULL);
    svc->mm_cbm_props_sub = g_dbus_connection_signal_subscribe(svc->system_bus,
        MM_DBUS_NAME, DBUS_PROP_IFACE, "PropertiesChanged", NULL, MM_CBM_IFACE,
        G_DBUS_SIGNAL_FLAGS_NONE, on_mm_cbm_props, svc, NULL);
}

static GVariant *alerts_variant(const ZylTelephonyService *svc) {
    GVariantBuilder list;
    g_variant_builder_init(&list, G_VARIANT_TYPE("aa{sv}"));
    for (int i = 0; i < zyl_cbs_store_count(svc->cbs); i++) {
        const ZylCbsAlert *a = zyl_cbs_store_get(svc->cbs, i);
        GVariantBuilder d;
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&d, "{sv}", "id", g_variant_new_uint64(a->id));
        g_variant_builder_add(&d, "{sv}", "at", g_variant_new_int64(a->at));
        g_variant_builder_add(&d, "{sv}", "channel", g_variant_new_uint32(a->channel));
        g_variant_builder_add(&d, "{sv}", "category",
                              g_variant_new_string(zyl_cbs_category_name(a->category)));
        g_variant_builder_add(&d, "{sv}", "text", g_variant_new_string(a->text));
        g_variant_builder_add(&d, "{sv}", "acked", g_variant_new_boolean(a->acked));
        g_variant_builder_add_value(&list, g_variant_builder_end(&d));
    }
    return g_variant_new("(aa{sv})", &list);
}

/* ─── 모바일 데이터 (NetworkManager) ───
 * One connection for every SIM. gsm.auto-config takes the APN from the
 * mobile-broadband-provider-info database for whichever network the modem
 * registers to, so no per-carrier table lives here. Both IP families are on:
 * Korean and many other carriers hand out IPv6, increasingly IPv6 only.
 * There, ipv4.clat (464XLAT, NetworkManager >= 1.58) keeps IPv4-only apps
 * and literals working through the carrier's NAT64. IPv6 uses temporary
 * addresses so the address does not follow the user around. Data roaming
 * starts off: NetworkManager's default (home-only no) would run up charges
 * abroad before the user knew it was on. */
#define ZYL_MOBILE_CONN "zyl-mobile"

static int nmcli(const char *const *args, char **out) {
    const char *argv[24] = { "nmcli" };
    int n = 1;
    for (int i = 0; args[i] && n < 23; i++) argv[n++] = args[i];
    argv[n] = NULL;
    int status = -1;
    if (!g_spawn_sync(NULL, (char **)argv, NULL,
                      G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL |
                      (out ? 0 : G_SPAWN_STDOUT_TO_DEV_NULL),
                      NULL, NULL, out, NULL, &status, NULL))
        return -1;
    return g_spawn_check_wait_status(status, NULL) ? 0 : 1;
}

static bool mobile_connection_exists(void) {
    char *out = NULL;
    const char *a[] = { "-g", "NAME", "connection", "show", NULL };
    bool found = false;
    if (nmcli(a, &out) == 0 && out) {
        char **names = g_strsplit(out, "\n", -1);
        for (int i = 0; names[i]; i++)
            if (strcmp(names[i], ZYL_MOBILE_CONN) == 0) found = true;
        g_strfreev(names);
    }
    g_free(out);
    return found;
}

static void ensure_mobile_connection(void) {
    if (mobile_connection_exists()) return;
    const char *with_clat[] = { "connection", "add", "type", "gsm", "ifname", "*",
        "con-name", ZYL_MOBILE_CONN, "gsm.auto-config", "yes", "gsm.home-only", "yes",
        "ipv4.method", "auto", "ipv6.method", "auto",
        "ipv6.addr-gen-mode", "stable-privacy", "ipv6.ip6-privacy", "2",
        "ipv4.clat", "auto", NULL };
    if (nmcli(with_clat, NULL) == 0) return;
    /* NetworkManager before 1.58 rejects ipv4.clat: dual stack without it */
    const char *plain[] = { "connection", "add", "type", "gsm", "ifname", "*",
        "con-name", ZYL_MOBILE_CONN, "gsm.auto-config", "yes", "gsm.home-only", "yes",
        "ipv4.method", "auto", "ipv6.method", "auto",
        "ipv6.addr-gen-mode", "stable-privacy", "ipv6.ip6-privacy", "2", NULL };
    if (nmcli(plain, NULL) != 0)
        g_warning("[Telephony] could not create the mobile data connection");
}

/* enabled = autoconnect; roaming = not home-only */
static bool read_mobile_data(bool *enabled, bool *roaming) {
    char *out = NULL;
    const char *a[] = { "-g", "connection.autoconnect,gsm.home-only",
                        "connection", "show", ZYL_MOBILE_CONN, NULL };
    bool ok = nmcli(a, &out) == 0 && out;
    if (ok) {
        char **v = g_strsplit(g_strstrip(out), "\n", -1);
        ok = v[0] && v[1];
        if (ok) {
            *enabled = strcmp(v[0], "yes") == 0;
            *roaming = strcmp(v[1], "no") == 0;
        }
        g_strfreev(v);
    }
    g_free(out);
    return ok;
}

static bool write_mobile_data(bool enabled, bool roaming) {
    const char *mod[] = { "connection", "modify", ZYL_MOBILE_CONN,
        "connection.autoconnect", enabled ? "yes" : "no",
        "gsm.home-only", roaming ? "no" : "yes", NULL };
    if (nmcli(mod, NULL) != 0) return false;
    /* Apply now, not at the next registration. Bringing it up can fail
       without coverage; autoconnect then connects when the network appears. */
    const char *now[] = { "connection", enabled ? "up" : "down", ZYL_MOBILE_CONN, NULL };
    nmcli(now, NULL);
    return true;
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
        "Added", active_modem_path(svc), NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_mm_sms_added, svc, NULL);

    setup_cell_broadcast(svc);
    ensure_mobile_connection();

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
    "      <arg type='a{sv}' name='call' direction='out'/>"
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
    "    <method name='GetCallLog'>"
    "      <arg type='aa{sv}' name='entries' direction='out'/>"
    "    </method>"
    "    <method name='DeleteCallLog'>"
    "      <arg type='t' name='id' direction='in'/>"
    "      <arg type='b' name='found' direction='out'/>"
    "    </method>"
    "    <method name='SendDTMF'>"
    "      <arg type='s' name='digit' direction='in'/>"
    "      <arg type='b' name='sent' direction='out'/>"
    "    </method>"
    "    <method name='GetEmergencyAlerts'>"
    "      <arg type='aa{sv}' name='alerts' direction='out'/>"
    "    </method>"
    "    <method name='AcknowledgeEmergencyAlert'>"
    "      <arg type='t' name='id' direction='in'/>"
    "      <arg type='b' name='found' direction='out'/>"
    "    </method>"
    "    <method name='GetEmergencyAlertSettings'>"
    "      <arg type='a{sv}' name='enabled' direction='out'/>"
    "    </method>"
    "    <method name='SetEmergencyAlertEnabled'>"
    "      <arg type='s' name='category' direction='in'/>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "      <arg type='b' name='applied' direction='out'/>"
    "    </method>"
    "    <method name='GetMobileData'>"
    "      <arg type='a{sv}' name='state' direction='out'/>"
    "    </method>"
    "    <method name='SetMobileData'>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "      <arg type='b' name='roaming' direction='in'/>"
    "      <arg type='b' name='applied' direction='out'/>"
    "    </method>"
    "    <signal name='EmergencyAlert'>"
    "      <arg type='t' name='id'/>"
    "      <arg type='s' name='category'/>"
    "      <arg type='u' name='channel'/>"
    "      <arg type='s' name='text'/>"
    "    </signal>"
    "    <signal name='CallStateChanged'>"
    "      <arg type='s' name='old_state'/>"
    "      <arg type='s' name='new_state'/>"
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
        GVariantBuilder d;
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&d, "{sv}", "state",
                              g_variant_new_string(call_state_name(svc->call_state)));
        g_variant_builder_add(&d, "{sv}", "number", g_variant_new_string(svc->active_call_number));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sv})", &d));
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
    } else if (g_strcmp0(method, "GetCallLog") == 0) {
        g_dbus_method_invocation_return_value(inv, calllog_variant(svc));
    } else if (g_strcmp0(method, "DeleteCallLog") == 0) {
        guint64 id = 0;
        g_variant_get(params, "(t)", &id);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", zyl_calllog_delete(svc->calllog, id)));
    } else if (g_strcmp0(method, "SendDTMF") == 0) {
        const gchar *digit = NULL;
        g_variant_get(params, "(&s)", &digit);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", zyl_telephony_send_dtmf(svc, digit) == 0));
    } else if (g_strcmp0(method, "GetEmergencyAlerts") == 0) {
        g_dbus_method_invocation_return_value(inv, alerts_variant(svc));
    } else if (g_strcmp0(method, "AcknowledgeEmergencyAlert") == 0) {
        guint64 id = 0;
        g_variant_get(params, "(t)", &id);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", zyl_cbs_store_ack(svc->cbs, id)));
    } else if (g_strcmp0(method, "GetMobileData") == 0) {
        bool enabled = false, roaming = false;
        bool known = svc->modem_found && read_mobile_data(&enabled, &roaming);
        GVariantBuilder d;
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&d, "{sv}", "supported", g_variant_new_boolean(known));
        g_variant_builder_add(&d, "{sv}", "enabled", g_variant_new_boolean(enabled));
        g_variant_builder_add(&d, "{sv}", "roaming", g_variant_new_boolean(roaming));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sv})", &d));
    } else if (g_strcmp0(method, "SetMobileData") == 0) {
        gboolean enabled = FALSE, roaming = FALSE;
        g_variant_get(params, "(bb)", &enabled, &roaming);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)",
            svc->modem_found && write_mobile_data(enabled, roaming)));
    } else if (g_strcmp0(method, "GetEmergencyAlertSettings") == 0) {
        GVariantBuilder d;
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        for (int c = 0; c < ZYL_CBS_CATEGORIES; c++)
            g_variant_builder_add(&d, "{sv}", zyl_cbs_category_name((ZylCbsCategory)c),
                g_variant_new_boolean(zyl_cbs_store_enabled(svc->cbs, (ZylCbsCategory)c)));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sv})", &d));
    } else if (g_strcmp0(method, "SetEmergencyAlertEnabled") == 0) {
        const gchar *category = NULL;
        gboolean on = FALSE;
        g_variant_get(params, "(&sb)", &category, &on);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)",
            zyl_cbs_store_set_enabled(svc->cbs, zyl_cbs_category_from_name(category), on)));
    }
}

static const GDBusInterfaceVTable telephony_vtable = {
    .method_call = handle_telephony_method,
};

static void on_telephony_bus_acquired(GDBusConnection *conn, const gchar *name,
                                       gpointer data) {
    (void)name;
    ZylTelephonyService *svc = data;
    svc->session_bus = conn;

    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(telephony_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, ZYL_TELEPHONY_DBUS_PATH,
            info->interfaces[0], &telephony_vtable, svc, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_message("[Telephony] D-Bus registered: %s", ZYL_TELEPHONY_DBUS_NAME);
}

/* ─── 공개 API ─── */

ZylTelephonyService *zyl_telephony_create(void) {
    ZylTelephonyService *svc = g_new0(ZylTelephonyService, 1);
    svc->call_state = ZYL_CALL_STATE_IDLE;
    /* History is kept even without a modem: alerts received before a SIM swap stay readable */
    svc->cbs = zyl_cbs_store_open(ZYL_ALERTS_FILE);
    svc->calllog = zyl_calllog_open(ZYL_CALLLOG_FILE);

    /* system bus 연결 (ModemManager) */
    GError *err = NULL;
    svc->system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (err) {
        g_warning("[Telephony] Cannot connect to system bus: %s", err->message);
        g_clear_error(&err);
    }

    /* 모뎀 탐색 (dual-SIM: 최대 MAX_MODEMS 개) */
    if (svc->system_bus) {
        svc->modem_found = find_modems(svc);
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
        if (svc->mm_cbs_sub)
            g_dbus_connection_signal_unsubscribe(svc->system_bus, svc->mm_cbs_sub);
        if (svc->mm_cbm_props_sub)
            g_dbus_connection_signal_unsubscribe(svc->system_bus, svc->mm_cbm_props_sub);
        g_object_unref(svc->system_bus);
    }

    g_bus_unown_name(svc->dbus_owner_id);
    zyl_cbs_store_free(svc->cbs);
    zyl_calllog_free(svc->calllog);
    g_free(svc);
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

    GVariant *result = g_dbus_connection_call_sync(svc->system_bus,
        MM_DBUS_NAME, active_modem_path(svc),
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
        if (start_result) g_variant_unref(start_result);
        g_variant_unref(result);
        return -1;
    }

    if (start_result) g_variant_unref(start_result);
    g_variant_unref(result);
    return 0;
}

int zyl_telephony_answer(ZylTelephonyService *svc) {
    if (!svc || !svc->modem_found || !svc->system_bus) return -1;
    if (svc->call_state != ZYL_CALL_STATE_RINGING) return -1;

    g_message("[Telephony] Answering call");

    /* ListCalls로 현재 통화 목록 조회 후 Accept */
    GError *err = NULL;
    GVariant *result = g_dbus_connection_call_sync(svc->system_bus,
        MM_DBUS_NAME, active_modem_path(svc),
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
        MM_DBUS_NAME, active_modem_path(svc),
        MM_VOICE_IFACE, "HangupAll",
        NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (err) {
        g_warning("[Telephony] HangupAll failed: %s", err->message);
        g_clear_error(&err);
        /* ListCalls + 개별 Hangup 폴백 */
        GVariant *result = g_dbus_connection_call_sync(svc->system_bus,
            MM_DBUS_NAME, active_modem_path(svc),
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

/* Tones on the call in progress (keypad during a call: menus, PINs) */
int zyl_telephony_send_dtmf(ZylTelephonyService *svc, const char *digits) {
    if (!svc || !digits || !*digits || strlen(digits) > 32 ||
        strspn(digits, "0123456789*#ABCD") != strlen(digits)) return -1;
    if (!svc->modem_found || !svc->system_bus || svc->call_state != ZYL_CALL_STATE_ACTIVE) return -1;
    GVariant *calls = g_dbus_connection_call_sync(svc->system_bus,
        MM_DBUS_NAME, active_modem_path(svc), MM_VOICE_IFACE, "ListCalls",
        NULL, G_VARIANT_TYPE("(ao)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
    if (!calls) return -1;
    int rc = -1;
    GVariantIter *iter;
    const gchar *call_path;
    g_variant_get(calls, "(ao)", &iter);
    if (g_variant_iter_next(iter, "&o", &call_path)) {
        GError *err = NULL;
        GVariant *r = g_dbus_connection_call_sync(svc->system_bus, MM_DBUS_NAME, call_path,
            MM_CALL_IFACE, "SendDtmf", g_variant_new("(s)", digits), NULL,
            G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);
        if (r) { rc = 0; g_variant_unref(r); }
        else {
            g_warning("[Telephony] SendDtmf failed: %s", err->message);
            g_clear_error(&err);
        }
    }
    g_variant_iter_free(iter);
    g_variant_unref(calls);
    return rc;
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
        MM_DBUS_NAME, active_modem_path(svc),
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

/* ─── main(): 독립 데몬 실행 ─── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    g_message("[Telephony] Zyl Telephony Service starting...");

    ZylTelephonyService *svc = zyl_telephony_create();
    if (!svc) {
        g_critical("[Telephony] Failed to create telephony service");
        return 1;
    }

    if (svc->modem_count > 1) {
        g_message("[Telephony] Dual-SIM detected (%d modems)", svc->modem_count);
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
