/* ----------------------------------------------------------
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 입력 서비스 인터페이스 -- 가상 키보드, 하드웨어 키, 멀티터치, IME
 * 수행범위: ZylInputService 타입, 키보드/터치/키 매핑 함수 선언
 * 의존방향: stdbool.h
 * SOLID: ISP -- 입력 관련 인터페이스만 노출
 * ---------------------------------------------------------- */

#ifndef ZYL_INPUT_H
#define ZYL_INPUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- D-Bus Constants ---------------------------------------- */

#define ZYL_INPUT_DBUS_NAME  "org.zylos.InputService"
#define ZYL_INPUT_DBUS_PATH  "/org/zylos/InputService"
#define ZYL_INPUT_DBUS_IFACE "org.zylos.InputService"

/* -- Max touch points --------------------------------------- */

#define ZYL_MAX_TOUCH_POINTS 10

/* -- Enums -------------------------------------------------- */

typedef enum {
    ZYL_KEY_POWER,
    ZYL_KEY_VOLUME_UP,
    ZYL_KEY_VOLUME_DOWN,
    ZYL_KEY_BACK,
    ZYL_KEY_HOME,
    ZYL_KEY_MENU,
} ZylHardwareKey;

/* -- Structs ------------------------------------------------ */

typedef struct {
    int   id;       /* touch point ID (0 .. MAX_TOUCH_POINTS-1) */
    float x, y;     /* normalized 0.0 - 1.0 */
    bool  active;
} ZylTouchPoint;

typedef struct {
    bool visible;
    char layout[32];  /* "ko", "en", "ja", "num" */
} ZylKeyboardState;

/* Opaque service handle */
typedef struct ZylInputService ZylInputService;

/* -- Service Lifecycle -------------------------------------- */

ZylInputService *zyl_input_create(void);
void             zyl_input_destroy(ZylInputService *svc);

/* -- Virtual Keyboard --------------------------------------- */

int              zyl_input_show_keyboard(ZylInputService *svc, const char *layout);
int              zyl_input_hide_keyboard(ZylInputService *svc);
int              zyl_input_switch_layout(ZylInputService *svc, const char *layout);
ZylKeyboardState zyl_input_get_keyboard_state(const ZylInputService *svc);

/* -- Multi-touch -------------------------------------------- */

int  zyl_input_get_touch_points(ZylInputService *svc, ZylTouchPoint *out, int max);

/* -- Hardware Keys ------------------------------------------ */

void zyl_input_on_hardware_key(ZylInputService *svc, ZylHardwareKey key);

#ifdef __cplusplus
}
#endif

#endif /* ZYL_INPUT_H */
