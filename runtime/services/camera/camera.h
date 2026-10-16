/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 카메라 서비스 인터페이스 — 촬영
 * 수행범위: 카메라 열기/닫기, 사진 촬영 (하드웨어는 Camera HAL 로만)
 * 의존방향: stdbool.h
 * SOLID: ISP — 카메라 관련 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_CAMERA_H
#define ZYL_CAMERA_H

#include <stdbool.h>

typedef enum {
    ZYL_CAMERA_BACK  = 0,
    ZYL_CAMERA_FRONT = 1,
} ZylCameraFacing;

typedef struct ZylCameraService ZylCameraService;

ZylCameraService *zyl_camera_create(void);
void              zyl_camera_destroy(ZylCameraService *cam);

int  zyl_camera_open(ZylCameraService *cam, ZylCameraFacing facing);
void zyl_camera_close(ZylCameraService *cam);

int  zyl_camera_capture(ZylCameraService *cam,
                         const char *output_path);

bool zyl_camera_is_open(const ZylCameraService *cam);

#define ZYL_CAMERA_DBUS_NAME "org.zylos.CameraService"
#define ZYL_CAMERA_DBUS_PATH "/org/zylos/CameraService"

#endif /* ZYL_CAMERA_H */
