#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 카메라 서비스 — 촬영, JPEG 저장, 손전등
 * 수행범위: Camera HAL 로 카메라 유무 확인·사진 한 장, 저장 경로는 카메라 디렉터리
 *           안만, 플래시 LED 손전등(Torch HAL — 없으면 GetCapabilities 가 torch=false
 *           로 알려 손전등이 비활성), D-Bus 인터페이스
 * 의존방향: camera.h, hal.h (Camera·Torch HAL), gio/gio.h
 * SOLID: SRP — 카메라 캡처만 담당
 * ────────────────────────────────────────────────────────── */

#include "camera.h"
#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <gio/gio.h>
#include <glib-unix.h>
#ifdef HAVE_LIBJPEG
#include <jpeglib.h>
#endif

#ifndef CAMERA_CAPTURE_DIR
#define CAMERA_CAPTURE_DIR "/data/camera/"
#endif

struct ZylCameraService {
    bool open;
    ZylCameraFacing facing;
    GDBusConnection *dbus;
    guint dbus_owner_id;
};

static ZylLensFacing lens_of(ZylCameraFacing facing) {
    return facing == ZYL_CAMERA_FRONT ? ZYL_HAL_LENS_FRONT : ZYL_HAL_LENS_BACK;
}

/* ─── D-Bus ─── */
static const char *cam_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_CAMERA_DBUS_NAME "'>"
    "    <method name='Open'>"
    "      <arg type='i' name='facing' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='Close'/>"
    "    <method name='Capture'>"
    "      <arg type='s' name='output_path' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='IsOpen'>"
    "      <arg type='b' name='open' direction='out'/>"
    "    </method>"
    "    <method name='GetCapabilities'>"
    "      <arg type='a{sv}' name='capabilities' direction='out'/>"
    "    </method>"
    "    <method name='SetTorch'>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "      <arg type='b' name='applied' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static void handle_cam_method(GDBusConnection *conn, const gchar *sender,
                               const gchar *path, const gchar *iface,
                               const gchar *method, GVariant *params,
                               GDBusMethodInvocation *inv, gpointer data) {
    ZylCameraService *cam = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "Open") == 0) {
        gint32 facing;
        g_variant_get(params, "(i)", &facing);
        int ret = zyl_camera_open(cam, (ZylCameraFacing)facing);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", ret == 0));
    } else if (g_strcmp0(method, "Close") == 0) {
        zyl_camera_close(cam);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "Capture") == 0) {
        const gchar *output_path;
        g_variant_get(params, "(&s)", &output_path);
        int ret = zyl_camera_capture(cam, output_path);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", ret == 0));
    } else if (g_strcmp0(method, "IsOpen") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", zyl_camera_is_open(cam)));
    } else if (g_strcmp0(method, "GetCapabilities") == 0) {
        GVariantBuilder d;
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&d, "{sv}", "torch", g_variant_new_boolean(zyl_hal_torch()->present()));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sv})", &d));
    } else if (g_strcmp0(method, "SetTorch") == 0) {
        gboolean on = FALSE;
        g_variant_get(params, "(b)", &on);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", zyl_hal_torch()->set(on)));
    }
}

static const GDBusInterfaceVTable cam_vtable = {
    .method_call = handle_cam_method
};

static void on_cam_bus(GDBusConnection *conn, const gchar *name,
                        gpointer data) {
    ZylCameraService *cam = data;
    (void)name;
    cam->dbus = conn;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(
        cam_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, ZYL_CAMERA_DBUS_PATH,
            info->interfaces[0], &cam_vtable, cam, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_message("[Camera] D-Bus registered: %s", ZYL_CAMERA_DBUS_NAME);
}

/* ─── Public API ─── */

ZylCameraService *zyl_camera_create(void) {
    ZylCameraService *cam = calloc(1, sizeof(ZylCameraService));
    if (!cam) return NULL;

    cam->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
        ZYL_CAMERA_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_cam_bus, NULL, NULL, cam, NULL);

    return cam;
}

void zyl_camera_destroy(ZylCameraService *cam) {
    if (!cam) return;
    zyl_camera_close(cam);
    g_bus_unown_name(cam->dbus_owner_id);
    free(cam);
}

int zyl_camera_open(ZylCameraService *cam, ZylCameraFacing facing) {
    if (!cam) return -1;
    cam->open = false;
    if (!zyl_hal_camera()->present(lens_of(facing))) {
        fprintf(stderr, "[Camera] No %s camera\n", facing == ZYL_CAMERA_FRONT ? "front" : "back");
        return -1;
    }
    cam->open = true;
    cam->facing = facing;
    g_message("[Camera] Opened %s camera", facing == ZYL_CAMERA_FRONT ? "front" : "back");
    return 0;
}

void zyl_camera_close(ZylCameraService *cam) {
    if (cam) cam->open = false;
}

int zyl_camera_capture(ZylCameraService *cam, const char *output_path) {
    if (!cam || !cam->open || !output_path) return -1;

    /* Reject path traversal attempts from D-Bus callers */
    if (strstr(output_path, "..") != NULL) {
        fprintf(stderr, "[Camera] Rejected path traversal: %s\n", output_path);
        return -1;
    }

    /* Whitelist: only allow writes under /data/camera/ */
    if (strncmp(output_path, CAMERA_CAPTURE_DIR, strlen(CAMERA_CAPTURE_DIR)) != 0) {
        fprintf(stderr, "[Camera] Rejected output path outside " CAMERA_CAPTURE_DIR ": %s\n",
                output_path);
        return -1;
    }

    ZylCameraImage img;
    if (!zyl_hal_camera()->capture(lens_of(cam->facing), &img)) return -1;
    if (img.format != ZYL_HAL_PIXEL_YUYV) {          /* the only one saved below */
        zyl_hal_camera()->free_image(&img);
        return -1;
    }

    int ret = 0;
#ifdef HAVE_LIBJPEG
    /* Convert YUYV → JPEG using libjpeg */
    {
        const uint8_t *yuyv = img.data;
        uint32_t width  = img.width;
        uint32_t height = img.height;

        /* Expand YUYV to RGB888 */
        uint8_t *rgb = malloc((size_t)width * height * 3);
        if (rgb) {
            for (uint32_t i = 0; i < width * height / 2; i++) {
                int y0 = yuyv[i*4+0];
                int u  = yuyv[i*4+1] - 128;
                int y1 = yuyv[i*4+2];
                int v  = yuyv[i*4+3] - 128;
#define CLAMP(x) ((x)<0?0:((x)>255?255:(x)))
                /* pixel 0 */
                rgb[(i*2+0)*3+0] = (uint8_t)CLAMP(y0 + (int)(1.402f*v));
                rgb[(i*2+0)*3+1] = (uint8_t)CLAMP(y0 - (int)(0.344f*u) - (int)(0.714f*v));
                rgb[(i*2+0)*3+2] = (uint8_t)CLAMP(y0 + (int)(1.772f*u));
                /* pixel 1 */
                rgb[(i*2+1)*3+0] = (uint8_t)CLAMP(y1 + (int)(1.402f*v));
                rgb[(i*2+1)*3+1] = (uint8_t)CLAMP(y1 - (int)(0.344f*u) - (int)(0.714f*v));
                rgb[(i*2+1)*3+2] = (uint8_t)CLAMP(y1 + (int)(1.772f*u));
#undef CLAMP
            }

            struct jpeg_compress_struct cinfo;
            struct jpeg_error_mgr jerr;
            cinfo.err = jpeg_std_error(&jerr);
            jpeg_create_compress(&cinfo);

            FILE *jf = fopen(output_path, "wb");
            if (jf) {
                jpeg_stdio_dest(&cinfo, jf);
                cinfo.image_width      = width;
                cinfo.image_height     = height;
                cinfo.input_components = 3;
                cinfo.in_color_space   = JCS_RGB;
                jpeg_set_defaults(&cinfo);
                jpeg_set_quality(&cinfo, 90, TRUE);
                jpeg_start_compress(&cinfo, TRUE);
                while (cinfo.next_scanline < cinfo.image_height) {
                    JSAMPROW row = rgb + cinfo.next_scanline * width * 3;
                    jpeg_write_scanlines(&cinfo, &row, 1);
                }
                jpeg_finish_compress(&cinfo);
                fclose(jf);
                g_message("[Camera] Captured JPEG → %s (%ux%u)",
                          output_path, width, height);
            } else {
                fprintf(stderr, "[Camera] Failed to open %s: %s\n",
                        output_path, strerror(errno));
                ret = -1;
            }
            jpeg_destroy_compress(&cinfo);
            free(rgb);
        } else {
            fprintf(stderr, "[Camera] malloc failed for YUYV→RGB conversion\n");
            ret = -1;
        }
    }
#else
    /* Fallback: write raw YUYV frame — apps convert to JPEG */
    FILE *f = fopen(output_path, "wb");
    if (f) {
        bool written = fwrite(img.data, 1, img.size, f) == img.size;
        if (fclose(f) != 0 || !written) {
            fprintf(stderr, "[Camera] Failed to write %s\n", output_path);
            ret = -1;
        } else {
            g_message("[Camera] Captured YUYV frame → %s (%zu bytes, no JPEG support)",
                      output_path, img.size);
        }
    } else {
        fprintf(stderr, "[Camera] Failed to open %s: %s\n",
                output_path, strerror(errno));
        ret = -1;
    }
#endif

    zyl_hal_camera()->free_image(&img);
    return ret;
}

bool zyl_camera_is_open(const ZylCameraService *cam) {
    return cam && cam->open;
}

/* ─── 데몬 진입점 ─── */
#ifndef ZYL_TEST

static GMainLoop *g_cam_loop = NULL;

static gboolean on_signal_cam(gpointer data) {
    (void)data;
    g_message("[Camera] Signal received, shutting down");
    if (g_cam_loop) g_main_loop_quit(g_cam_loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    ZylCameraService *cam = zyl_camera_create();
    if (!cam) { g_critical("[Camera] Failed to create service"); return 1; }
    g_message("[Camera] Zyl OS Camera Service started");
    g_cam_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_signal_cam, NULL);
    g_unix_signal_add(SIGINT,  on_signal_cam, NULL);
    g_main_loop_run(g_cam_loop);
    g_main_loop_unref(g_cam_loop);
    zyl_camera_destroy(cam);
    return 0;
}
#endif /* ZYL_TEST */
