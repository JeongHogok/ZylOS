/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 시스템 제스처 판정 — 어떤 터치가 시스템의 것이고 무엇을 뜻하는가
 * 수행범위: 제스처 종류, 설정값, 손가락 추적 상태, 판정 함수. 시스템 제스처는
 *           화면 가장자리에서 시작한 것뿐이다: 아래(길게 위로 홈, 짧게 최근 앱,
 *           아래쪽을 따라 옆으로 다음 앱), 위(알림판), 왼쪽·오른쪽 가장자리에서
 *           안쪽으로(뒤로).
 *           그 밖의 터치는 전부 앱의 것 — 가로 스크롤이 뒤로가기가 되지 않는다
 * 의존방향: 없음 (wlroots 없이 컴파일 — tests/test_gesture.c 가 이 구현을 그대로 시험)
 * SOLID: SRP — 판정만. 입력 장치·전달은 input/gesture.c
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_GESTURE_H
#define ZYL_GESTURE_H

#include <stdbool.h>
#include <stdint.h>

/* ─── Gesture direction identifiers ─── */
enum gesture_direction {
    GESTURE_NONE,
    GESTURE_SWIPE_UP,      /* from the bottom edge: home           */
    GESTURE_SWIPE_DOWN,    /* from the top edge: notification panel */
    GESTURE_SWIPE_LEFT,    /* from a side edge inward: back         */
    GESTURE_SWIPE_RIGHT,   /* along the bottom edge: the next app   */
    GESTURE_SWIPE_UP_SHORT,/* a short one up from the bottom: recents */
    GESTURE_DIRECTION_COUNT
};

/* ─── Runtime configuration (no #defines) ─── */
struct zyl_config {
    int swipe_threshold_px;    /* Min px to register a swipe          */
    int swipe_from_bottom_px;  /* Bottom-edge hot zone height         */
    int swipe_from_top_px;     /* Top-edge hot zone height            */
    int swipe_from_side_px;    /* Left/right-edge hot zone width      */
    int statusbar_height_px;   /* Pixels reserved for the status bar  */
};

/* ─── Multi-touch tracking ─── */
#define ZYL_MAX_TOUCH_POINTS 10

/**
 * Per-finger touch point, keyed by hardware touch_id.
 * Allocated from a fixed pool to avoid per-event heap churn.
 */
struct touch_point {
    bool     active;
    int32_t  id;            /* wlr touch_id (hardware assigned) */
    double   start_x, start_y;
    double   current_x, current_y;
    uint32_t start_time_ms;
    double   surface_dx, surface_dy;   /* layout − surface coordinates at touch down */
};

/* ─── Touch tracking state ─── */
struct touch_state {
    /* Primary touch — the one a gesture is read from */
    bool     active;
    double   start_x, start_y;
    double   current_x, current_y;
    uint32_t start_time_ms;
    enum gesture_direction pending;
    bool     at_edge;        /* began in a system zone: may become a gesture  */
    bool     claimed;        /* became one: the app's touches were cancelled */

    /* Multi-touch pool (indexed by slot, not touch_id) */
    struct touch_point points[ZYL_MAX_TOUCH_POINTS];
    int                num_active;   /* count of active fingers */
};

/* Does a touch starting here belong to a system gesture zone? */
bool gesture_at_edge(double x, double y, int screen_w, int screen_h,
                     const struct zyl_config *cfg);

/*
 * Which gesture (if any) the primary touch so far represents. A touch that
 * did not start at an edge is never one. GESTURE_NONE below the threshold.
 */
enum gesture_direction gesture_detect(const struct touch_state *t,
                                      int screen_w, int screen_h,
                                      const struct zyl_config *cfg);

#endif /* ZYL_GESTURE_H */
