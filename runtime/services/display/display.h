/* ----------------------------------------------------------
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 디스플레이 관리 서비스 인터페이스 -- 모드, 스케일, 회전 관리
 * 수행범위: DRM/KMS 모드 열거, 해상도 설정, DPI 스케일링, 화면 회전
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP -- 디스플레이 제어 인터페이스만 노출
 * ---------------------------------------------------------- */

#ifndef ZYL_DISPLAY_H
#define ZYL_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

/* --- Display mode descriptor --- */
typedef struct {
    int width, height;
    int refresh_hz;
    char connector[32]; /* HDMI-A-1, DSI-1, etc */
} ZylDisplayMode;

/* --- Rotation enum --- */
typedef enum {
    ZYL_ROTATION_0   = 0,
    ZYL_ROTATION_90  = 90,
    ZYL_ROTATION_180 = 180,
    ZYL_ROTATION_270 = 270,
} ZylRotation;

/* --- Opaque service handle --- */
typedef struct ZylDisplayService ZylDisplayService;

/* Service lifecycle */
ZylDisplayService *zyl_display_create(void);
void               zyl_display_destroy(ZylDisplayService *svc);

/* Mode management */
int  zyl_display_get_modes(ZylDisplayService *svc,
                           ZylDisplayMode **out, int *count);
int  zyl_display_set_mode(ZylDisplayService *svc,
                          int width, int height, int hz);
int  zyl_display_get_current_mode(ZylDisplayService *svc,
                                  ZylDisplayMode *out);

/* DPI scaling */
int   zyl_display_set_scale(ZylDisplayService *svc, float scale);
float zyl_display_get_scale(const ZylDisplayService *svc);

/* Rotation */
int         zyl_display_set_rotation(ZylDisplayService *svc,
                                     ZylRotation rot);
ZylRotation zyl_display_get_rotation(const ZylDisplayService *svc);
int         zyl_display_set_auto_rotate(ZylDisplayService *svc,
                                        bool enabled);

/* D-Bus constants */
#define ZYL_DISPLAY_DBUS_NAME "org.zylos.DisplayManager"
#define ZYL_DISPLAY_DBUS_PATH "/org/zylos/DisplayManager"

#endif /* ZYL_DISPLAY_H */
