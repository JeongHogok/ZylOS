/* ----------------------------------------------------------
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 입력 서비스 구현 -- D-Bus로 가상 키보드/터치/하드웨어 키 관리
 * 수행범위: evdev 입력 읽기, 터치 포인트 추적, 키보드 상태 관리, D-Bus 메서드/시그널
 * 의존방향: input.h, gio/gio.h, linux/input.h
 * SOLID: SRP -- 입력 관리 로직만 담당
 * ---------------------------------------------------------- */

#include "input.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

    /* Multi-touch tracking */
    ZylTouchPoint    touch_points[ZYL_MAX_TOUCH_POINTS];

    /* evdev file descriptor (-1 if not opened) */
    int              evdev_fd;

    /* GLib I/O channel for evdev */
    GIOChannel      *evdev_channel;
    guint            evdev_watch_id;

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

    static const char *known[] = {"en", "ko", "ja", "zh", "num", NULL};
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
    if (!svc->connection) return;

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

/* == Helper: Map evdev key code to ZylHardwareKey =========== */

static bool evdev_to_hardware_key(int evdev_code, ZylHardwareKey *out)
{
    switch (evdev_code) {
    case KEY_POWER:      *out = ZYL_KEY_POWER;       return true;
    case KEY_VOLUMEUP:   *out = ZYL_KEY_VOLUME_UP;   return true;
    case KEY_VOLUMEDOWN: *out = ZYL_KEY_VOLUME_DOWN;  return true;
    case KEY_BACK:       *out = ZYL_KEY_BACK;         return true;
    case KEY_HOMEPAGE:   *out = ZYL_KEY_HOME;         return true;
    case KEY_MENU:       *out = ZYL_KEY_MENU;         return true;
    default:             return false;
    }
}

/* == evdev I/O callback ===================================== */

static gboolean on_evdev_event(GIOChannel  *source,
                               GIOCondition condition,
                               gpointer     user_data)
{
    (void)source;
    ZylInputService *svc = user_data;

    if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        g_warning("zyl-input: evdev channel error/hangup");
        svc->evdev_watch_id = 0;
        return G_SOURCE_REMOVE;
    }

    struct input_event ev;
    ssize_t n = read(svc->evdev_fd, &ev, sizeof(ev));
    if (n < (ssize_t)sizeof(ev)) return G_SOURCE_CONTINUE;

    /* Handle key press events (value == 1 means press) */
    if (ev.type == EV_KEY && ev.value == 1) {
        ZylHardwareKey hw_key;
        if (evdev_to_hardware_key(ev.code, &hw_key)) {
            zyl_input_on_hardware_key(svc, hw_key);
        }
    }

    /* Handle multi-touch events (ABS_MT_SLOT, ABS_MT_POSITION_X/Y, ABS_MT_TRACKING_ID) */
    if (ev.type == EV_ABS) {
        static int current_slot = 0;

        switch (ev.code) {
        case ABS_MT_SLOT:
            if (ev.value >= 0 && ev.value < ZYL_MAX_TOUCH_POINTS)
                current_slot = ev.value;
            break;
        case ABS_MT_TRACKING_ID:
            if (current_slot >= 0 && current_slot < ZYL_MAX_TOUCH_POINTS) {
                if (ev.value >= 0) {
                    svc->touch_points[current_slot].id     = ev.value;
                    svc->touch_points[current_slot].active  = true;
                } else {
                    /* tracking ID == -1 means finger lifted */
                    svc->touch_points[current_slot].active  = false;
                    svc->touch_points[current_slot].x       = 0.0f;
                    svc->touch_points[current_slot].y       = 0.0f;
                }
            }
            break;
        case ABS_MT_POSITION_X:
            if (current_slot >= 0 && current_slot < ZYL_MAX_TOUCH_POINTS) {
                /* Normalize: assume 0-4095 touchscreen range */
                svc->touch_points[current_slot].x = (float)ev.value / 4095.0f;
                if (svc->touch_points[current_slot].x > 1.0f)
                    svc->touch_points[current_slot].x = 1.0f;
            }
            break;
        case ABS_MT_POSITION_Y:
            if (current_slot >= 0 && current_slot < ZYL_MAX_TOUCH_POINTS) {
                svc->touch_points[current_slot].y = (float)ev.value / 4095.0f;
                if (svc->touch_points[current_slot].y > 1.0f)
                    svc->touch_points[current_slot].y = 1.0f;
            }
            break;
        }
    }

    return G_SOURCE_CONTINUE;
}

/* == Helper: Try to open an evdev device ==================== */

static void try_open_evdev(ZylInputService *svc)
{
    /* Try /dev/input/event0 through event9 for a key-capable device */
    for (int i = 0; i < 10; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        /* Check if device has key events via EVIOCGBIT */
        unsigned long evbits[2] = {0};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) >= 0) {
            /* Check EV_KEY bit */
            if (evbits[0] & (1UL << EV_KEY)) {
                svc->evdev_fd = fd;
                g_info("zyl-input: opened evdev device %s", path);
                return;
            }
        }
        close(fd);
    }

    g_info("zyl-input: no suitable evdev device found (will operate without hardware keys)");
    svc->evdev_fd = -1;
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

    /* Initialize touch points */
    for (int i = 0; i < ZYL_MAX_TOUCH_POINTS; i++) {
        svc->touch_points[i].id     = i;
        svc->touch_points[i].x      = 0.0f;
        svc->touch_points[i].y      = 0.0f;
        svc->touch_points[i].active  = false;
    }

    /* Try to open evdev device */
    try_open_evdev(svc);
    if (svc->evdev_fd >= 0) {
        svc->evdev_channel = g_io_channel_unix_new(svc->evdev_fd);
        g_io_channel_set_encoding(svc->evdev_channel, NULL, NULL);
        svc->evdev_watch_id = g_io_add_watch(
            svc->evdev_channel,
            G_IO_IN | G_IO_HUP | G_IO_ERR,
            on_evdev_event,
            svc);
    }

    /* Set up D-Bus */
    svc->introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    if (!svc->introspection_data) {
        g_critical("zyl-input: failed to parse introspection XML");
        zyl_input_destroy(svc);
        return NULL;
    }

    svc->bus_owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
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

    if (svc->evdev_watch_id > 0)
        g_source_remove(svc->evdev_watch_id);

    if (svc->evdev_channel)
        g_io_channel_unref(svc->evdev_channel);

    if (svc->evdev_fd >= 0)
        close(svc->evdev_fd);

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

/* == Public API -- Multi-touch ============================== */

int zyl_input_get_touch_points(ZylInputService *svc, ZylTouchPoint *out, int max)
{
    if (!svc || !out || max <= 0) return 0;

    int count = 0;
    int limit = (max < ZYL_MAX_TOUCH_POINTS) ? max : ZYL_MAX_TOUCH_POINTS;

    for (int i = 0; i < ZYL_MAX_TOUCH_POINTS && count < limit; i++) {
        if (svc->touch_points[i].active) {
            out[count] = svc->touch_points[i];
            count++;
        }
    }
    return count;
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
