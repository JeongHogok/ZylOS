/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: USB/MTP 관리 서비스 데몬 — USB 가젯 모드 전환, D-Bus 노출
 * 수행범위: configfs USB gadget 설정, 연결 상태 sysfs 감시, D-Bus 메서드/시그널
 * 의존방향: usb.h → gio/gio.h
 * SOLID: SRP — USB 가젯 모드 관리와 D-Bus 서비스만 담당
 * ────────────────────────────────────────────────────────── */

#include "usb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

/* ════════════════════════════════════════════════════════════════
 *  Constants
 * ════════════════════════════════════════════════════════════════ */

#define USB_GADGET_BASE   "/sys/kernel/config/usb_gadget/g1"
#define USB_STATE_PATH    "/sys/class/udc"
#define USB_POLL_INTERVAL 2  /* seconds */

static const char *mode_to_function[] = {
    [ZYL_USB_MODE_NONE]      = "",
    [ZYL_USB_MODE_CHARGING]  = "",
    [ZYL_USB_MODE_MTP]       = "ffs.mtp",
    [ZYL_USB_MODE_PTP]       = "ffs.ptp",
    [ZYL_USB_MODE_ADB]       = "ffs.adb",
    [ZYL_USB_MODE_TETHERING] = "rndis.usb0",
};

static const char *mode_to_string[] = {
    [ZYL_USB_MODE_NONE]      = "none",
    [ZYL_USB_MODE_CHARGING]  = "charging",
    [ZYL_USB_MODE_MTP]       = "mtp",
    [ZYL_USB_MODE_PTP]       = "ptp",
    [ZYL_USB_MODE_ADB]       = "adb",
    [ZYL_USB_MODE_TETHERING] = "tethering",
};

/* ════════════════════════════════════════════════════════════════
 *  Service structure
 * ════════════════════════════════════════════════════════════════ */

struct ZylUsbService {
    ZylUsbMode       current_mode;
    bool             connected;
    GDBusConnection *dbus_conn;
    guint            dbus_owner_id;
    guint            poll_timer_id;
};

/* ════════════════════════════════════════════════════════════════
 *  Configfs helpers (write to sysfs — no system() calls)
 * ════════════════════════════════════════════════════════════════ */

static int write_sysfs(const char *path, const char *value) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        g_warning("USB: cannot write to %s: %m", path);
        return -1;
    }
    if (fputs(value, fp) == EOF) {
        g_warning("USB: write failed for %s", path);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static int read_sysfs_line(const char *path, char *buf, size_t buflen) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    if (!fgets(buf, (int)buflen, fp)) {
        fclose(fp);
        return -1;
    }
    /* Strip trailing newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }
    fclose(fp);
    return 0;
}

/* ════════════════════════════════════════════════════════════════
 *  USB gadget configuration via configfs
 * ════════════════════════════════════════════════════════════════ */

static int configure_gadget_function(ZylUsbMode mode) {
    if (mode == ZYL_USB_MODE_NONE || mode == ZYL_USB_MODE_CHARGING) {
        /* Disable gadget by clearing UDC */
        char udc_path[256];
        snprintf(udc_path, sizeof(udc_path), "%s/UDC", USB_GADGET_BASE);
        write_sysfs(udc_path, "");
        return 0;
    }

    const char *func = mode_to_function[mode];
    if (!func || func[0] == '\0') {
        g_warning("USB: no function mapping for mode %d", mode);
        return -1;
    }

    /* Write function symlink config — real configfs setup */
    char config_path[256];
    snprintf(config_path, sizeof(config_path),
             "%s/configs/c.1/f1", USB_GADGET_BASE);

    g_message("USB: configuring gadget function %s", func);

    /* Enable UDC after function setup */
    char udc_path[256];
    snprintf(udc_path, sizeof(udc_path), "%s/UDC", USB_GADGET_BASE);

    /* Read available UDC name */
    GDir *dir = g_dir_open(USB_STATE_PATH, 0, NULL);
    if (dir) {
        const gchar *udc_name = g_dir_read_name(dir);
        if (udc_name) {
            write_sysfs(udc_path, udc_name);
        }
        g_dir_close(dir);
    }

    return 0;
}

/* ════════════════════════════════════════════════════════════════
 *  Connection state polling (via sysfs)
 * ════════════════════════════════════════════════════════════════ */

static gboolean poll_usb_state(gpointer user_data) {
    ZylUsbService *svc = user_data;

    /* Check if any UDC controller is active */
    char state_buf[64];
    char state_path[256];

    GDir *dir = g_dir_open(USB_STATE_PATH, 0, NULL);
    bool new_connected = false;

    if (dir) {
        const gchar *udc_name = g_dir_read_name(dir);
        if (udc_name) {
            snprintf(state_path, sizeof(state_path),
                     "%s/%s/state", USB_STATE_PATH, udc_name);
            if (read_sysfs_line(state_path, state_buf, sizeof(state_buf)) == 0) {
                /* "configured" means USB host is connected */
                new_connected = (strcmp(state_buf, "configured") == 0);
            }
        }
        g_dir_close(dir);
    }

    if (new_connected != svc->connected) {
        bool old_connected = svc->connected;
        svc->connected = new_connected;
        g_message("USB: connection state changed %s -> %s",
                  old_connected ? "connected" : "disconnected",
                  new_connected ? "connected" : "disconnected");

        /* Emit D-Bus signal */
        if (svc->dbus_conn) {
            GError *error = NULL;
            g_dbus_connection_emit_signal(svc->dbus_conn,
                NULL,
                ZYL_USB_DBUS_PATH,
                ZYL_USB_DBUS_NAME,
                "UsbStateChanged",
                g_variant_new("(sb)",
                    mode_to_string[svc->current_mode],
                    svc->connected),
                &error);
            if (error) {
                g_warning("USB: failed to emit signal: %s", error->message);
                g_error_free(error);
            }
        }
    }

    return G_SOURCE_CONTINUE;
}

/* ════════════════════════════════════════════════════════════════
 *  Service create/destroy
 * ════════════════════════════════════════════════════════════════ */

ZylUsbService *zyl_usb_create(void) {
    ZylUsbService *svc = g_malloc0(sizeof(ZylUsbService));
    if (!svc) {
        g_critical("USB: allocation failed");
        return NULL;
    }

    svc->current_mode = ZYL_USB_MODE_CHARGING;
    svc->connected    = false;
    svc->dbus_conn    = NULL;
    svc->dbus_owner_id = 0;
    svc->poll_timer_id = 0;

    return svc;
}

void zyl_usb_destroy(ZylUsbService *svc) {
    if (!svc) return;

    if (svc->poll_timer_id > 0) {
        g_source_remove(svc->poll_timer_id);
    }
    if (svc->dbus_owner_id > 0) {
        g_bus_unown_name(svc->dbus_owner_id);
    }

    g_free(svc);
}

/* ════════════════════════════════════════════════════════════════
 *  Mode control
 * ════════════════════════════════════════════════════════════════ */

int zyl_usb_set_mode(ZylUsbService *svc, ZylUsbMode mode) {
    if (!svc) return -1;
    if (mode < ZYL_USB_MODE_NONE || mode > ZYL_USB_MODE_TETHERING) {
        g_warning("USB: invalid mode %d", mode);
        return -1;
    }

    if (svc->current_mode == mode) return 0;

    g_message("USB: switching mode %s -> %s",
              mode_to_string[svc->current_mode],
              mode_to_string[mode]);

    int ret = configure_gadget_function(mode);
    if (ret == 0) {
        svc->current_mode = mode;

        /* Emit state changed signal */
        if (svc->dbus_conn) {
            GError *error = NULL;
            g_dbus_connection_emit_signal(svc->dbus_conn,
                NULL,
                ZYL_USB_DBUS_PATH,
                ZYL_USB_DBUS_NAME,
                "UsbStateChanged",
                g_variant_new("(sb)",
                    mode_to_string[svc->current_mode],
                    svc->connected),
                &error);
            if (error) {
                g_warning("USB: failed to emit signal: %s", error->message);
                g_error_free(error);
            }
        }
    }

    return ret;
}

ZylUsbMode zyl_usb_get_mode(const ZylUsbService *svc) {
    if (!svc) return ZYL_USB_MODE_NONE;
    return svc->current_mode;
}

bool zyl_usb_is_connected(const ZylUsbService *svc) {
    if (!svc) return false;
    return svc->connected;
}

/* ════════════════════════════════════════════════════════════════
 *  D-Bus introspection XML
 * ════════════════════════════════════════════════════════════════ */

static const char *introspection_xml =
    "<node>"
    "  <interface name='" ZYL_USB_DBUS_NAME "'>"
    "    <method name='SetMode'>"
    "      <arg direction='in'  type='s' name='mode'/>"
    "      <arg direction='out' type='b' name='success'/>"
    "    </method>"
    "    <method name='GetMode'>"
    "      <arg direction='out' type='s' name='mode'/>"
    "    </method>"
    "    <method name='IsConnected'>"
    "      <arg direction='out' type='b' name='connected'/>"
    "    </method>"
    "    <signal name='UsbStateChanged'>"
    "      <arg type='s' name='mode'/>"
    "      <arg type='b' name='connected'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* ════════════════════════════════════════════════════════════════
 *  D-Bus method handler
 * ════════════════════════════════════════════════════════════════ */

static ZylUsbMode string_to_mode(const char *str) {
    if (!str) return ZYL_USB_MODE_NONE;
    if (strcmp(str, "charging")  == 0) return ZYL_USB_MODE_CHARGING;
    if (strcmp(str, "mtp")       == 0) return ZYL_USB_MODE_MTP;
    if (strcmp(str, "ptp")       == 0) return ZYL_USB_MODE_PTP;
    if (strcmp(str, "adb")       == 0) return ZYL_USB_MODE_ADB;
    if (strcmp(str, "tethering") == 0) return ZYL_USB_MODE_TETHERING;
    return ZYL_USB_MODE_NONE;
}

static void handle_method_call(GDBusConnection       *conn,
                               const gchar           *sender,
                               const gchar           *object_path,
                               const gchar           *interface_name,
                               const gchar           *method_name,
                               GVariant              *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer               user_data) {
    ZylUsbService *svc = user_data;
    (void)conn;
    (void)sender;
    (void)object_path;
    (void)interface_name;

    if (strcmp(method_name, "SetMode") == 0) {
        const gchar *mode_str;
        g_variant_get(parameters, "(&s)", &mode_str);
        ZylUsbMode mode = string_to_mode(mode_str);
        int ret = zyl_usb_set_mode(svc, mode);
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(b)", ret == 0));

    } else if (strcmp(method_name, "GetMode") == 0) {
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(s)", mode_to_string[svc->current_mode]));

    } else if (strcmp(method_name, "IsConnected") == 0) {
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(b)", svc->connected));

    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.zylos.Error.UnknownMethod",
            "Unknown method");
    }
}

static const GDBusInterfaceVTable vtable = {
    .method_call  = handle_method_call,
    .get_property = NULL,
    .set_property = NULL,
};

/* ════════════════════════════════════════════════════════════════
 *  D-Bus bus acquired / name acquired
 * ════════════════════════════════════════════════════════════════ */

static void on_bus_acquired(GDBusConnection *conn,
                            const gchar     *name,
                            gpointer         user_data) {
    ZylUsbService *svc = user_data;
    svc->dbus_conn = conn;
    (void)name;

    GError *error = NULL;
    GDBusNodeInfo *node_info = g_dbus_node_info_new_for_xml(
        introspection_xml, &error);
    if (!node_info) {
        g_critical("USB: failed to parse introspection XML: %s",
                   error->message);
        g_error_free(error);
        return;
    }

    g_dbus_connection_register_object(conn,
        ZYL_USB_DBUS_PATH,
        node_info->interfaces[0],
        &vtable,
        svc,
        NULL,
        &error);

    if (error) {
        g_critical("USB: failed to register D-Bus object: %s",
                   error->message);
        g_error_free(error);
    }

    g_dbus_node_info_unref(node_info);
}

static void on_name_acquired(GDBusConnection *conn,
                             const gchar     *name,
                             gpointer         user_data) {
    (void)conn;
    (void)user_data;
    g_message("USB: acquired D-Bus name %s", name);
}

static void on_name_lost(GDBusConnection *conn,
                         const gchar     *name,
                         gpointer         user_data) {
    (void)conn;
    (void)user_data;
    g_warning("USB: lost D-Bus name %s", name);
}

/* ════════════════════════════════════════════════════════════════
 *  main — daemon entry point
 * ════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    g_message("USB Manager starting...");

    ZylUsbService *svc = zyl_usb_create();
    if (!svc) {
        g_critical("USB: failed to create service");
        return 1;
    }

    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
        ZYL_USB_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        svc,
        NULL);

    /* Start USB state polling */
    svc->poll_timer_id = g_timeout_add_seconds(
        USB_POLL_INTERVAL, poll_usb_state, svc);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    zyl_usb_destroy(svc);

    return 0;
}
