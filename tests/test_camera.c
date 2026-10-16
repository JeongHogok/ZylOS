/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 카메라 서비스(runtime/services/camera)와 V4L2 Camera HAL(runtime/hal/hal_camera.c)
 *       단위 테스트
 * 수행범위: 캡처 장치만 열림(V4L2 아님·캡처 기능 없음·캡처가 아닌 노드 거부, 전면=video1),
 *           저장 경로는 카메라 디렉터리 안만(.., 접두어 흉내, 상대 경로 거부 —
 *           거부 시 스트림도 안 켬), 스트림 직후 첫 프레임을 기다려 그대로 저장,
 *           촬영마다 버퍼·장치를 남기지 않음, YUYV 를 못 하는 드라이버 거부,
 *           드라이버가 정한 크기·행 패딩 제거·더 많이 준 버퍼, 엉뚱한 버퍼 번호·
 *           모자란 프레임·프레임 없음 처리
 * 의존방향: camera.c (ZYL_TEST, CAMERA_CAPTURE_DIR 교체), hal_camera.c (ZYL_DEV_ROOT,
 *           ioctl/mmap/poll 을 가짜 V4L2 드라이버로), gio
 * SOLID: SRP — 카메라 캡처 경로 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#define ROOT_ "/tmp/zyl-camera-test"
#define ZYL_DEV_ROOT ROOT_
#define CAMERA_CAPTURE_DIR ROOT_ "/data/camera/"
#include <assert.h>
#include <stdarg.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

/* A fake V4L2 driver behind the HAL's own ioctl/mmap/poll calls,
   with the kernel's rules: a frame is only there after waiting for it */
static int fake_ioctl(int fd, unsigned long req, ...);
static void *fake_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
static int fake_munmap(void *addr, size_t len);
static int fake_poll(struct pollfd *fds, nfds_t n, int timeout);
#define ioctl fake_ioctl
#define mmap fake_mmap
#define munmap fake_munmap
#define poll fake_poll
#include "../runtime/hal/hal_camera.c"
#include "../runtime/services/camera/camera.c"

#define PAGE 4096
static struct {
    bool v4l2, capture, no_frames;
    uint32_t node_caps;                 /* non-zero: the node's own caps (device_caps) */
    uint32_t pixfmt, max_w, max_h;      /* what S_FMT settles on */
    uint32_t stride;                    /* bytesperline, 0 = packed */
    uint32_t reqbufs;                   /* what REQBUFS hands out, 0 = as asked */
    uint32_t w, h, sizeimage, bogus_index, short_by;
    bool streaming, frame_ready;
    int streamons, mapped;
    void *buf_at[MAX_BUFFERS];
} drv;

static int fake_ioctl(int fd, unsigned long req, ...) {
    (void)fd;
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (!drv.v4l2) { errno = ENOTTY; return -1; }
    switch (req) {
    case VIDIOC_QUERYCAP: {
        struct v4l2_capability *c = arg;
        memset(c, 0, sizeof(*c));
        snprintf((char *)c->card, sizeof(c->card), "fake sensor");
        c->capabilities = drv.capture ? V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING
                                      : V4L2_CAP_VIDEO_OUTPUT;
        if (drv.node_caps) {
            c->capabilities |= V4L2_CAP_DEVICE_CAPS;
            c->device_caps = drv.node_caps;
        }
        return 0;
    }
    case VIDIOC_S_FMT: {
        struct v4l2_format *f = arg;
        if (f->fmt.pix.width > drv.max_w) f->fmt.pix.width = drv.max_w;
        if (f->fmt.pix.height > drv.max_h) f->fmt.pix.height = drv.max_h;
        f->fmt.pix.pixelformat = drv.pixfmt;
        drv.w = f->fmt.pix.width;
        drv.h = f->fmt.pix.height;
        f->fmt.pix.bytesperline = drv.stride;
        drv.sizeimage = f->fmt.pix.sizeimage = drv.h * (drv.stride ? drv.stride : drv.w * 2);
        return 0;
    }
    case VIDIOC_REQBUFS: {                 /* vb2 today: old mappings orphaned, no error */
        struct v4l2_requestbuffers *r = arg;
        r->count = drv.reqbufs ? drv.reqbufs : r->count;
        return 0;
    }
    case VIDIOC_QUERYBUF: {
        struct v4l2_buffer *b = arg;
        b->length = drv.sizeimage;
        b->m.offset = b->index * PAGE;
        return 0;
    }
    case VIDIOC_QBUF:
        return 0;
    case VIDIOC_STREAMON:
        drv.streaming = true;
        drv.streamons++;
        return 0;
    case VIDIOC_STREAMOFF:
        drv.streaming = drv.frame_ready = false;
        return 0;
    case VIDIOC_DQBUF: {
        struct v4l2_buffer *b = arg;
        if (!drv.streaming) { errno = EINVAL; return -1; }
        if (!drv.frame_ready) { errno = EAGAIN; return -1; }
        drv.frame_ready = false;
        b->index = drv.bogus_index ? drv.bogus_index : 0;
        b->bytesused = drv.sizeimage - drv.short_by;
        if (b->index < MAX_BUFFERS && drv.buf_at[b->index]) {
            uint8_t *p = drv.buf_at[b->index];
            memset(p, 0xEE, drv.sizeimage);                  /* row padding */
            for (uint32_t y = 0; y < drv.h; y++)
                memset(p + (size_t)y * (drv.stride ? drv.stride : drv.w * 2), 0x5A, drv.w * 2);
        }
        return 0;
    }
    }
    errno = EINVAL;
    return -1;
}

static void *fake_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    (void)addr; (void)prot; (void)flags; (void)fd;
    assert(off / PAGE < MAX_BUFFERS);                         /* never past the HAL's table */
    void *p = calloc(1, len);
    assert(p);
    drv.buf_at[off / PAGE] = p;
    drv.mapped++;
    return p;
}

static int fake_munmap(void *addr, size_t len) {
    (void)len;
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (drv.buf_at[i] == addr) drv.buf_at[i] = NULL;
    free(addr);
    drv.mapped--;
    return 0;
}

static int fake_poll(struct pollfd *fds, nfds_t n, int timeout) {
    (void)n; (void)timeout;
    if (!drv.streaming || drv.no_frames) return 0;           /* timed out */
    drv.frame_ready = true;
    fds[0].revents = POLLIN;
    return 1;
}

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)
static ZylCameraService cam;

static int open_fds(void) {
    GDir *d = g_dir_open("/proc/self/fd", 0, NULL);
    int n = 0;
    while (d && g_dir_read_name(d)) n++;
    if (d) g_dir_close(d);
    return n;
}

static void reset(void) {
    zyl_camera_close(&cam);
    memset(&cam, 0, sizeof(cam));
    memset(&drv, 0, sizeof(drv));
    drv.v4l2 = drv.capture = true;
    drv.pixfmt = V4L2_PIX_FMT_YUYV;
    drv.max_w = 1920; drv.max_h = 1080;
    g_assert_true(system("rm -rf " ROOT_ " && mkdir -p " ROOT_ "/data/camera " ROOT_ "/dev && "
                         "touch " ROOT_ "/dev/video0 " ROOT_ "/dev/video1") == 0);
}

static gsize file_size(const char *path) {
    char *s = NULL;
    gsize n = 0;
    if (!g_file_get_contents(path, &s, &n, NULL)) return 0;
    g_free(s);
    return n;
}

static void test_open_takes_only_a_capture_device(void) {
    drv.v4l2 = false;                                         /* not a V4L2 node */
    assert(zyl_camera_open(&cam, ZYL_CAMERA_BACK) == -1 && !zyl_camera_is_open(&cam));
    drv.v4l2 = true; drv.capture = false;                     /* an output device */
    assert(zyl_camera_open(&cam, ZYL_CAMERA_BACK) == -1 && !zyl_camera_is_open(&cam));
    drv.capture = true;
    drv.node_caps = V4L2_CAP_META_CAPTURE;                    /* an ISP's statistics node */
    assert(zyl_camera_open(&cam, ZYL_CAMERA_BACK) == -1);
    drv.node_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
    assert(zyl_camera_open(&cam, ZYL_CAMERA_BACK) == 0 && zyl_camera_is_open(&cam));
    g_assert_true(unlink(ROOT_ "/dev/video1") == 0);          /* no front camera */
    assert(zyl_camera_open(&cam, ZYL_CAMERA_FRONT) == -1 && !zyl_camera_is_open(&cam));
    assert(!zyl_hal_camera()->present(ZYL_HAL_LENS_FRONT) && zyl_hal_camera()->present(ZYL_HAL_LENS_BACK));
    assert(zyl_camera_open(&cam, ZYL_CAMERA_BACK) == 0);
    assert(zyl_camera_capture(NULL, ROOT_ "/data/camera/a.yuv") == -1);
    zyl_camera_close(&cam);
    assert(!zyl_camera_is_open(&cam));
}

static void test_capture_path_must_stay_in_the_camera_dir(void) {
    assert(zyl_camera_capture(&cam, ROOT_ "/data/camera/a.yuv") == -1);   /* not open */
    assert(zyl_camera_open(&cam, ZYL_CAMERA_BACK) == 0);
    const char *bad[] = {
        "/etc/passwd", "", "data/camera/a.yuv",
        ROOT_ "/data/camera",                                 /* the dir itself */
        ROOT_ "/data/cameraX/a.yuv",                          /* prefix look-alike */
        ROOT_ "/data/camera/../escape.yuv",
        ROOT_ "/data/camera/x/../../../escape.yuv",
        NULL };
    for (int i = 0; bad[i]; i++)
        assert(zyl_camera_capture(&cam, bad[i]) == -1);
    assert(zyl_camera_capture(&cam, NULL) == -1);
    assert(drv.streamons == 0);                               /* refused before the sensor */
    assert(!g_file_test(ROOT_ "/data/escape.yuv", G_FILE_TEST_EXISTS));
    assert(!g_file_test(ROOT_ "/escape.yuv", G_FILE_TEST_EXISTS));
}

static void test_capture_waits_for_the_first_frame_and_saves_it(void) {
    assert(zyl_camera_open(&cam, ZYL_CAMERA_BACK) == 0);
    /* the fd is non-blocking: DQBUF straight after STREAMON is EAGAIN */
    assert(zyl_camera_capture(&cam, ROOT_ "/data/camera/IMG_1.yuv") == 0);
    char *s = NULL;
    gsize n = 0;
    g_assert_true(g_file_get_contents(ROOT_ "/data/camera/IMG_1.yuv", &s, &n, NULL));
    assert(n == 1920 * 1080 * 2);
    assert((unsigned char)s[0] == 0x5A && (unsigned char)s[n - 1] == 0x5A);
    g_free(s);
    assert(!drv.streaming && drv.mapped == 0);                /* stream off, buffers given back */
}

static void test_captures_leave_no_buffers_or_devices_behind(void) {
    assert(zyl_camera_open(&cam, ZYL_CAMERA_BACK) == 0);
    int fds = open_fds();
    for (int i = 0; i < 3; i++) {
        char path[128];
        snprintf(path, sizeof(path), ROOT_ "/data/camera/IMG_%d.yuv", i);
        assert(zyl_camera_capture(&cam, path) == 0);
        assert(drv.mapped == 0);                              /* ~16 MB per capture leaked */
        assert(open_fds() == fds);                            /* the device is closed again */
    }
    drv.reqbufs = 8;                                          /* more than asked for */
    assert(zyl_camera_capture(&cam, ROOT_ "/data/camera/IMG_8.yuv") == 0);
    assert(drv.mapped == 0 && open_fds() == fds);
}

static void test_driver_without_yuyv_is_refused(void) {
    drv.pixfmt = V4L2_PIX_FMT_MJPEG;                          /* S_FMT adjusts, never fails */
    assert(zyl_camera_open(&cam, ZYL_CAMERA_BACK) == 0);
    assert(zyl_camera_capture(&cam, ROOT_ "/data/camera/IMG.yuv") == -1);
    assert(!g_file_test(ROOT_ "/data/camera/IMG.yuv", G_FILE_TEST_EXISTS));
    assert(drv.mapped == 0);
}

static void test_frame_size_is_what_the_driver_chose(void) {
    drv.max_w = 1280; drv.max_h = 720;
    drv.stride = 1280 * 2 + 64;                               /* rows padded for alignment */
    ZylCameraImage img;
    assert(zyl_hal_camera()->capture(ZYL_HAL_LENS_BACK, &img));
    assert(img.format == ZYL_HAL_PIXEL_YUYV && img.width == 1280 && img.height == 720);
    assert(img.size == 1280 * 720 * 2 && !memchr(img.data, 0xEE, img.size));   /* packed */
    zyl_hal_camera()->free_image(&img);
    assert(!img.data && img.size == 0);
    assert(zyl_camera_open(&cam, ZYL_CAMERA_BACK) == 0);
    assert(zyl_camera_capture(&cam, ROOT_ "/data/camera/IMG.yuv") == 0);
    assert(file_size(ROOT_ "/data/camera/IMG.yuv") == 1280 * 720 * 2);
}

static void test_bad_frames_save_nothing(void) {
    assert(zyl_camera_open(&cam, ZYL_CAMERA_BACK) == 0);
    drv.bogus_index = 7;                                      /* index past our buffers */
    assert(zyl_camera_capture(&cam, ROOT_ "/data/camera/IMG.yuv") == -1);
    drv.bogus_index = 0;
    drv.short_by = 1;                                         /* a frame missing its tail */
    assert(zyl_camera_capture(&cam, ROOT_ "/data/camera/IMG.yuv") == -1);
    drv.short_by = 0;
    drv.no_frames = true;                                     /* sensor never delivers */
    assert(zyl_camera_capture(&cam, ROOT_ "/data/camera/IMG.yuv") == -1);
    assert(!g_file_test(ROOT_ "/data/camera/IMG.yuv", G_FILE_TEST_EXISTS));
    assert(!drv.streaming && drv.mapped == 0);
}

int main(void) {
    printf("=== Zyl OS Camera Service Tests ===\n");
    RUN_TEST(test_open_takes_only_a_capture_device);
    RUN_TEST(test_capture_path_must_stay_in_the_camera_dir);
    RUN_TEST(test_capture_waits_for_the_first_frame_and_saves_it);
    RUN_TEST(test_captures_leave_no_buffers_or_devices_behind);
    RUN_TEST(test_driver_without_yuyv_is_refused);
    RUN_TEST(test_frame_size_is_what_the_driver_chose);
    RUN_TEST(test_bad_frames_save_nothing);
    reset();
    assert(system("rm -rf " ROOT_) == 0);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
