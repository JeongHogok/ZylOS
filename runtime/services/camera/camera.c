#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: V4L2 카메라 서비스 — 캡처, 프리뷰, JPEG 저장
 * 수행범위: /dev/videoN 열기, MMAP 버퍼, 스트리밍, D-Bus 인터페이스
 * 의존방향: camera.h, gio/gio.h, linux/videodev2.h
 * SOLID: SRP — 카메라 캡처만 담당
 * ────────────────────────────────────────────────────────── */

#include "camera.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <gio/gio.h>
#include <glib-unix.h>

#define MAX_BUFFERS 4

struct buffer {
    void   *start;
    size_t  length;
};

struct ZylCameraService {
    int fd;
    bool streaming;
    ZylCameraFacing facing;
    ZylCameraConfig config;
    struct buffer buffers[MAX_BUFFERS];
    uint32_t n_buffers;
    zyl_camera_frame_cb frame_cb;
    void *frame_user_data;
    GDBusConnection *dbus;
    guint dbus_owner_id;
};

/* ─── V4L2 ioctl wrapper ─── */
static int xioctl(int fd, unsigned long request, void *arg) {
    int r;
    do { r = ioctl(fd, request, arg); } while (r == -1 && errno == EINTR);
    return r;
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
    cam->fd = -1;
    cam->streaming = false;

    cam->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
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
    if (cam->fd >= 0) zyl_camera_close(cam);

    /* Select device: back=video0, front=video1 */
    const char *dev = (facing == ZYL_CAMERA_FRONT) ?
        "/dev/video1" : "/dev/video0";

    cam->fd = open(dev, O_RDWR | O_NONBLOCK);
    if (cam->fd < 0) {
        fprintf(stderr, "[Camera] Failed to open %s: %s\n",
                dev, strerror(errno));
        return -1;
    }

    /* Verify V4L2 capability */
    struct v4l2_capability cap;
    if (xioctl(cam->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        fprintf(stderr, "[Camera] VIDIOC_QUERYCAP failed: %s\n",
                strerror(errno));
        close(cam->fd); cam->fd = -1;
        return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "[Camera] %s does not support capture\n", dev);
        close(cam->fd); cam->fd = -1;
        return -1;
    }

    cam->facing = facing;
    g_message("[Camera] Opened %s (%s)", dev, cap.card);
    return 0;
}

void zyl_camera_close(ZylCameraService *cam) {
    if (!cam) return;
    zyl_camera_stop_preview(cam);

    /* Unmap buffers */
    for (uint32_t i = 0; i < cam->n_buffers; i++) {
        if (cam->buffers[i].start) {
            munmap(cam->buffers[i].start, cam->buffers[i].length);
            cam->buffers[i].start = NULL;
        }
    }
    cam->n_buffers = 0;

    if (cam->fd >= 0) {
        close(cam->fd);
        cam->fd = -1;
    }
}

static int setup_mmap_buffers(ZylCameraService *cam) {
    struct v4l2_requestbuffers req = {0};
    req.count  = MAX_BUFFERS;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(cam->fd, VIDIOC_REQBUFS, &req) < 0) {
        fprintf(stderr, "[Camera] VIDIOC_REQBUFS failed: %s\n",
                strerror(errno));
        return -1;
    }

    cam->n_buffers = req.count;
    for (uint32_t i = 0; i < cam->n_buffers; i++) {
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (xioctl(cam->fd, VIDIOC_QUERYBUF, &buf) < 0) goto fail_cleanup;

        cam->buffers[i].length = buf.length;
        cam->buffers[i].start = mmap(NULL, buf.length,
            PROT_READ | PROT_WRITE, MAP_SHARED, cam->fd, buf.m.offset);
        if (cam->buffers[i].start == MAP_FAILED) {
            cam->buffers[i].start = NULL;
            goto fail_cleanup;
        }
    }
    return 0;

fail_cleanup:
    /* Unmap any successfully mapped buffers */
    for (uint32_t j = 0; j < cam->n_buffers; j++) {
        if (cam->buffers[j].start) {
            munmap(cam->buffers[j].start, cam->buffers[j].length);
            cam->buffers[j].start = NULL;
        }
    }
    cam->n_buffers = 0;
    return -1;
}

int zyl_camera_start_preview(ZylCameraService *cam,
                              const ZylCameraConfig *config,
                              zyl_camera_frame_cb callback,
                              void *user_data) {
    if (!cam || cam->fd < 0) return -1;

    /* Set format */
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width  = config ? config->width  : 640;
    fmt.fmt.pix.height = config ? config->height : 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(cam->fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "[Camera] VIDIOC_S_FMT failed: %s\n",
                strerror(errno));
        return -1;
    }

    cam->config.width  = fmt.fmt.pix.width;
    cam->config.height = fmt.fmt.pix.height;
    cam->config.format = ZYL_CAMERA_FMT_YUYV;
    cam->frame_cb = callback;
    cam->frame_user_data = user_data;

    if (setup_mmap_buffers(cam) < 0) return -1;

    /* Queue buffers */
    for (uint32_t i = 0; i < cam->n_buffers; i++) {
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (xioctl(cam->fd, VIDIOC_QBUF, &buf) < 0) return -1;
    }

    /* Start streaming */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(cam->fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "[Camera] VIDIOC_STREAMON failed: %s\n",
                strerror(errno));
        return -1;
    }

    cam->streaming = true;
    g_message("[Camera] Preview started: %ux%u YUYV",
              cam->config.width, cam->config.height);
    return 0;
}

void zyl_camera_stop_preview(ZylCameraService *cam) {
    if (!cam || !cam->streaming) return;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(cam->fd, VIDIOC_STREAMOFF, &type);
    cam->streaming = false;
    g_message("[Camera] Preview stopped");
}

int zyl_camera_capture(ZylCameraService *cam, const char *output_path) {
    if (!cam || cam->fd < 0 || !output_path) return -1;

    /* Reject path traversal attempts from D-Bus callers */
    if (strstr(output_path, "..") != NULL) {
        fprintf(stderr, "[Camera] Rejected path traversal: %s\n", output_path);
        return -1;
    }

    /* Whitelist: only allow writes under /data/camera/ */
    if (strncmp(output_path, "/data/camera/", 13) != 0) {
        fprintf(stderr, "[Camera] Rejected output path outside /data/camera/: %s\n",
                output_path);
        return -1;
    }

    /* Ensure streaming for capture */
    bool was_streaming = cam->streaming;
    if (!was_streaming) {
        ZylCameraConfig cfg = { .width = 1920, .height = 1080,
                                .format = ZYL_CAMERA_FMT_YUYV, .fps = 30 };
        if (zyl_camera_start_preview(cam, &cfg, NULL, NULL) < 0) return -1;
    }

    /* Dequeue a buffer */
    struct v4l2_buffer buf = {0};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(cam->fd, VIDIOC_DQBUF, &buf) < 0) {
        fprintf(stderr, "[Camera] VIDIOC_DQBUF failed: %s\n",
                strerror(errno));
        if (!was_streaming) zyl_camera_stop_preview(cam);
        return -1;
    }

    /* Validate buffer index from kernel */
    if (buf.index >= cam->n_buffers) {
        fprintf(stderr, "[Camera] Invalid buffer index %u (max %u)\n",
                buf.index, cam->n_buffers);
        xioctl(cam->fd, VIDIOC_QBUF, &buf);
        if (!was_streaming) zyl_camera_stop_preview(cam);
        return -1;
    }

    /* Write raw frame to file (YUYV — apps convert to JPEG) */
    int ret = 0;
    FILE *f = fopen(output_path, "wb");
    if (f) {
        fwrite(cam->buffers[buf.index].start, 1, buf.bytesused, f);
        fclose(f);
        g_message("[Camera] Captured frame → %s (%u bytes)",
                  output_path, buf.bytesused);
    } else {
        fprintf(stderr, "[Camera] Failed to open %s: %s\n",
                output_path, strerror(errno));
        ret = -1;
    }

    /* Re-queue buffer */
    xioctl(cam->fd, VIDIOC_QBUF, &buf);
    if (!was_streaming) zyl_camera_stop_preview(cam);
    return ret;
}

int zyl_camera_get_supported_resolutions(ZylCameraService *cam,
                                          ZylCameraConfig **out,
                                          int *count) {
    if (!cam || cam->fd < 0 || !out || !count) return -1;

    int cap = 16, n = 0;
    ZylCameraConfig *list = calloc(cap, sizeof(ZylCameraConfig));
    if (!list) return -1;

    struct v4l2_frmsizeenum frmsize = {0};
    frmsize.pixel_format = V4L2_PIX_FMT_YUYV;

    while (xioctl(cam->fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
        if (n >= cap) {
            cap *= 2;
            ZylCameraConfig *tmp = realloc(list, cap * sizeof(ZylCameraConfig));
            if (!tmp) break;
            list = tmp;
        }
        if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            list[n].width  = frmsize.discrete.width;
            list[n].height = frmsize.discrete.height;
            list[n].format = ZYL_CAMERA_FMT_YUYV;
            list[n].fps    = 30;
            n++;
        }
        frmsize.index++;
    }

    *out = list;
    *count = n;
    return 0;
}

bool zyl_camera_is_open(const ZylCameraService *cam) {
    return cam && cam->fd >= 0;
}

/* ─── 데몬 진입점 ─── */

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
    g_message("[Camera] Zyl OS Camera Service started (V4L2)");
    g_cam_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_signal_cam, NULL);
    g_unix_signal_add(SIGINT,  on_signal_cam, NULL);
    g_main_loop_run(g_cam_loop);
    g_main_loop_unref(g_cam_loop);
    zyl_camera_destroy(cam);
    return 0;
}
