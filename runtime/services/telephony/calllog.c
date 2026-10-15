/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Policy
 *
 * 역할: 통화 기록 구현 — 판정, 보관, 영속
 * 수행범위: calllog.h 참고. 파일은 GKeyFile, 원자적 교체(0600)
 * 의존방향: calllog.h, glib
 * SOLID: SRP — 통화 기록만
 * ────────────────────────────────────────────────────────── */

#include "calllog.h"

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

struct ZylCallLog {
    char   *path;            /* NULL: memory only */
    GArray *entries;         /* ZylCallLogEntry, oldest first */
};

static void calllog_save(const ZylCallLog *log) {
    if (!log->path) return;
    GKeyFile *kf = g_key_file_new();
    for (guint i = 0; i < log->entries->len; i++) {
        const ZylCallLogEntry *e = &g_array_index(log->entries, ZylCallLogEntry, i);
        char group[48];
        snprintf(group, sizeof(group), "call-%" G_GUINT64_FORMAT, e->id);
        g_key_file_set_uint64(kf, group, "id", e->id);
        g_key_file_set_int64(kf, group, "time", e->time_ms);
        g_key_file_set_uint64(kf, group, "duration", e->duration);
        g_key_file_set_string(kf, group, "type", e->type);
        g_key_file_set_string(kf, group, "number", e->number);
    }
    gsize len = 0;
    char *data = g_key_file_to_data(kf, &len, NULL);
    char *dir = g_path_get_dirname(log->path);
    g_mkdir_with_parents(dir, 0700);
    GError *err = NULL;
    if (!g_file_set_contents_full(log->path, data, (gssize)len,
            G_FILE_SET_CONTENTS_CONSISTENT, 0600, &err)) {
        g_warning("[CallLog] cannot save: %s", err->message);
        g_clear_error(&err);
    }
    g_free(dir);
    g_free(data);
    g_key_file_free(kf);
}

ZylCallLog *zyl_calllog_open(const char *path) {
    ZylCallLog *log = g_new0(ZylCallLog, 1);
    log->path = g_strdup(path);
    log->entries = g_array_new(FALSE, TRUE, sizeof(ZylCallLogEntry));
    GKeyFile *kf = g_key_file_new();
    if (path && g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        gsize n = 0;
        char **groups = g_key_file_get_groups(kf, &n);
        for (gsize i = 0; i < n; i++) {
            ZylCallLogEntry e = {0};
            e.id = g_key_file_get_uint64(kf, groups[i], "id", NULL);
            e.time_ms = g_key_file_get_int64(kf, groups[i], "time", NULL);
            e.duration = (uint32_t)g_key_file_get_uint64(kf, groups[i], "duration", NULL);
            char *type = g_key_file_get_string(kf, groups[i], "type", NULL);
            char *number = g_key_file_get_string(kf, groups[i], "number", NULL);
            g_strlcpy(e.type, type ? type : "", sizeof(e.type));
            g_strlcpy(e.number, number ? number : "", sizeof(e.number));
            g_free(type);
            g_free(number);
            g_array_append_val(log->entries, e);
        }
        g_strfreev(groups);
    }
    g_key_file_free(kf);
    return log;
}

void zyl_calllog_free(ZylCallLog *log) {
    if (!log) return;
    g_array_free(log->entries, TRUE);
    g_free(log->path);
    g_free(log);
}

const ZylCallLogEntry *zyl_calllog_record(ZylCallLog *log, const char *number,
                                          bool incoming, int64_t start_ms,
                                          int64_t answered_ms, int64_t end_ms) {
    ZylCallLogEntry e = {0};
    e.id = (uint64_t)start_ms;
    if (log->entries->len) {
        uint64_t last = g_array_index(log->entries, ZylCallLogEntry, log->entries->len - 1).id;
        if (e.id <= last) e.id = last + 1;   /* ids stay unique and increasing */
    }
    e.time_ms = start_ms;
    e.duration = answered_ms && end_ms > answered_ms ? (uint32_t)((end_ms - answered_ms) / 1000) : 0;
    g_strlcpy(e.type, !incoming ? "outgoing" : answered_ms ? "incoming" : "missed", sizeof(e.type));
    g_strlcpy(e.number, number ? number : "", sizeof(e.number));
    g_array_append_val(log->entries, e);
    if (log->entries->len > ZYL_CALLLOG_MAX) g_array_remove_index(log->entries, 0);
    calllog_save(log);
    return &g_array_index(log->entries, ZylCallLogEntry, log->entries->len - 1);
}

unsigned zyl_calllog_count(const ZylCallLog *log) {
    return log ? log->entries->len : 0;
}

const ZylCallLogEntry *zyl_calllog_get(const ZylCallLog *log, unsigned i) {
    if (!log || i >= log->entries->len) return NULL;
    return &g_array_index(log->entries, ZylCallLogEntry, log->entries->len - 1 - i);
}

bool zyl_calllog_delete(ZylCallLog *log, uint64_t id) {
    for (guint i = 0; i < log->entries->len; i++) {
        if (g_array_index(log->entries, ZylCallLogEntry, i).id == id) {
            g_array_remove_index(log->entries, i);
            calllog_save(log);
            return true;
        }
    }
    return false;
}
