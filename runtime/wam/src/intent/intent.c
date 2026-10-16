/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Service
 *
 * 역할: 인텐트 해석 (intent.h) — apps/system/intent.js 와 같은 규칙
 * 수행범위: MIME 일치, 처리 앱 찾기(PICK 은 시스템 앱만), 넘길 내용 정리(건넬
 *           파일 포함), 메시지 스크립트
 * 의존방향: intent.h
 * SOLID: SRP — 해석만
 * ────────────────────────────────────────────────────────── */

#include "intent.h"
#include <string.h>

gboolean zyl_intent_mime_matches(const char *filter, const char *mime) {
    if (!filter || !mime) return FALSE;
    if (strcmp(filter, "*/*") == 0 || strcmp(filter, mime) == 0) return TRUE;
    const char *slash = strchr(filter, '/');
    if (!slash || slash == filter || strcmp(slash, "/*") != 0) return FALSE;
    size_t n = (size_t)(slash - filter);
    return strncmp(filter, mime, n) == 0 && mime[n] == '/';
}

static const char *member_string(JsonObject *o, const char *name) {
    JsonNode *n = o && json_object_has_member(o, name) ? json_object_get_member(o, name) : NULL;
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_STRING
           ? json_node_get_string(n) : NULL;
}

static gint by_id(gconstpointer a, gconstpointer b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

GPtrArray *zyl_intent_resolve(GHashTable *manifests, const char *source, JsonObject *intent) {
    GPtrArray *out = g_ptr_array_new();
    const char *action = member_string(intent, "action");
    const char *mime = member_string(intent, "mimeType");
    gboolean pick = g_strcmp0(action, ZYL_INTENT_PICK) == 0;
    const char *target = member_string(intent, "targetApp");
    if (target) {
        ZylAppManifest *m = g_hash_table_lookup(manifests, target);
        if (m && (!pick || m->is_system)) g_ptr_array_add(out, m->id);
        return out;
    }
    if (!action) return out;
    if (!mime || !*mime) mime = "*/*";
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, manifests);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        ZylAppManifest *m = value;
        if ((source && strcmp(m->id, source) == 0) || (pick && !m->is_system)) continue;
        for (int i = 0; m->intent_actions && m->intent_actions[i]; i++) {
            if (strcmp(m->intent_actions[i], action) == 0 &&
                zyl_intent_mime_matches(m->intent_mimes[i], mime)) {
                g_ptr_array_add(out, m->id);
                break;
            }
        }
    }
    g_ptr_array_sort(out, by_id);
    return out;
}

JsonObject *zyl_intent_deliverable(JsonObject *intent, const char *source) {
    JsonObject *d = json_object_new();
    const char *action = member_string(intent, "action");
    const char *data = member_string(intent, "data");
    const char *mime = member_string(intent, "mimeType");
    json_object_set_string_member(d, "action", action ? action : "");
    json_object_set_string_member(d, "data", data ? data : "");
    json_object_set_string_member(d, "mimeType", mime ? mime : "");
    JsonNode *extras = json_object_has_member(intent, "extras") ? json_object_get_member(intent, "extras") : NULL;
    if (extras && JSON_NODE_HOLDS_OBJECT(extras)) json_object_set_member(d, "extras", json_node_copy(extras));
    else json_object_set_object_member(d, "extras", json_object_new());
    json_object_set_array_member(d, "files", zyl_intent_files(intent));
    json_object_set_string_member(d, "sourceApp", source ? source : "");
    return d;
}

JsonArray *zyl_intent_files(JsonObject *o) {
    JsonArray *out = json_array_new();
    JsonNode *n = o && json_object_has_member(o, "files") ? json_object_get_member(o, "files") : NULL;
    JsonArray *in = n && JSON_NODE_HOLDS_ARRAY(n) ? json_node_get_array(n) : NULL;
    for (guint i = 0; in && i < json_array_get_length(in) && json_array_get_length(out) < ZYL_INTENT_MAX_FILES; i++) {
        JsonNode *e = json_array_get_element(in, i);
        if (JSON_NODE_HOLDS_VALUE(e) && json_node_get_value_type(e) == G_TYPE_STRING && *json_node_get_string(e))
            json_array_add_string_element(out, json_node_get_string(e));
    }
    return out;
}

JsonNode *zyl_intent_object_node(JsonObject *obj) {
    JsonNode *n = json_node_init_object(json_node_alloc(), obj);
    json_object_unref(obj);
    return n;
}

char *zyl_intent_post_script(const char *type, JsonNode *data) {
    JsonObject *msg = json_object_new();
    json_object_set_string_member(msg, "type", type);
    json_object_set_member(msg, "data", data);
    JsonNode *root = json_node_init_object(json_node_alloc(), msg);
    gchar *json = json_to_string(root, FALSE);
    /* The JSON text itself, quoted as a JS string literal */
    JsonNode *lit = json_node_init_string(json_node_alloc(), json);
    gchar *quoted = json_to_string(lit, FALSE);
    char *script = g_strdup_printf("window.postMessage(%s,'*')", quoted);
    g_free(quoted);
    json_node_unref(lit);
    g_free(json);
    json_node_unref(root);
    json_object_unref(msg);
    return script;
}
