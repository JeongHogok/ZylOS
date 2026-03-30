/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 카메라 서비스 인터페이스 — V4L2 기반 캡처, 프리뷰, 촬영
 * 수행범위: 디바이스 열기/닫기, 프리뷰 스트림, 사진 촬영, 설정 변경
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 카메라 관련 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_CAMERA_H
#define ZYL_CAMERA_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    ZYL_CAMERA_BACK  = 0,
    ZYL_CAMERA_FRONT = 1,
} ZylCameraFacing;

typedef enum {
    ZYL_CAMERA_FMT_JPEG = 0,
    ZYL_CAMERA_FMT_NV21 = 1,
    ZYL_CAMERA_FMT_YUYV = 2,
} ZylCameraFormat;

typedef struct {
    uint32_t width;
    uint32_t height;
    ZylCameraFormat format;
    uint32_t fps;
} ZylCameraConfig;

typedef struct {
    void   *data;
    size_t  size;
    uint32_t width;
    uint32_t height;
    ZylCameraFormat format;
    uint64_t timestamp_us;
} ZylCameraFrame;

typedef void (*zyl_camera_frame_cb)(const ZylCameraFrame *frame,
                                     void *user_data);

typedef struct ZylCameraService ZylCameraService;

ZylCameraService *zyl_camera_create(void);
void              zyl_camera_destroy(ZylCameraService *cam);

int  zyl_camera_open(ZylCameraService *cam, ZylCameraFacing facing);
void zyl_camera_close(ZylCameraService *cam);

int  zyl_camera_start_preview(ZylCameraService *cam,
                               const ZylCameraConfig *config,
                               zyl_camera_frame_cb callback,
                               void *user_data);
void zyl_camera_stop_preview(ZylCameraService *cam);

int  zyl_camera_capture(ZylCameraService *cam,
                         const char *output_path);

int  zyl_camera_get_supported_resolutions(ZylCameraService *cam,
                                           ZylCameraConfig **out,
                                           int *count);

bool zyl_camera_is_open(const ZylCameraService *cam);

#define ZYL_CAMERA_DBUS_NAME "org.zylos.CameraService"
#define ZYL_CAMERA_DBUS_PATH "/org/zylos/CameraService"

#endif /* ZYL_CAMERA_H */
