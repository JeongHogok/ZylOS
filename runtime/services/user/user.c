/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 멀티유저 관리 서비스 — 계정 CRUD, 프로필 전환, 데이터 격리
 * 수행범위: /data/users/ 디렉토리 관리, D-Bus 인터페이스, 사용자 전환
 * 의존방향: user.h, gio/gio.h
 * SOLID: SRP — 사용자 관리 로직만 담당
 * ────────────────────────────────────────────────────────── */

#include "user.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <gio/gio.h>

struct ZylUserService {
    ZylUserProfile users[ZYL_USER_MAX];
    int user_count;
    uint32_t current_uid;
    uint32_t next_uid;
    GDBusConnection *dbus;
    guint dbus_owner_id;
};

static bool ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode);
    return mkdir(path, 0700) == 0;
}

/* ─── 사용자 데이터 디렉토리 생성 ─── */
static bool create_user_data(uint32_t uid) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%u", ZYL_USER_DATA_ROOT, uid);
    if (!ensure_dir(path)) return false;

    /* 앱 데이터, 다운로드, 설정 서브디렉토리 */
    char sub[300];
    snprintf(sub, sizeof(sub), "%s/apps", path);
    ensure_dir(sub);
    snprintf(sub, sizeof(sub), "%s/downloads", path);
    ensure_dir(sub);
    snprintf(sub, sizeof(sub), "%s/settings", path);
    ensure_dir(sub);

    return true;
}

/* ─── D-Bus 인트로스펙션 ─── */
static const char *user_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_USER_DBUS_NAME "'>"
    "    <method name='AddUser'>"
    "      <arg type='s' name='name' direction='in'/>"
    "      <arg type='i' name='type' direction='in'/>"
    "      <arg type='u' name='uid' direction='out'/>"
    "    </method>"
    "    <method name='RemoveUser'>"
    "      <arg type='u' name='uid' direction='in'/>"
    "    </method>"
    "    <method name='SwitchUser'>"
    "      <arg type='u' name='uid' direction='in'/>"
    "    </method>"
    "    <method name='GetCurrent'>"
    "      <arg type='u' name='uid' direction='out'/>"
    "    </method>"
    "    <method name='ListUsers'>"
    "      <arg type='a(ussi)' name='users' direction='out'/>"
    "    </method>"
    "    <signal name='UserSwitched'>"
    "      <arg type='u' name='old_uid'/>"
    "      <arg type='u' name='new_uid'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static void handle_user_method(GDBusConnection *conn, const gchar *sender,
                                const gchar *path, const gchar *iface,
                                const gchar *method, GVariant *params,
                                GDBusMethodInvocation *inv, gpointer data) {
    ZylUserService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "AddUser") == 0) {
        const gchar *name;
        gint32 type;
        g_variant_get(params, "(&si)", &name, &type);
        uint32_t uid = 0;
        int ret = zyl_user_add(svc, name, (ZylUserType)type, &uid);
        if (ret == 0) {
            g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", uid));
        } else {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                G_DBUS_ERROR_FAILED, "Failed to add user");
        }
    } else if (g_strcmp0(method, "RemoveUser") == 0) {
        guint32 uid;
        g_variant_get(params, "(u)", &uid);
        zyl_user_remove(svc, uid);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "SwitchUser") == 0) {
        guint32 uid;
        g_variant_get(params, "(u)", &uid);
        zyl_user_switch(svc, uid);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "GetCurrent") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(u)", svc->current_uid));
    } else if (g_strcmp0(method, "ListUsers") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ussi)"));
        for (int i = 0; i < svc->user_count; i++) {
            ZylUserProfile *u = &svc->users[i];
            g_variant_builder_add(&builder, "(ussi)",
                u->uid, u->name, u->data_dir, (gint32)u->type);
        }
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(a(ussi))", &builder));
    }
}

static const GDBusInterfaceVTable user_vtable = { .method_call = handle_user_method };

static void on_user_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    ZylUserService *svc = data;
    svc->dbus = conn;

    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(user_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, ZYL_USER_DBUS_PATH,
            info->interfaces[0], &user_vtable, svc, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_message("[User] D-Bus registered: %s", ZYL_USER_DBUS_NAME);
}

/* ─── 공개 API ─── */

ZylUserService *zyl_user_create(void) {
    ZylUserService *svc = calloc(1, sizeof(ZylUserService));
    if (!svc) return NULL;

    svc->next_uid = 1000;
    ensure_dir(ZYL_USER_DATA_ROOT);

    /* 기본 소유자 계정 생성 */
    uint32_t owner_uid = 0;
    zyl_user_add(svc, "Owner", ZYL_USER_TYPE_OWNER, &owner_uid);
    svc->current_uid = owner_uid;
    svc->users[0].is_active = true;

    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
        ZYL_USER_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_user_bus, NULL, NULL, svc, NULL);

    return svc;
}

void zyl_user_destroy(ZylUserService *svc) {
    if (!svc) return;
    g_bus_unown_name(svc->dbus_owner_id);
    free(svc);
}

int zyl_user_add(ZylUserService *svc, const char *name, ZylUserType type,
                  uint32_t *out_uid) {
    if (!svc || !name || svc->user_count >= ZYL_USER_MAX) return -1;

    /* 소유자는 1명만 */
    if (type == ZYL_USER_TYPE_OWNER) {
        for (int i = 0; i < svc->user_count; i++) {
            if (svc->users[i].type == ZYL_USER_TYPE_OWNER) return -1;
        }
    }

    ZylUserProfile *u = &svc->users[svc->user_count];
    u->uid = svc->next_uid++;
    snprintf(u->name, sizeof(u->name), "%s", name);
    u->type = type;
    u->created_at = (uint64_t)time(NULL);
    u->last_login_at = 0;
    u->is_active = false;
    snprintf(u->data_dir, sizeof(u->data_dir), "%s/%u", ZYL_USER_DATA_ROOT, u->uid);

    if (!create_user_data(u->uid)) {
        return -1;
    }

    svc->user_count++;
    if (out_uid) *out_uid = u->uid;
    g_message("[User] Added: %s (uid=%u, type=%d)", name, u->uid, type);
    return 0;
}

int zyl_user_remove(ZylUserService *svc, uint32_t uid) {
    if (!svc) return -1;

    for (int i = 0; i < svc->user_count; i++) {
        if (svc->users[i].uid == uid) {
            /* 소유자 삭제 불가 */
            if (svc->users[i].type == ZYL_USER_TYPE_OWNER) return -1;
            /* 현재 사용자 삭제 불가 */
            if (uid == svc->current_uid) return -1;

            g_message("[User] Removed: %s (uid=%u)", svc->users[i].name, uid);

            /* 배열에서 제거 (마지막 요소로 교체) */
            svc->users[i] = svc->users[--svc->user_count];
            return 0;
        }
    }
    return -1;
}

int zyl_user_get_profile(ZylUserService *svc, uint32_t uid, ZylUserProfile *out) {
    if (!svc || !out) return -1;
    for (int i = 0; i < svc->user_count; i++) {
        if (svc->users[i].uid == uid) {
            *out = svc->users[i];
            return 0;
        }
    }
    return -1;
}

int zyl_user_list(ZylUserService *svc, ZylUserProfile **out, int *count) {
    if (!svc || !out || !count) return -1;
    *out = svc->users;
    *count = svc->user_count;
    return 0;
}

int zyl_user_switch(ZylUserService *svc, uint32_t uid) {
    if (!svc) return -1;

    /* 대상 사용자 찾기 */
    int target = -1;
    for (int i = 0; i < svc->user_count; i++) {
        if (svc->users[i].uid == uid) { target = i; break; }
    }
    if (target < 0) return -1;

    uint32_t old_uid = svc->current_uid;

    /* 이전 사용자 비활성화 */
    for (int i = 0; i < svc->user_count; i++) {
        if (svc->users[i].uid == old_uid) svc->users[i].is_active = false;
    }

    /* 새 사용자 활성화 */
    svc->users[target].is_active = true;
    svc->users[target].last_login_at = (uint64_t)time(NULL);
    svc->current_uid = uid;

    /* D-Bus 시그널 */
    if (svc->dbus) {
        g_dbus_connection_emit_signal(svc->dbus, NULL,
            ZYL_USER_DBUS_PATH, ZYL_USER_DBUS_NAME,
            "UserSwitched", g_variant_new("(uu)", old_uid, uid), NULL);
    }

    g_message("[User] Switched: %u → %u", old_uid, uid);
    return 0;
}

uint32_t zyl_user_get_current(const ZylUserService *svc) {
    return svc ? svc->current_uid : 0;
}

int zyl_user_get_app_data_path(const ZylUserService *svc, uint32_t uid,
                                const char *app_id, char *out, size_t out_len) {
    if (!svc || !app_id || !out || out_len == 0) return -1;
    snprintf(out, out_len, "%s/%u/apps/%s", ZYL_USER_DATA_ROOT, uid, app_id);
    return 0;
}

/* ─── 데몬 진입점 ─── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    ZylUserService *svc = zyl_user_create();
    if (!svc) {
        g_critical("[User] Failed to create service");
        return 1;
    }

    g_message("[User] Zyl OS User Manager started");
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    zyl_user_destroy(svc);
    return 0;
}
