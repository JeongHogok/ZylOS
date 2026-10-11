/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Policy
 *
 * 역할: 재난문자(Cell Broadcast) 정책 구현
 * 수행범위: 3GPP TS 23.041 §9.4.1.2.2 메시지 식별자 분류, 채널 범위,
 *           재전송 무시, 경보 기록 영속
 * 의존방향: cbs.h, glib
 * SOLID: SRP — 분류·기록만
 * ────────────────────────────────────────────────────────── */

#include "cbs.h"

#include <stdio.h>
#include <string.h>
#include <glib.h>

/* ETWS 4352-4359 and CMAS 4370-4399. 4400 (geo-fencing trigger) carries no
   text for the user and is left out. */
const ZylCbsRange ZYL_CBS_CHANNELS[] = {
    { 4352, 4359 },
    { 4370, 4399 },
    { 0, 0 }
};

ZylCbsCategory zyl_cbs_classify(unsigned ch) {
    if (ch >= 4352 && ch <= 4354) return ZYL_CBS_EARTHQUAKE;   /* earthquake, tsunami, both */
    if (ch == 4355) return ZYL_CBS_TEST;                        /* ETWS test */
    if (ch == 4356) return ZYL_CBS_EXTREME;                     /* other emergency */
    if (ch >= 4357 && ch <= 4359) return ZYL_CBS_EARTHQUAKE;   /* reserved ETWS */
    /* CMAS, each class twice: first range in the network's main language,
       second (4383+) in an additional language */
    if (ch == 4370 || ch == 4383) return ZYL_CBS_PRESIDENTIAL;
    if ((ch >= 4371 && ch <= 4372) || (ch >= 4384 && ch <= 4385)) return ZYL_CBS_EXTREME;
    if ((ch >= 4373 && ch <= 4378) || (ch >= 4386 && ch <= 4391)) return ZYL_CBS_SEVERE;
    if (ch == 4379 || ch == 4392) return ZYL_CBS_AMBER;
    if ((ch >= 4380 && ch <= 4382) || (ch >= 4393 && ch <= 4395)) return ZYL_CBS_TEST;
    if (ch >= 4396 && ch <= 4397) return ZYL_CBS_PUBLIC_SAFETY;
    if (ch >= 4398 && ch <= 4399) return ZYL_CBS_TEST;          /* state/local test */
    return ZYL_CBS_NONE;
}

const char *zyl_cbs_category_name(ZylCbsCategory c) {
    switch (c) {
    case ZYL_CBS_PRESIDENTIAL:  return "presidential";
    case ZYL_CBS_EXTREME:       return "extreme";
    case ZYL_CBS_SEVERE:        return "severe";
    case ZYL_CBS_AMBER:         return "amber";
    case ZYL_CBS_PUBLIC_SAFETY: return "public_safety";
    case ZYL_CBS_EARTHQUAKE:    return "earthquake";
    case ZYL_CBS_TEST:          return "test";
    default:                    return "";
    }
}

ZylCbsCategory zyl_cbs_category_from_name(const char *name) {
    for (int c = 0; c < ZYL_CBS_CATEGORIES; c++)
        if (name && strcmp(name, zyl_cbs_category_name((ZylCbsCategory)c)) == 0)
            return (ZylCbsCategory)c;
    return ZYL_CBS_NONE;
}

/* ─── History ─── */

struct ZylCbsStore {
    ZylCbsAlert items[ZYL_CBS_HISTORY_MAX];   /* newest first */
    int         count;
    uint64_t    next_id;
    char       *path;
    bool        enabled[ZYL_CBS_CATEGORIES];
};

static void store_save(const ZylCbsStore *s) {
    if (!s->path) return;
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_uint64(kf, "store", "next_id", s->next_id);
    for (int c = 0; c < ZYL_CBS_CATEGORIES; c++)
        g_key_file_set_boolean(kf, "enabled", zyl_cbs_category_name((ZylCbsCategory)c),
                               s->enabled[c]);
    for (int i = 0; i < s->count; i++) {
        const ZylCbsAlert *a = &s->items[i];
        char group[24];
        snprintf(group, sizeof(group), "alert%d", i);
        g_key_file_set_uint64(kf, group, "id", a->id);
        g_key_file_set_int64(kf, group, "at", a->at);
        g_key_file_set_integer(kf, group, "channel", (int)a->channel);
        g_key_file_set_integer(kf, group, "code", (int)a->code);
        g_key_file_set_integer(kf, group, "update", (int)a->update);
        g_key_file_set_boolean(kf, group, "acked", a->acked);
        g_key_file_set_string(kf, group, "text", a->text ? a->text : "");
    }
    char *dir = g_path_get_dirname(s->path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    GError *err = NULL;
    if (!g_key_file_save_to_file(kf, s->path, &err)) {    /* atomic replace */
        g_warning("[CBS] cannot save alert history: %s", err->message);
        g_error_free(err);
    }
    g_key_file_free(kf);
}

ZylCbsStore *zyl_cbs_store_open(const char *path) {
    ZylCbsStore *s = g_new0(ZylCbsStore, 1);
    s->next_id = 1;
    s->path = g_strdup(path);
    for (int c = 0; c < ZYL_CBS_CATEGORIES; c++) s->enabled[c] = (c != ZYL_CBS_TEST);
    if (!path) return s;

    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        guint64 next = g_key_file_get_uint64(kf, "store", "next_id", NULL);
        if (next) s->next_id = next;
        for (int c = 1; c < ZYL_CBS_CATEGORIES; c++) {   /* presidential stays on */
            const char *key = zyl_cbs_category_name((ZylCbsCategory)c);
            if (g_key_file_has_key(kf, "enabled", key, NULL))
                s->enabled[c] = g_key_file_get_boolean(kf, "enabled", key, NULL);
        }
        for (int i = 0; i < ZYL_CBS_HISTORY_MAX; i++) {
            char group[24];
            snprintf(group, sizeof(group), "alert%d", i);
            if (!g_key_file_has_group(kf, group)) break;
            ZylCbsAlert *a = &s->items[s->count++];
            a->id      = g_key_file_get_uint64(kf, group, "id", NULL);
            a->at      = g_key_file_get_int64(kf, group, "at", NULL);
            a->channel = (unsigned)g_key_file_get_integer(kf, group, "channel", NULL);
            a->code    = (unsigned)g_key_file_get_integer(kf, group, "code", NULL);
            a->update  = (unsigned)g_key_file_get_integer(kf, group, "update", NULL);
            a->acked   = g_key_file_get_boolean(kf, group, "acked", NULL);
            a->text    = g_key_file_get_string(kf, group, "text", NULL);
            a->category = zyl_cbs_classify(a->channel);
        }
    }
    g_key_file_free(kf);
    return s;
}

void zyl_cbs_store_free(ZylCbsStore *s) {
    if (!s) return;
    for (int i = 0; i < s->count; i++) g_free(s->items[i].text);
    g_free(s->path);
    g_free(s);
}

/* Networks repeat an alert for as long as it is in force; a phone that showed
   it each time would sound the siren again every few minutes — and again
   after every reboot, which is why this checks the saved history. A changed
   update number is a new version of the alert and is shown. */
#define REPEAT_WINDOW_S (24 * 3600)

const ZylCbsAlert *zyl_cbs_store_receive(ZylCbsStore *s, unsigned channel,
                                         unsigned code, unsigned update,
                                         const char *text, int64_t now) {
    ZylCbsCategory cat = zyl_cbs_classify(channel);
    if (cat == ZYL_CBS_NONE || !s->enabled[cat]) return NULL;
    for (int i = 0; i < s->count; i++) {
        const ZylCbsAlert *a = &s->items[i];
        if (a->channel == channel && a->code == code && a->update == update &&
            now - a->at < REPEAT_WINDOW_S)
            return NULL;
    }
    if (s->count == ZYL_CBS_HISTORY_MAX) g_free(s->items[--s->count].text);
    memmove(&s->items[1], &s->items[0], sizeof(ZylCbsAlert) * (size_t)s->count);
    s->count++;
    ZylCbsAlert *a = &s->items[0];
    a->id = s->next_id++;
    a->at = now;
    a->channel = channel;
    a->code = code;
    a->update = update;
    a->category = cat;
    a->text = g_strdup(text ? text : "");
    a->acked = false;
    store_save(s);
    return a;
}

bool zyl_cbs_store_ack(ZylCbsStore *s, uint64_t id) {
    for (int i = 0; i < s->count; i++) {
        if (s->items[i].id != id) continue;
        if (!s->items[i].acked) {
            s->items[i].acked = true;
            store_save(s);
        }
        return true;
    }
    return false;
}

bool zyl_cbs_store_enabled(const ZylCbsStore *s, ZylCbsCategory c) {
    return c > ZYL_CBS_NONE && c < ZYL_CBS_CATEGORIES && s->enabled[c];
}

bool zyl_cbs_store_set_enabled(ZylCbsStore *s, ZylCbsCategory c, bool on) {
    if (c <= ZYL_CBS_NONE || c >= ZYL_CBS_CATEGORIES) return false;
    if (c == ZYL_CBS_PRESIDENTIAL && !on) return false;
    if (s->enabled[c] != on) {
        s->enabled[c] = on;
        store_save(s);
    }
    return true;
}

int zyl_cbs_store_count(const ZylCbsStore *s) { return s ? s->count : 0; }

const ZylCbsAlert *zyl_cbs_store_get(const ZylCbsStore *s, int index) {
    return (s && index >= 0 && index < s->count) ? &s->items[index] : NULL;
}
