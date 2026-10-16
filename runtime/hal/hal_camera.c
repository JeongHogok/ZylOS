/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Camera HAL Linux 구현 — V4L2 캡처 장치에서 사진 한 장
 * 수행범위: 후면 /dev/video0·전면 /dev/video1 중 캡처 장치만, YUYV 로 형식을 정하고
 *           (드라이버가 다른 형식으로 바꾸면 거부) MMAP 버퍼로 스트림을 켠 뒤 첫 프레임을
 *           poll 로 기다려 행 패딩 없이 복사, 스트림을 끄고 버퍼를 풀고 장치를 닫는다.
 *           보드 기본 구현 — ISP 를 거치는 보드는 board_hal_camera 로 자기 것을 쓴다
 * 의존방향: hal.h (Domain), linux/videodev2.h
 * SOLID: SRP — 프레임 획득만. 저장 위치·인코딩은 서비스 몫
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include "hal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

/* Overridable so tests point at fake device nodes */
#ifndef ZYL_DEV_ROOT
#define ZYL_DEV_ROOT ""
#endif
#define MAX_BUFFERS      4
#define FRAME_TIMEOUT_MS 2000
#define STILL_WIDTH      1920    /* asked for; S_FMT settles on what the sensor has */
#define STILL_HEIGHT     1080

typedef struct {
    void   *start;
    size_t  length;
} Buffer;

static int xioctl(int fd, unsigned long request, void *arg) {
    int r;
    do { r = ioctl(fd, request, arg); } while (r == -1 && errno == EINTR);
    return r;
}

/* The capture device for this lens, -1 when there is none */
static int open_capture(ZylLensFacing lens) {
    const char *dev = lens == ZYL_HAL_LENS_FRONT ? ZYL_DEV_ROOT "/dev/video1" : ZYL_DEV_ROOT "/dev/video0";
    int fd = open(dev, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    struct v4l2_capability cap;
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        fprintf(stderr, "[Camera HAL] %s: not a V4L2 device: %s\n", dev, strerror(errno));
        close(fd);
        return -1;
    }
    /* capabilities covers every node of the device; device_caps is this one */
    uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "[Camera HAL] %s does not support capture\n", dev);
        close(fd);
        return -1;
    }
    return fd;
}

static void unmap_buffers(Buffer *bufs, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        if (bufs[i].start) munmap(bufs[i].start, bufs[i].length);
}

/* YUYV at the size the driver settles on, buffers mapped and queued, streaming */
static bool start_stream(int fd, struct v4l2_format *fmt, Buffer *bufs, uint32_t *n) {
    fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt->fmt.pix.width  = STILL_WIDTH;
    fmt->fmt.pix.height = STILL_HEIGHT;
    fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt->fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, fmt) < 0) {
        fprintf(stderr, "[Camera HAL] VIDIOC_S_FMT failed: %s\n", strerror(errno));
        return false;
    }
    /* S_FMT adjusts instead of failing: a driver may settle on MJPEG etc.,
       and the image handed out is YUYV */
    if (fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV || fmt->fmt.pix.width == 0 || fmt->fmt.pix.height == 0) {
        fprintf(stderr, "[Camera HAL] Driver does not do YUYV\n");
        return false;
    }

    struct v4l2_requestbuffers req = {
        .count = MAX_BUFFERS, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP,
    };
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count == 0) {
        fprintf(stderr, "[Camera HAL] VIDIOC_REQBUFS failed: %s\n", strerror(errno));
        return false;
    }
    /* A driver may hand out more than asked for: map no more than there is room for */
    uint32_t count = req.count < MAX_BUFFERS ? req.count : MAX_BUFFERS;
    for (*n = 0; *n < count; (*n)++) {
        struct v4l2_buffer buf = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = *n };
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) return false;
        void *start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (start == MAP_FAILED) return false;
        bufs[*n] = (Buffer){ start, buf.length };
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            (*n)++;                                   /* mapped: unmapped by the caller */
            return false;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "[Camera HAL] VIDIOC_STREAMON failed: %s\n", strerror(errno));
        return false;
    }
    return true;
}

/* The first frame, copied out packed (row padding dropped) */
static bool grab_frame(int fd, const struct v4l2_format *fmt, const Buffer *bufs, uint32_t n,
                       ZylCameraImage *out) {
    struct v4l2_buffer buf = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
    /* The fd is non-blocking: right after STREAMON no frame is there yet */
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    errno = 0;
    if (poll(&pfd, 1, FRAME_TIMEOUT_MS) <= 0 || xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        fprintf(stderr, "[Camera HAL] No frame: %s\n", errno ? strerror(errno) : "timed out");
        return false;
    }
    /* Validate what the kernel handed back before reading it */
    if (buf.index >= n) {
        fprintf(stderr, "[Camera HAL] Invalid buffer index %u (max %u)\n", buf.index, n);
        return false;
    }
    uint32_t w = fmt->fmt.pix.width, h = fmt->fmt.pix.height;
    size_t row = (size_t)w * 2;
    size_t stride = fmt->fmt.pix.bytesperline ? fmt->fmt.pix.bytesperline : row;
    size_t used = buf.bytesused < bufs[buf.index].length ? buf.bytesused : bufs[buf.index].length;
    if (stride < row || used < stride * (h - 1) + row) {
        fprintf(stderr, "[Camera HAL] Short frame: %zu bytes for %ux%u\n", used, w, h);
        return false;
    }
    uint8_t *data = malloc(row * h);
    if (!data) return false;
    for (uint32_t y = 0; y < h; y++)
        memcpy(data + y * row, (const uint8_t *)bufs[buf.index].start + y * stride, row);
    *out = (ZylCameraImage){ .format = ZYL_HAL_PIXEL_YUYV, .width = w, .height = h,
                             .data = data, .size = row * h };
    return true;
}

static bool camera_present(ZylLensFacing lens) {
    int fd = open_capture(lens);
    if (fd < 0) return false;
    close(fd);
    return true;
}

static bool camera_capture(ZylLensFacing lens, ZylCameraImage *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    int fd = open_capture(lens);
    if (fd < 0) return false;

    struct v4l2_format fmt = {0};
    Buffer bufs[MAX_BUFFERS] = {{0}};
    uint32_t n = 0;
    bool ok = start_stream(fd, &fmt, bufs, &n) && grab_frame(fd, &fmt, bufs, n, out);

    /* Every capture starts from a closed device: nothing is left mapped or streaming */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd, VIDIOC_STREAMOFF, &type);
    unmap_buffers(bufs, n);
    close(fd);
    return ok;
}

static void camera_free_image(ZylCameraImage *image) {
    if (!image) return;
    free(image->data);
    image->data = NULL;
    image->size = 0;
}

static const ZylCameraHal camera_hal = {
    .present = camera_present, .capture = camera_capture, .free_image = camera_free_image,
};

const ZylCameraHal *zyl_hal_camera(void) { return &camera_hal; }
