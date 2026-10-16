/* ----------------------------------------------------------
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 입력 서비스 구현 -- D-Bus로 가상 키보드/하드웨어 키 관리
 * 수행범위: Input HAL 의 키 누름을 HardwareKeyPressed 로, 키보드 상태 관리,
 *           D-Bus 메서드/시그널
 * 의존방향: input.h, hal.h (Input HAL), gio/gio.h
 * SOLID: SRP -- 입력 관리 로직만 담당
 * ---------------------------------------------------------- */

#include "input.h"
#include "hal.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* == D-Bus Introspection XML ================================ */

static const char *introspection_xml =
    "<node>"
    "  <interface name='" ZYL_INPUT_DBUS_IFACE "'>"
    "    <method name='ShowKeyboard'>"
    "      <arg direction='in'  type='s' name='layout'/>"
    "      <arg direction='out' type='b' name='success'/>"
    "    </method>"
    "    <method name='HideKeyboard'>"
    "      <arg direction='out' type='b' name='success'/>"
    "    </method>"
    "    <method name='SwitchLayout'>"
    "      <arg direction='in'  type='s' name='layout'/>"
    "      <arg direction='out' type='b' name='success'/>"
    "    </method>"
    "    <method name='GetState'>"
    "      <arg direction='out' type='b' name='visible'/>"
    "      <arg direction='out' type='s' name='layout'/>"
    "    </method>"
    "    <signal name='HardwareKeyPressed'>"
    "      <arg type='i' name='key_code'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* == Internal Service Structure ============================= */

struct ZylInputService {
    /* Keyboard state */
    ZylKeyboardState keyboard;

    /* The Input HAL's key devices: readable when one has input (-1 none) */
    int              keys_fd;
    guint            keys_watch_id;

    /* D-Bus */
    GDBusConnection *connection;
    GDBusNodeInfo   *introspection_data;
    guint            bus_owner_id;
    guint            registration_id;
};

/* == Forward Declarations =================================== */

static void handle_method_call(GDBusConnection       *connection,
                               const gchar           *sender,
                               const gchar           *object_path,
                               const gchar           *interface_name,
                               const gchar           *method_name,
                               GVariant              *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer               user_data);

static void on_bus_acquired(GDBusConnection *connection,
                            const gchar     *name,
                            gpointer         user_data);

static void on_name_acquired(GDBusConnection *connection,
                             const gchar     *name,
                             gpointer         user_data);

static void on_name_lost(GDBusConnection *connection,
                         const gchar     *name,
                         gpointer         user_data);

/* == Helper: Validate layout string ========================= */

static bool is_valid_layout(const char *layout)
{
    if (!layout || layout[0] == '\0') return false;

    static const char *known[] = {
        "en", "ko", "ja", "zh", "num",
        "de", "fr", "es", "it", "pt",    /* European */
        "ar", "he", "fa",                 /* RTL */
        "ru", "uk",                       /* Cyrillic */
        NULL
    };
    for (int i = 0; known[i]; i++) {
        if (strcmp(layout, known[i]) == 0) return true;
    }
    return false;
}

/* == Helper: Set layout safely ============================== */

static void set_layout(ZylKeyboardState *kb, const char *layout)
{
    if (!layout) return;
    snprintf(kb->layout, sizeof(kb->layout), "%s", layout);
}

/* == Helper: Emit D-Bus signal ============================== */

static void emit_signal(ZylInputService *svc,
                        const char      *signal_name,
                        GVariant        *params)
{
    if (!svc->connection) {                       /* no bus yet: params is floating, drop it */
        g_variant_unref(g_variant_ref_sink(params));
        return;
    }

    GError *err = NULL;
    g_dbus_connection_emit_signal(svc->connection,
                                 NULL,
                                 ZYL_INPUT_DBUS_PATH,
                                 ZYL_INPUT_DBUS_IFACE,
                                 signal_name,
                                 params,
                                 &err);
    if (err) {
        g_warning("zyl-input: failed to emit signal %s: %s",
                  signal_name, err->message);
        g_error_free(err);
    }
}

/* == Key devices ============================================ */

/* The D-Bus key_code is ZylHardwareKey; the HAL names the same keys its way */
static const ZylHardwareKey HW_KEY[] = {
    [ZYL_HAL_KEY_POWER]       = ZYL_KEY_POWER,
    [ZYL_HAL_KEY_VOLUME_UP]   = ZYL_KEY_VOLUME_UP,
    [ZYL_HAL_KEY_VOLUME_DOWN] = ZYL_KEY_VOLUME_DOWN,
    [ZYL_HAL_KEY_BACK]        = ZYL_KEY_BACK,
    [ZYL_HAL_KEY_HOME]        = ZYL_KEY_HOME,
    [ZYL_HAL_KEY_MENU]        = ZYL_KEY_MENU,
};

static gboolean on_keys_ready(gint fd, GIOCondition condition, gpointer user_data)
{
    (void)fd;
    ZylInputService *svc = user_data;

    /* One device going away is the HAL's to handle; this is the whole set broken */
    if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        g_warning("zyl-input: key devices error/hangup");
        svc->keys_watch_id = 0;
        return G_SOURCE_REMOVE;
    }

    ZylInputKey key;
    while (zyl_hal_input()->read_key(&key))
        zyl_input_on_hardware_key(svc, HW_KEY[key]);
    return G_SOURCE_CONTINUE;
}

/* == D-Bus Method Handler =================================== */

static void handle_show_keyboard(ZylInputService       *svc,
                                 GVariant              *parameters,
                                 GDBusMethodInvocation *invocation)
{
    const char *layout = NULL;
    g_variant_get(parameters, "(&s)", &layout);

    int result = zyl_input_show_keyboard(svc, layout);
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", result == 0));
}

static void handle_hide_keyboard(ZylInputService       *svc,
                                 GVariant              *parameters,
                                 GDBusMethodInvocation *invocation)
{
    (void)parameters;
    int result = zyl_input_hide_keyboard(svc);
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", result == 0));
}

static void handle_switch_layout(ZylInputService       *svc,
                                 GVariant              *parameters,
                                 GDBusMethodInvocation *invocation)
{
    const char *layout = NULL;
    g_variant_get(parameters, "(&s)", &layout);

    int result = zyl_input_switch_layout(svc, layout);
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", result == 0));
}

static void handle_get_state(ZylInputService       *svc,
                             GVariant              *parameters,
                             GDBusMethodInvocation *invocation)
{
    (void)parameters;
    ZylKeyboardState state = zyl_input_get_keyboard_state(svc);
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(bs)",
                                                        state.visible,
                                                        state.layout));
}

static void handle_method_call(GDBusConnection       *connection,
                               const gchar           *sender,
                               const gchar           *object_path,
                               const gchar           *interface_name,
                               const gchar           *method_name,
                               GVariant              *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer               user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;

    ZylInputService *svc = user_data;

    if (g_strcmp0(method_name, "ShowKeyboard") == 0) {
        handle_show_keyboard(svc, parameters, invocation);
    } else if (g_strcmp0(method_name, "HideKeyboard") == 0) {
        handle_hide_keyboard(svc, parameters, invocation);
    } else if (g_strcmp0(method_name, "SwitchLayout") == 0) {
        handle_switch_layout(svc, parameters, invocation);
    } else if (g_strcmp0(method_name, "GetState") == 0) {
        handle_get_state(svc, parameters, invocation);
    } else {
        g_dbus_method_invocation_return_dbus_error(
            invocation,
            "org.zylos.InputService.UnknownMethod",
            "Method not implemented");
    }
}

static const GDBusInterfaceVTable interface_vtable = {
    handle_method_call,
    NULL,
    NULL,
    {0}
};

/* == Bus Ownership Callbacks ================================ */

static void on_bus_acquired(GDBusConnection *connection,
                            const gchar     *name,
                            gpointer         user_data)
{
    (void)name;
    ZylInputService *svc = user_data;
    svc->connection = g_object_ref(connection);

    if (!svc->introspection_data || !svc->introspection_data->interfaces ||
        !svc->introspection_data->interfaces[0]) {
        g_warning("zyl-input: introspection data is NULL, cannot register object");
        return;
    }

    GError *err = NULL;
    svc->registration_id = g_dbus_connection_register_object(
        connection,
        ZYL_INPUT_DBUS_PATH,
        svc->introspection_data->interfaces[0],
        &interface_vtable,
        svc,
        NULL,
        &err);

    if (err) {
        g_warning("zyl-input: failed to register D-Bus object: %s", err->message);
        g_error_free(err);
    }
}

static void on_name_acquired(GDBusConnection *connection,
                             const gchar     *name,
                             gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_info("zyl-input: acquired D-Bus name %s", name);
}

static void on_name_lost(GDBusConnection *connection,
                         const gchar     *name,
                         gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_warning("zyl-input: lost D-Bus name %s", name);
}

/* == Public API -- Service Lifecycle ======================== */

ZylInputService *zyl_input_create(void)
{
    ZylInputService *svc = calloc(1, sizeof(ZylInputService));
    if (!svc) {
        g_critical("zyl-input: failed to allocate service");
        return NULL;
    }

    /* Initialize keyboard state */
    svc->keyboard.visible = false;
    snprintf(svc->keyboard.layout, sizeof(svc->keyboard.layout), "en");

    /* Every device with hardware keys: power, volume and the rest are often apart */
    svc->keys_fd = zyl_hal_input()->open();
    if (svc->keys_fd >= 0)
        svc->keys_watch_id = g_unix_fd_add(svc->keys_fd, G_IO_IN | G_IO_HUP | G_IO_ERR,
                                           on_keys_ready, svc);
    else
        g_info("zyl-input: no hardware key device (will operate without hardware keys)");

    /* Set up D-Bus */
    svc->introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    if (!svc->introspection_data) {
        g_critical("zyl-input: failed to parse introspection XML");
        zyl_input_destroy(svc);
        return NULL;
    }

    svc->bus_owner_id = g_bus_own_name(
        G_BUS_TYPE_SYSTEM,
        ZYL_INPUT_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        svc,
        NULL);

    return svc;
}

void zyl_input_destroy(ZylInputService *svc)
{
    if (!svc) return;

    if (svc->bus_owner_id > 0)
        g_bus_unown_name(svc->bus_owner_id);

    if (svc->registration_id > 0 && svc->connection) {
        g_dbus_connection_unregister_object(svc->connection,
                                            svc->registration_id);
    }

    if (svc->connection)
        g_object_unref(svc->connection);

    if (svc->introspection_data)
        g_dbus_node_info_unref(svc->introspection_data);

    if (svc->keys_watch_id > 0)
        g_source_remove(svc->keys_watch_id);

    if (svc->keys_fd >= 0)
        zyl_hal_input()->close();

    free(svc);
}

/* == Public API -- Virtual Keyboard ========================= */

int zyl_input_show_keyboard(ZylInputService *svc, const char *layout)
{
    if (!svc) return -1;

    if (layout && is_valid_layout(layout)) {
        set_layout(&svc->keyboard, layout);
    }
    svc->keyboard.visible = true;

    g_info("zyl-input: keyboard shown (layout=%s)", svc->keyboard.layout);
    return 0;
}

int zyl_input_hide_keyboard(ZylInputService *svc)
{
    if (!svc) return -1;

    svc->keyboard.visible = false;
    g_info("zyl-input: keyboard hidden");
    return 0;
}

int zyl_input_switch_layout(ZylInputService *svc, const char *layout)
{
    if (!svc || !layout) return -1;

    if (!is_valid_layout(layout)) {
        g_warning("zyl-input: unknown layout '%s'", layout);
        return -1;
    }

    set_layout(&svc->keyboard, layout);
    g_info("zyl-input: switched to layout '%s'", svc->keyboard.layout);
    return 0;
}

ZylKeyboardState zyl_input_get_keyboard_state(const ZylInputService *svc)
{
    ZylKeyboardState state = {0};
    if (!svc) return state;
    return svc->keyboard;
}

/* == Public API -- Hardware Keys ============================ */

void zyl_input_on_hardware_key(ZylInputService *svc, ZylHardwareKey key)
{
    if (!svc) return;

    g_info("zyl-input: hardware key pressed: %d", (int)key);

    /* Emit D-Bus signal so other services can react */
    emit_signal(svc, "HardwareKeyPressed",
                g_variant_new("(i)", (gint32)key));
}

/* == Entry Point ============================================ */

#ifndef ZYL_TEST
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    g_info("zyl-input: starting service");

    ZylInputService *svc = zyl_input_create();
    if (!svc) {
        g_critical("zyl-input: failed to create service");
        return 1;
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    /* Cleanup (reached on shutdown) */
    g_main_loop_unref(loop);
    zyl_input_destroy(svc);

    return 0;
}
#endif /* ZYL_TEST */
