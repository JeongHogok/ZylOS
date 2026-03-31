/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: compositor/input 회귀 테스트 (멀티터치 슬롯 관리 + primary touch 보존)
 * 수행범위: wlroots 의존성 없이 touch slot allocator / primary swipe state 검증
 * 의존방향: 없음 (gesture.c 핵심 로직을 독립 복제)
 * SOLID: SRP — compositor input regression만 테스트
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ZYL_MAX_TOUCH_POINTS 10

enum gesture_direction {
    GESTURE_NONE,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN,
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT,
    GESTURE_DIRECTION_COUNT
};

struct zyl_config {
    int swipe_threshold_px;
    int swipe_from_bottom_px;
    int swipe_from_top_px;
    int statusbar_height_px;
};

struct touch_point {
    bool     active;
    int32_t  id;
    double   start_x, start_y;
    double   current_x, current_y;
    uint32_t start_time_ms;
};

struct touch_state {
    bool     active;
    double   start_x, start_y;
    double   current_x, current_y;
    uint32_t start_time_ms;
    enum gesture_direction pending;

    struct touch_point points[ZYL_MAX_TOUCH_POINTS];
    int                num_active;
};

static enum gesture_direction gesture_detect(const struct touch_state *t,
                                             int screen_h,
                                             const struct zyl_config *cfg)
{
    double dx     = t->current_x - t->start_x;
    double dy     = t->current_y - t->start_y;
    double abs_dx = dx > 0 ? dx : -dx;
    double abs_dy = dy > 0 ? dy : -dy;

    if (abs_dx < cfg->swipe_threshold_px &&
        abs_dy < cfg->swipe_threshold_px)
        return GESTURE_NONE;

    if (t->start_y > screen_h - cfg->swipe_from_bottom_px &&
        dy < -cfg->swipe_threshold_px)
        return GESTURE_SWIPE_UP;

    if (t->start_y < cfg->swipe_from_top_px &&
        dy > cfg->swipe_threshold_px)
        return GESTURE_SWIPE_DOWN;

    if (abs_dx > abs_dy) {
        if (dx < -cfg->swipe_threshold_px)
            return GESTURE_SWIPE_LEFT;
        if (dx > cfg->swipe_threshold_px)
            return GESTURE_SWIPE_RIGHT;
    }

    return GESTURE_NONE;
}

static int touch_find_slot(struct touch_state *ts, int32_t id)
{
    for (int i = 0; i < ZYL_MAX_TOUCH_POINTS; i++) {
        if (ts->points[i].active && ts->points[i].id == id)
            return i;
    }
    return -1;
}

static int touch_alloc_slot(struct touch_state *ts)
{
    for (int i = 0; i < ZYL_MAX_TOUCH_POINTS; i++) {
        if (!ts->points[i].active)
            return i;
    }
    return -1;
}

static void simulate_touch_down(struct touch_state *ts, int32_t id,
                                double x, double y)
{
    int slot = touch_alloc_slot(ts);
    assert(slot >= 0);

    ts->points[slot].active        = true;
    ts->points[slot].id            = id;
    ts->points[slot].start_x       = x;
    ts->points[slot].start_y       = y;
    ts->points[slot].current_x     = x;
    ts->points[slot].current_y     = y;
    ts->points[slot].start_time_ms = 1;
    ts->num_active++;

    if (!ts->active) {
        ts->active        = true;
        ts->start_x       = x;
        ts->start_y       = y;
        ts->current_x     = x;
        ts->current_y     = y;
        ts->start_time_ms = 1;
        ts->pending       = GESTURE_NONE;
    }
}

static void simulate_touch_motion(struct touch_state *ts, int32_t id,
                                  double x, double y)
{
    int slot = touch_find_slot(ts, id);
    assert(slot >= 0);

    ts->points[slot].current_x = x;
    ts->points[slot].current_y = y;

    if (ts->active) {
        if (ts->num_active == 1 || slot == 0) {
            ts->current_x = x;
            ts->current_y = y;
        }
    }
}

static void simulate_touch_up(struct touch_state *ts, int32_t id)
{
    int slot = touch_find_slot(ts, id);
    assert(slot >= 0);
    ts->points[slot].active = false;
    ts->num_active--;

    if (ts->num_active == 0)
        ts->active = false;
}

static struct zyl_config default_cfg(void)
{
    return (struct zyl_config){
        .swipe_threshold_px = 50,
        .swipe_from_bottom_px = 40,
        .swipe_from_top_px = 40,
        .statusbar_height_px = 36,
    };
}

static void test_slot_allocate_find_release_reuse(void)
{
    struct touch_state ts;
    memset(&ts, 0, sizeof(ts));

    simulate_touch_down(&ts, 11, 10, 10);
    simulate_touch_down(&ts, 22, 20, 20);

    assert(touch_find_slot(&ts, 11) == 0);
    assert(touch_find_slot(&ts, 22) == 1);
    assert(ts.num_active == 2);

    simulate_touch_up(&ts, 11);
    assert(touch_find_slot(&ts, 11) == -1);
    assert(ts.num_active == 1);

    int slot = touch_alloc_slot(&ts);
    assert(slot == 0);
}

static void test_pool_exhaustion_limit(void)
{
    struct touch_state ts;
    memset(&ts, 0, sizeof(ts));

    for (int i = 0; i < ZYL_MAX_TOUCH_POINTS; i++) {
        simulate_touch_down(&ts, i + 1, i, i);
    }

    assert(ts.num_active == ZYL_MAX_TOUCH_POINTS);
    assert(touch_alloc_slot(&ts) == -1);
}

static void test_secondary_finger_does_not_override_primary_swipe(void)
{
    struct touch_state ts;
    memset(&ts, 0, sizeof(ts));
    struct zyl_config cfg = default_cfg();

    simulate_touch_down(&ts, 1, 540.0, 1890.0);
    simulate_touch_down(&ts, 2, 100.0, 100.0);

    simulate_touch_motion(&ts, 2, 900.0, 900.0);
    assert(ts.current_x == 540.0);
    assert(ts.current_y == 1890.0);

    simulate_touch_motion(&ts, 1, 540.0, 1760.0);
    assert(ts.current_x == 540.0);
    assert(ts.current_y == 1760.0);

    assert(gesture_detect(&ts, 1920, &cfg) == GESTURE_SWIPE_UP);
}

int main(void)
{
    printf("=== compositor touch regression tests ===\n");

    test_slot_allocate_find_release_reuse();
    printf("  test_slot_allocate_find_release_reuse        PASS\n");

    test_pool_exhaustion_limit();
    printf("  test_pool_exhaustion_limit                   PASS\n");

    test_secondary_finger_does_not_override_primary_swipe();
    printf("  test_secondary_finger_does_not_override_primary_swipe PASS\n");

    printf("All compositor touch regression tests passed.\n");
    return 0;
}
