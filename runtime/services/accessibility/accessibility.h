/* ----------------------------------------------------------
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 접근성 서비스 인터페이스 — 고대비, 폰트 스케일링, 스크린리더 상태
 * 수행범위: ZylAccessibilityState 타입, D-Bus 서비스 선언
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 접근성 제어 인터페이스만 노출
 * ---------------------------------------------------------- */

#ifndef ZYL_ACCESSIBILITY_H
#define ZYL_ACCESSIBILITY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- D-Bus Constants ---------------------------------------- */

#define ZYL_ACCESSIBILITY_DBUS_NAME  "org.zylos.Accessibility"
#define ZYL_ACCESSIBILITY_DBUS_PATH  "/org/zylos/Accessibility"
#define ZYL_ACCESSIBILITY_DBUS_IFACE "org.zylos.Accessibility"

/* -- Font scale bounds -------------------------------------- */

#define ZYL_FONT_SCALE_MIN 1.0
#define ZYL_FONT_SCALE_MAX 3.0

/* -- Accessibility state ------------------------------------ */

typedef struct {
    bool   high_contrast;
    double font_scale;           /* 1.0 - 3.0 */
    bool   screen_reader_active;
} ZylAccessibilityState;

/* Opaque service handle */
typedef struct _ZylAccessibilityService ZylAccessibilityService;

/* -- Service Lifecycle -------------------------------------- */

ZylAccessibilityService *zyl_accessibility_service_create(void);
void                     zyl_accessibility_service_destroy(ZylAccessibilityService *svc);

/* -- Setters ------------------------------------------------ */

void zyl_accessibility_set_high_contrast(ZylAccessibilityService *svc,
                                         bool enabled);
bool zyl_accessibility_set_font_scale(ZylAccessibilityService *svc,
                                      double scale);

/* -- Getters ------------------------------------------------ */

ZylAccessibilityState zyl_accessibility_get_state(const ZylAccessibilityService *svc);

#ifdef __cplusplus
}
#endif

#endif /* ZYL_ACCESSIBILITY_H */
