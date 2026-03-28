/*
 * BPI-OS Web Application Manager (WAM)
 *
 * WebKitGTK 기반으로 웹 앱을 관리하는 데몬.
 * 각 앱은 독립된 WebKitWebView에서 실행되며,
 * D-Bus를 통해 컴포지터 및 시스템 서비스와 통신한다.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <json-glib/json-glib.h>

#define WAM_APP_DIR     "/usr/share/bpi-os/apps"
#define WAM_USER_DIR    "/home/user/.bpi-os/apps"
#define WAM_DBUS_NAME   "org.bpios.WebAppManager"
#define WAM_DBUS_PATH   "/org/bpios/WebAppManager"

/* ─── 앱 매니페스트 ─── */
typedef struct {
    char *id;           /* 고유 앱 ID (예: "com.bpios.home") */
    char *name;         /* 표시 이름 */
    char *version;      /* 버전 */
    char *entry;        /* 진입점 HTML 파일 */
    char *icon;         /* 아이콘 경로 */
    char *base_path;    /* 앱 디렉토리 경로 */
    char **permissions; /* 요청 권한 목록 */
    int n_permissions;
    gboolean is_system; /* 시스템 앱 여부 */
} BpiAppManifest;

/* ─── 실행 중인 앱 인스턴스 ─── */
typedef struct {
    BpiAppManifest *manifest;
    GtkWidget *window;
    WebKitWebView *webview;
    gboolean is_visible;
    gboolean is_suspended;
} BpiAppInstance;

/* ─── WAM 서버 ─── */
typedef struct {
    GApplication *app;
    GDBusConnection *dbus;
    GHashTable *manifests;   /* id → BpiAppManifest* */
    GHashTable *instances;   /* id → BpiAppInstance* */
    guint dbus_owner_id;
} BpiWam;

static BpiWam *g_wam = NULL;

/* ─── 매니페스트 파싱 ─── */
static BpiAppManifest *parse_manifest(const char *app_dir) {
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/app.json", app_dir);

    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (!json_parser_load_from_file(parser, manifest_path, &error)) {
        g_warning("Failed to parse %s: %s", manifest_path, error->message);
        g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);

    BpiAppManifest *m = g_new0(BpiAppManifest, 1);
    m->id = g_strdup(json_object_get_string_member(obj, "id"));
    m->name = g_strdup(json_object_get_string_member(obj, "name"));
    m->version = g_strdup(json_object_get_string_member_with_default(
        obj, "version", "1.0.0"));
    m->entry = g_strdup(json_object_get_string_member_with_default(
        obj, "entry", "index.html"));
    m->icon = g_strdup(json_object_get_string_member_with_default(
        obj, "icon", "icon.png"));
    m->base_path = g_strdup(app_dir);

    /* 권한 파싱 */
    if (json_object_has_member(obj, "permissions")) {
        JsonArray *perms = json_object_get_array_member(obj, "permissions");
        m->n_permissions = json_array_get_length(perms);
        m->permissions = g_new0(char *, m->n_permissions + 1);
        for (int i = 0; i < m->n_permissions; i++) {
            m->permissions[i] = g_strdup(
                json_array_get_string_element(perms, i));
        }
    }

    g_object_unref(parser);
    return m;
}

/* ─── 앱 디렉토리 스캔 ─── */
static void scan_apps(BpiWam *wam, const char *base_dir, gboolean is_system) {
    DIR *dir = opendir(base_dir);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char app_dir[512];
        snprintf(app_dir, sizeof(app_dir), "%s/%s", base_dir, entry->d_name);

        BpiAppManifest *manifest = parse_manifest(app_dir);
        if (manifest) {
            manifest->is_system = is_system;
            g_hash_table_insert(wam->manifests, g_strdup(manifest->id), manifest);
            g_message("Registered app: %s (%s)", manifest->name, manifest->id);
        }
    }
    closedir(dir);
}

/* ─── JS Bridge 스크립트 주입 ─── */
static void inject_bridge_script(WebKitWebView *webview,
                                 BpiAppManifest *manifest) {
    /* JS-Native Bridge를 전역 navigator.system 객체로 주입 */
    const char *bridge_script =
        "window.navigator.system = {"
        "  app: {"
        "    id: '%s',"
        "    name: '%s',"
        "    version: '%s',"
        "    close: function() {"
        "      window.webkit.messageHandlers.bridge.postMessage("
        "        JSON.stringify({type: 'app.close', appId: '%s'})"
        "      );"
        "    },"
        "    minimize: function() {"
        "      window.webkit.messageHandlers.bridge.postMessage("
        "        JSON.stringify({type: 'app.minimize', appId: '%s'})"
        "      );"
        "    }"
        "  },"
        "  launch: function(appId) {"
        "    window.webkit.messageHandlers.bridge.postMessage("
        "      JSON.stringify({type: 'app.launch', appId: appId})"
        "    );"
        "  },"
        "  notification: {"
        "    create: function(title, body, opts) {"
        "      window.webkit.messageHandlers.bridge.postMessage("
        "        JSON.stringify({type: 'notification.create',"
        "          title: title, body: body, options: opts || {}})"
        "      );"
        "    }"
        "  },"
        "  battery: {"
        "    getLevel: function() {"
        "      return new Promise(function(resolve) {"
        "        window.webkit.messageHandlers.bridge.postMessage("
        "          JSON.stringify({type: 'battery.getLevel', _cbId: "
        "            (window._bpiCbId = (window._bpiCbId||0)+1)})"
        "        );"
        "        window['_bpiCb_' + window._bpiCbId] = resolve;"
        "      });"
        "    }"
        "  },"
        "  wifi: {"
        "    scan: function() {"
        "      return new Promise(function(resolve) {"
        "        window.webkit.messageHandlers.bridge.postMessage("
        "          JSON.stringify({type: 'wifi.scan', _cbId: "
        "            (window._bpiCbId = (window._bpiCbId||0)+1)})"
        "        );"
        "        window['_bpiCb_' + window._bpiCbId] = resolve;"
        "      });"
        "    }"
        "  }"
        "};";

    char *script = g_strdup_printf(bridge_script,
        manifest->id, manifest->name, manifest->version,
        manifest->id, manifest->id);

    WebKitUserContentManager *ucm =
        webkit_web_view_get_user_content_manager(webview);
    WebKitUserScript *user_script = webkit_user_script_new(
        script,
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        NULL, NULL);
    webkit_user_content_manager_add_script(ucm, user_script);
    webkit_user_script_unref(user_script);
    g_free(script);
}

/* ─── Bridge 메시지 핸들러 ─── */
static void on_bridge_message(WebKitUserContentManager *manager,
                              WebKitJavascriptResult *result,
                              gpointer user_data) {
    BpiAppInstance *instance = user_data;
    JSCValue *value = webkit_javascript_result_get_js_value(result);
    char *msg_str = jsc_value_to_string(value);

    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, msg_str, -1, NULL)) {
        JsonObject *msg = json_node_get_object(json_parser_get_root(parser));
        const char *type = json_object_get_string_member(msg, "type");

        g_message("Bridge message from %s: %s",
                  instance->manifest->id, type);

        if (g_strcmp0(type, "app.close") == 0) {
            gtk_window_close(GTK_WINDOW(instance->window));
        } else if (g_strcmp0(type, "app.launch") == 0) {
            const char *target = json_object_get_string_member(msg, "appId");
            /* TODO: D-Bus로 앱 매니저에게 실행 요청 */
            g_message("Launch request: %s", target);
        } else if (g_strcmp0(type, "notification.create") == 0) {
            const char *title = json_object_get_string_member(msg, "title");
            const char *body = json_object_get_string_member(msg, "body");
            /* TODO: D-Bus로 알림 매니저에게 전달 */
            g_message("Notification: %s - %s", title, body);
        }
    }

    g_object_unref(parser);
    g_free(msg_str);
}

/* ─── 앱 실행 ─── */
static BpiAppInstance *launch_app(BpiWam *wam, const char *app_id) {
    /* 이미 실행 중이면 활성화 */
    BpiAppInstance *existing = g_hash_table_lookup(wam->instances, app_id);
    if (existing) {
        gtk_window_present(GTK_WINDOW(existing->window));
        existing->is_visible = TRUE;
        existing->is_suspended = FALSE;
        return existing;
    }

    BpiAppManifest *manifest = g_hash_table_lookup(wam->manifests, app_id);
    if (!manifest) {
        g_warning("App not found: %s", app_id);
        return NULL;
    }

    BpiAppInstance *instance = g_new0(BpiAppInstance, 1);
    instance->manifest = manifest;

    /* GTK 윈도우 (Wayland toplevel이 됨) */
    instance->window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(instance->window), manifest->name);
    gtk_window_set_decorated(GTK_WINDOW(instance->window), FALSE);
    gtk_window_fullscreen(GTK_WINDOW(instance->window));

    /* WebKit 설정 */
    WebKitSettings *settings = webkit_settings_new();
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_developer_extras(settings, FALSE);
    webkit_settings_set_hardware_acceleration_policy(settings,
        WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS);
    webkit_settings_set_allow_file_access_from_file_urls(settings, TRUE);

    /* UserContentManager + Bridge */
    WebKitUserContentManager *ucm = webkit_user_content_manager_new();
    g_signal_connect(ucm, "script-message-received::bridge",
                     G_CALLBACK(on_bridge_message), instance);
    webkit_user_content_manager_register_script_message_handler(ucm, "bridge");

    /* WebView 생성 */
    instance->webview = WEBKIT_WEB_VIEW(
        webkit_web_view_new_with_user_content_manager(ucm));
    webkit_web_view_set_settings(instance->webview, settings);

    /* JS Bridge 주입 */
    inject_bridge_script(instance->webview, manifest);

    /* 윈도우에 WebView 추가 */
    gtk_window_set_child(GTK_WINDOW(instance->window),
                         GTK_WIDGET(instance->webview));

    /* 앱 로드 */
    char entry_uri[1024];
    snprintf(entry_uri, sizeof(entry_uri), "file://%s/%s",
             manifest->base_path, manifest->entry);
    webkit_web_view_load_uri(instance->webview, entry_uri);

    instance->is_visible = TRUE;
    instance->is_suspended = FALSE;

    gtk_window_present(GTK_WINDOW(instance->window));

    g_hash_table_insert(wam->instances, g_strdup(app_id), instance);
    g_message("Launched app: %s (%s)", manifest->name, app_id);

    g_object_unref(settings);
    return instance;
}

/* ─── 앱 일시정지 (메모리 절약) ─── */
static void suspend_app(BpiWam *wam, const char *app_id) {
    BpiAppInstance *instance = g_hash_table_lookup(wam->instances, app_id);
    if (!instance || instance->is_suspended) return;

    /* WebKit의 page visibility 를 hidden으로 설정하여
       JS 타이머, 애니메이션 등을 멈춤 */
    instance->is_suspended = TRUE;
    instance->is_visible = FALSE;
    g_message("Suspended app: %s", app_id);
}

/* ─── 앱 종료 ─── */
static void close_app(BpiWam *wam, const char *app_id) {
    BpiAppInstance *instance = g_hash_table_lookup(wam->instances, app_id);
    if (!instance) return;

    gtk_window_close(GTK_WINDOW(instance->window));
    g_hash_table_remove(wam->instances, app_id);
    g_free(instance);
    g_message("Closed app: %s", app_id);
}

/* ─── D-Bus 메서드 핸들러 ─── */
static const char *dbus_introspection_xml =
    "<node>"
    "  <interface name='org.bpios.WebAppManager'>"
    "    <method name='Launch'>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='Close'>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "    </method>"
    "    <method name='Suspend'>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "    </method>"
    "    <method name='ListApps'>"
    "      <arg type='as' name='app_ids' direction='out'/>"
    "    </method>"
    "    <method name='ListRunning'>"
    "      <arg type='as' name='app_ids' direction='out'/>"
    "    </method>"
    "    <signal name='AppLaunched'>"
    "      <arg type='s' name='app_id'/>"
    "    </signal>"
    "    <signal name='AppClosed'>"
    "      <arg type='s' name='app_id'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static void handle_dbus_method(GDBusConnection *connection,
                               const gchar *sender,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *method_name,
                               GVariant *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer user_data) {
    BpiWam *wam = user_data;

    if (g_strcmp0(method_name, "Launch") == 0) {
        const gchar *app_id;
        g_variant_get(parameters, "(&s)", &app_id);
        BpiAppInstance *inst = launch_app(wam, app_id);
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(b)", inst != NULL));

    } else if (g_strcmp0(method_name, "Close") == 0) {
        const gchar *app_id;
        g_variant_get(parameters, "(&s)", &app_id);
        close_app(wam, app_id);
        g_dbus_method_invocation_return_value(invocation, NULL);

    } else if (g_strcmp0(method_name, "Suspend") == 0) {
        const gchar *app_id;
        g_variant_get(parameters, "(&s)", &app_id);
        suspend_app(wam, app_id);
        g_dbus_method_invocation_return_value(invocation, NULL);

    } else if (g_strcmp0(method_name, "ListApps") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        GHashTableIter iter;
        gpointer key;
        g_hash_table_iter_init(&iter, wam->manifests);
        while (g_hash_table_iter_next(&iter, &key, NULL))
            g_variant_builder_add(&builder, "s", (const char *)key);
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(as)", &builder));

    } else if (g_strcmp0(method_name, "ListRunning") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        GHashTableIter iter;
        gpointer key;
        g_hash_table_iter_init(&iter, wam->instances);
        while (g_hash_table_iter_next(&iter, &key, NULL))
            g_variant_builder_add(&builder, "s", (const char *)key);
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(as)", &builder));
    }
}

static const GDBusInterfaceVTable dbus_vtable = {
    .method_call = handle_dbus_method,
};

/* ─── D-Bus 이름 획득 ─── */
static void on_bus_acquired(GDBusConnection *connection,
                            const gchar *name, gpointer user_data) {
    BpiWam *wam = user_data;
    wam->dbus = connection;

    GDBusNodeInfo *node_info =
        g_dbus_node_info_new_for_xml(dbus_introspection_xml, NULL);

    g_dbus_connection_register_object(connection,
        WAM_DBUS_PATH,
        node_info->interfaces[0],
        &dbus_vtable,
        wam, NULL, NULL);

    g_dbus_node_info_unref(node_info);
    g_message("D-Bus registered: %s", WAM_DBUS_NAME);
}

static void on_name_acquired(GDBusConnection *conn, const gchar *name,
                             gpointer data) {
    g_message("D-Bus name acquired: %s", name);
}

static void on_name_lost(GDBusConnection *conn, const gchar *name,
                         gpointer data) {
    g_warning("D-Bus name lost: %s", name);
}

/* ─── GTK activate ─── */
static void on_activate(GApplication *app, gpointer user_data) {
    BpiWam *wam = user_data;

    /* 앱 스캔 */
    scan_apps(wam, WAM_APP_DIR, TRUE);
    scan_apps(wam, WAM_USER_DIR, FALSE);

    /* 홈스크린 자동 실행 */
    launch_app(wam, "com.bpios.home");

    g_message("WAM activated, %d apps registered",
              g_hash_table_size(wam->manifests));
}

/* ─── 매니페스트 해제 ─── */
static void free_manifest(gpointer data) {
    BpiAppManifest *m = data;
    g_free(m->id);
    g_free(m->name);
    g_free(m->version);
    g_free(m->entry);
    g_free(m->icon);
    g_free(m->base_path);
    if (m->permissions) g_strfreev(m->permissions);
    g_free(m);
}

/* ─── 메인 ─── */
int main(int argc, char *argv[]) {
    BpiWam wam = {0};
    g_wam = &wam;

    wam.manifests = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          g_free, free_manifest);
    wam.instances = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          g_free, NULL);

    /* D-Bus 등록 */
    wam.dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
        WAM_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired, on_name_acquired, on_name_lost,
        &wam, NULL);

    /* GTK 앱 시작 */
    wam.app = G_APPLICATION(gtk_application_new("org.bpios.wam",
        G_APPLICATION_DEFAULT_FLAGS));
    g_signal_connect(wam.app, "activate", G_CALLBACK(on_activate), &wam);

    int status = g_application_run(wam.app, argc, argv);

    g_bus_unown_name(wam.dbus_owner_id);
    g_hash_table_destroy(wam.manifests);
    g_hash_table_destroy(wam.instances);
    g_object_unref(wam.app);

    return status;
}
