#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 알람/스케줄러 서비스 구현 — D-Bus Set/Cancel/List + GLib 타이머 관리
 * 수행범위: 알람 등록/취소/조회, g_timeout_add 기반 트리거, AlarmTriggered 시그널 발송
 * 의존방향: alarm.h, gio/gio.h, glib.h
 * SOLID: SRP — 알람 스케줄링과 D-Bus 통신만 담당
 * ────────────────────────────────────────────────────────── */

#include "alarm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>

#define ALARM_PERSIST_DIR  "/data/alarms"
#define ALARM_PERSIST_FILE "/data/alarms/alarms.json"

/* ─── 내부 상수 ─── */
#define MAX_ALARMS 256

/* ─── 내부 알람 레코드 (타이머 ID 포함) ─── */
typedef struct {
    ZylAlarm alarm;
    guint    timer_id;   /* GLib 타이머 핸들, 0 = 비활성 */
} AlarmEntry;

/* ─── 서비스 구조체 ─── */
struct ZylAlarmService {
    AlarmEntry       entries[MAX_ALARMS];
    int              count;

    /* D-Bus */
    GDBusConnection *dbus;
    guint            dbus_owner_id;
};

/* ─── 전역 서비스 포인터 (타이머 콜백에서 참조) ─── */
static ZylAlarmService *g_svc = NULL;

/* ─── 유틸리티: epoch ms 현재 시각 ─── */
static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
}

/* ─── AlarmTriggered D-Bus 시그널 발송 ─── */
static void emit_alarm_triggered(ZylAlarmService *svc,
                                  const char *tag,
                                  const char *app_id) {
    if (!svc || !svc->dbus) return;
    GError *err = NULL;
    g_dbus_connection_emit_signal(svc->dbus, NULL,
        ZYL_ALARM_DBUS_PATH,
        ZYL_ALARM_DBUS_NAME,
        "AlarmTriggered",
        g_variant_new("(ss)", tag ? tag : "", app_id ? app_id : ""),
        &err);
    if (err) {
        g_warning("[Alarm] emit AlarmTriggered failed: %s", err->message);
        g_error_free(err);
    }
}

/* ─── 타이머 콜백 컨텍스트 ─── */
typedef struct {
    ZylAlarmService *svc;
    char             tag[256];
} AlarmTimerCtx;

static gboolean on_alarm_fire(gpointer data) {
    AlarmTimerCtx *ctx = data;
    ZylAlarmService *svc = ctx->svc;
    if (!svc) { g_free(ctx); return G_SOURCE_REMOVE; }

    /* 엔트리 검색 */
    AlarmEntry *entry = NULL;
    for (int i = 0; i < svc->count; i++) {
        if (g_strcmp0(svc->entries[i].alarm.tag, ctx->tag) == 0) {
            entry = &svc->entries[i];
            break;
        }
    }
    if (!entry) { g_free(ctx); return G_SOURCE_REMOVE; }

    g_message("[Alarm] Triggered: tag=%s app=%s",
              entry->alarm.tag, entry->alarm.app_id ? entry->alarm.app_id : "");

    /* D-Bus 시그널 발송 */
    emit_alarm_triggered(svc, entry->alarm.tag, entry->alarm.app_id);

    if (entry->alarm.repeating && entry->alarm.interval_ms > 0) {
        /* 반복 알람 — 다음 트리거 예약 (interval_ms) */
        entry->alarm.trigger_at = now_ms() + entry->alarm.interval_ms;
        guint ms = (guint)(entry->alarm.interval_ms > G_MAXUINT
                           ? G_MAXUINT : entry->alarm.interval_ms);
        /* 새 타이머 등록 (context 재사용) */
        entry->timer_id = g_timeout_add(ms, on_alarm_fire, ctx);
        return G_SOURCE_REMOVE; /* 이전 소스 제거 */
    } else {
        /* 1회성 알람 — 엔트리 제거 */
        entry->timer_id = 0;
        g_free(entry->alarm.tag);
        g_free(entry->alarm.app_id);
        /* 마지막 엔트리로 교체 */
        int last = --svc->count;
        if (entry != &svc->entries[last]) {
            *entry = svc->entries[last];
        }
        memset(&svc->entries[last], 0, sizeof(AlarmEntry));
        g_free(ctx);
        return G_SOURCE_REMOVE;
    }
}

/* ─── D-Bus 인트로스펙션 ─── */
static const char *alarm_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_ALARM_DBUS_NAME "'>"
    "    <method name='Set'>"
    "      <arg type='s' name='tag'        direction='in'/>"
    "      <arg type='t' name='trigger_at' direction='in'/>"
    "      <arg type='t' name='interval_ms' direction='in'/>"
    "      <arg type='s' name='app_id'     direction='in'/>"
    "      <arg type='b' name='repeating'  direction='in'/>"
    "      <arg type='b' name='ok'         direction='out'/>"
    "    </method>"
    "    <method name='Cancel'>"
    "      <arg type='s' name='tag' direction='in'/>"
    "      <arg type='b' name='ok'  direction='out'/>"
    "    </method>"
    "    <method name='List'>"
    "      <arg type='a(sttsb)' name='alarms' direction='out'/>"
    "    </method>"
    "    <signal name='AlarmTriggered'>"
    "      <arg type='s' name='tag'/>"
    "      <arg type='s' name='app_id'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* ─── D-Bus 메서드 핸들러 ─── */
static void handle_alarm_method(GDBusConnection      *conn,
                                 const gchar          *sender,
                                 const gchar          *path,
                                 const gchar          *iface,
                                 const gchar          *method,
                                 GVariant             *params,
                                 GDBusMethodInvocation *inv,
                                 gpointer              data) {
    ZylAlarmService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "Set") == 0) {
        const gchar *tag, *app_id;
        guint64      trigger_at, interval_ms;
        gboolean     repeating;
        g_variant_get(params, "(&stts&sb)",
                      &tag, &trigger_at, &interval_ms, &app_id, &repeating);

        ZylAlarm alarm = {
            .tag        = (char *)tag,
            .trigger_at = trigger_at,
            .interval_ms = interval_ms,
            .app_id     = (char *)app_id,
            .repeating  = (bool)repeating,
        };
        int rc = zyl_alarm_set(svc, &alarm);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", rc == 0));

    } else if (g_strcmp0(method, "Cancel") == 0) {
        const gchar *tag;
        g_variant_get(params, "(&s)", &tag);
        int rc = zyl_alarm_cancel(svc, tag);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", rc == 0));

    } else if (g_strcmp0(method, "List") == 0) {
        ZylAlarm *alarms = NULL;
        int count = 0;
        zyl_alarm_list(svc, &alarms, &count);

        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(sttsb)"));
        for (int i = 0; i < count; i++) {
            g_variant_builder_add(&builder, "(sttsb)",
                alarms[i].tag        ? alarms[i].tag    : "",
                (guint64)alarms[i].trigger_at,
                (guint64)alarms[i].interval_ms,
                alarms[i].app_id     ? alarms[i].app_id : "",
                (gboolean)alarms[i].repeating);
        }
        zyl_alarm_list_free(alarms, count);

        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(a(sttsb))", &builder));
    }
}

static const GDBusInterfaceVTable alarm_vtable = {
    .method_call = handle_alarm_method,
};

static void on_alarm_bus_acquired(GDBusConnection *conn,
                                   const gchar     *name,
                                   gpointer         data) {
    ZylAlarmService *svc = data;
    (void)name;
    svc->dbus = conn;

    GError *err = NULL;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(
        alarm_introspection_xml, &err);
    if (!info) {
        g_critical("[Alarm] Failed to parse introspection XML: %s",
                   err ? err->message : "unknown");
        if (err) g_error_free(err);
        return;
    }

    g_dbus_connection_register_object(conn,
        ZYL_ALARM_DBUS_PATH,
        info->interfaces[0],
        &alarm_vtable,
        svc, NULL, NULL);
    g_dbus_node_info_unref(info);
    g_message("[Alarm] D-Bus registered: %s", ZYL_ALARM_DBUS_NAME);
}

/* ─── 공개 API 구현 ─── */

/* ─── Persistence ─── */

static void alarm_persist_save(ZylAlarmService *svc) {
    if (!svc) return;

    struct stat st;
    if (stat(ALARM_PERSIST_DIR, &st) != 0) mkdir(ALARM_PERSIST_DIR, 0700);

    JsonBuilder *b = json_builder_new();
    json_builder_begin_array(b);
    for (int i = 0; i < svc->count; i++) {
        ZylAlarm *a = &svc->entries[i].alarm;
        json_builder_begin_object(b);
        json_builder_set_member_name(b, "tag");
        json_builder_add_string_value(b, a->tag ? a->tag : "");
        json_builder_set_member_name(b, "trigger_at");
        json_builder_add_int_value(b, (gint64)a->trigger_at);
        json_builder_set_member_name(b, "interval_ms");
        json_builder_add_int_value(b, (gint64)a->interval_ms);
        json_builder_set_member_name(b, "app_id");
        json_builder_add_string_value(b, a->app_id ? a->app_id : "");
        json_builder_set_member_name(b, "repeating");
        json_builder_add_boolean_value(b, a->repeating);
        json_builder_end_object(b);
    }
    json_builder_end_array(b);

    JsonNode *root = json_builder_get_root(b);
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, root);
    GError *err = NULL;
    json_generator_to_file(gen, ALARM_PERSIST_FILE, &err);
    if (err) {
        g_warning("[Alarm] persist save failed: %s", err->message);
        g_error_free(err);
    }
    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(b);
}

static void alarm_persist_load(ZylAlarmService *svc) {
    if (!svc) return;

    GError *err = NULL;
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_file(parser, ALARM_PERSIST_FILE, &err)) {
        g_clear_error(&err);
        g_object_unref(parser);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
        g_object_unref(parser);
        return;
    }

    JsonArray *arr = json_node_get_array(root);
    guint n = json_array_get_length(arr);
    uint64_t now = now_ms();

    for (guint i = 0; i < n && svc->count < MAX_ALARMS; i++) {
        JsonNode *anode = json_array_get_element(arr, i);
        if (!JSON_NODE_HOLDS_OBJECT(anode)) continue;
        JsonObject *ao = json_node_get_object(anode);

        uint64_t trigger = json_object_has_member(ao, "trigger_at") ?
            (uint64_t)json_object_get_int_member(ao, "trigger_at") : 0;
        bool repeating = json_object_has_member(ao, "repeating") &&
                         json_object_get_boolean_member(ao, "repeating");
        uint64_t interval = json_object_has_member(ao, "interval_ms") ?
            (uint64_t)json_object_get_int_member(ao, "interval_ms") : 0;

        /* Skip expired non-repeating alarms */
        if (!repeating && trigger < now) continue;

        ZylAlarm alarm = {0};
        alarm.tag = g_strdup(json_object_has_member(ao, "tag") ?
                             json_object_get_string_member(ao, "tag") : "");
        alarm.trigger_at = trigger;
        alarm.interval_ms = interval;
        alarm.app_id = g_strdup(json_object_has_member(ao, "app_id") ?
                                json_object_get_string_member(ao, "app_id") : "");
        alarm.repeating = repeating;

        /* For repeating alarms that fired while service was down,
           advance trigger_at to the next future occurrence */
        if (repeating && interval > 0 && trigger < now) {
            uint64_t delta = now - trigger;
            uint64_t periods = delta / interval + 1;
            alarm.trigger_at = trigger + periods * interval;
        }

        zyl_alarm_set(svc, &alarm);
        g_free(alarm.tag);
        g_free(alarm.app_id);
    }

    g_object_unref(parser);
    g_message("[Alarm] Loaded %d alarms from disk", svc->count);
}

ZylAlarmService *zyl_alarm_service_create(void) {
    ZylAlarmService *svc = calloc(1, sizeof(ZylAlarmService));
    if (!svc) return NULL;

    g_svc = svc;

    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
        ZYL_ALARM_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_alarm_bus_acquired,
        NULL, NULL,
        svc, NULL);

    /* Load persisted alarms */
    alarm_persist_load(svc);

    g_message("[Alarm] Zyl OS Alarm Service created");
    return svc;
}

void zyl_alarm_service_destroy(ZylAlarmService *svc) {
    if (!svc) return;

    /* 모든 타이머 취소 */
    for (int i = 0; i < svc->count; i++) {
        if (svc->entries[i].timer_id) {
            g_source_remove(svc->entries[i].timer_id);
        }
        g_free(svc->entries[i].alarm.tag);
        g_free(svc->entries[i].alarm.app_id);
    }

    g_bus_unown_name(svc->dbus_owner_id);
    free(svc);
    g_svc = NULL;
}

int zyl_alarm_set(ZylAlarmService *svc, const ZylAlarm *alarm) {
    if (!svc || !alarm || !alarm->tag || alarm->tag[0] == '\0') return -1;

    /* 중복 tag 확인 — 기존 알람 갱신 */
    AlarmEntry *entry = NULL;
    for (int i = 0; i < svc->count; i++) {
        if (g_strcmp0(svc->entries[i].alarm.tag, alarm->tag) == 0) {
            entry = &svc->entries[i];
            break;
        }
    }

    if (entry) {
        /* 기존 타이머 취소 후 갱신 */
        if (entry->timer_id) {
            g_source_remove(entry->timer_id);
            entry->timer_id = 0;
        }
        g_free(entry->alarm.tag);
        g_free(entry->alarm.app_id);
    } else {
        /* 새 슬롯 */
        if (svc->count >= MAX_ALARMS) {
            g_warning("[Alarm] Max alarms (%d) reached", MAX_ALARMS);
            return -1;
        }
        entry = &svc->entries[svc->count++];
    }

    entry->alarm.tag        = g_strdup(alarm->tag);
    entry->alarm.trigger_at = alarm->trigger_at;
    entry->alarm.interval_ms = alarm->interval_ms;
    entry->alarm.app_id     = alarm->app_id ? g_strdup(alarm->app_id) : g_strdup("");
    entry->alarm.repeating  = alarm->repeating;
    entry->timer_id         = 0;

    /* 트리거 시각까지 남은 시간 (ms) 계산 */
    uint64_t now = now_ms();
    guint delay_ms = 0;
    if (alarm->trigger_at > now) {
        uint64_t diff = alarm->trigger_at - now;
        delay_ms = (guint)(diff > G_MAXUINT ? G_MAXUINT : diff);
    }
    /* 0ms 지연 시 다음 GLib 루프 이터레이션에서 즉시 발동 */

    AlarmTimerCtx *ctx = g_new(AlarmTimerCtx, 1);
    ctx->svc = svc;
    g_strlcpy(ctx->tag, alarm->tag, sizeof(ctx->tag));

    entry->timer_id = g_timeout_add(delay_ms, on_alarm_fire, ctx);

    g_message("[Alarm] Set: tag=%s trigger_at=%llu interval_ms=%llu repeating=%d delay_ms=%u",
              alarm->tag,
              (unsigned long long)alarm->trigger_at,
              (unsigned long long)alarm->interval_ms,
              (int)alarm->repeating,
              delay_ms);
    alarm_persist_save(svc);
    return 0;
}

int zyl_alarm_cancel(ZylAlarmService *svc, const char *tag) {
    if (!svc || !tag) return -1;

    for (int i = 0; i < svc->count; i++) {
        if (g_strcmp0(svc->entries[i].alarm.tag, tag) == 0) {
            if (svc->entries[i].timer_id) {
                g_source_remove(svc->entries[i].timer_id);
                svc->entries[i].timer_id = 0;
            }
            g_free(svc->entries[i].alarm.tag);
            g_free(svc->entries[i].alarm.app_id);
            /* 마지막 엔트리로 교체 */
            int last = --svc->count;
            if (i != last) {
                svc->entries[i] = svc->entries[last];
            }
            memset(&svc->entries[last], 0, sizeof(AlarmEntry));
            g_message("[Alarm] Cancelled: tag=%s", tag);
            alarm_persist_save(svc);
            return 0;
        }
    }
    return -1; /* 알람 없음 */
}

int zyl_alarm_list(ZylAlarmService *svc, ZylAlarm **out, int *count) {
    if (!svc || !out || !count) return -1;

    if (svc->count == 0) {
        *out = NULL;
        *count = 0;
        return 0;
    }

    ZylAlarm *result = calloc((size_t)svc->count, sizeof(ZylAlarm));
    if (!result) return -1;

    for (int i = 0; i < svc->count; i++) {
        result[i].tag        = g_strdup(svc->entries[i].alarm.tag);
        result[i].trigger_at = svc->entries[i].alarm.trigger_at;
        result[i].interval_ms = svc->entries[i].alarm.interval_ms;
        result[i].app_id     = g_strdup(svc->entries[i].alarm.app_id);
        result[i].repeating  = svc->entries[i].alarm.repeating;
    }

    *out   = result;
    *count = svc->count;
    return 0;
}

void zyl_alarm_list_free(ZylAlarm *alarms, int count) {
    if (!alarms) return;
    for (int i = 0; i < count; i++) {
        g_free(alarms[i].tag);
        g_free(alarms[i].app_id);
    }
    free(alarms);
}

/* ─── 데몬 진입점 ─── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    ZylAlarmService *svc = zyl_alarm_service_create();
    if (!svc) {
        g_critical("[Alarm] Failed to create service");
        return 1;
    }

    g_message("[Alarm] Zyl OS Alarm Service started");

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    zyl_alarm_service_destroy(svc);
    return 0;
}
