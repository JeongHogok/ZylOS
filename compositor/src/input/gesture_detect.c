/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Policy
 *
 * 역할: 시스템 제스처 판정 구현 (zyl_gesture.h)
 * 수행범위: 가장자리 영역, 방향·임계값 판정
 * 의존방향: zyl_gesture.h
 * SOLID: SRP — 순수 함수만 (wlroots 없음)
 * ────────────────────────────────────────────────────────── */

#include "zyl_gesture.h"

static bool in_bottom(double y, int h, const struct zyl_config *c) { return y > h - c->swipe_from_bottom_px; }
static bool in_top(double y, const struct zyl_config *c) { return y < c->swipe_from_top_px; }
static bool in_left(double x, const struct zyl_config *c) { return x < c->swipe_from_side_px; }
static bool in_right(double x, int w, const struct zyl_config *c) { return x > w - c->swipe_from_side_px; }

bool gesture_at_edge(double x, double y, int screen_w, int screen_h,
                     const struct zyl_config *cfg)
{
    return in_bottom(y, screen_h, cfg) || in_top(y, cfg) ||
           in_left(x, cfg) || in_right(x, screen_w, cfg);
}

enum gesture_direction gesture_detect(const struct touch_state *t,
                                      int screen_w, int screen_h,
                                      const struct zyl_config *cfg)
{
    double dx     = t->current_x - t->start_x;
    double dy     = t->current_y - t->start_y;
    double abs_dx = dx > 0 ? dx : -dx;
    double abs_dy = dy > 0 ? dy : -dy;
    double min    = cfg->swipe_threshold_px;

    if (abs_dx < min && abs_dy < min)
        return GESTURE_NONE;

    /* Bottom edge: a long way up is home, a short one recents, along it the next app */
    if (in_bottom(t->start_y, screen_h, cfg)) {
        if (abs_dy >= abs_dx && dy < -3 * min) return GESTURE_SWIPE_UP;
        if (abs_dy >= abs_dx && dy < -min) return GESTURE_SWIPE_UP_SHORT;
        if (abs_dx > abs_dy) return GESTURE_SWIPE_RIGHT;
        return GESTURE_NONE;
    }

    /* Top edge: down opens the notification panel */
    if (in_top(t->start_y, cfg))
        return (abs_dy >= abs_dx && dy > min) ? GESTURE_SWIPE_DOWN : GESTURE_NONE;

    /* A side edge, moving inward: back */
    if (abs_dx > abs_dy) {
        if (in_left(t->start_x, cfg) && dx > min) return GESTURE_SWIPE_LEFT;
        if (in_right(t->start_x, screen_w, cfg) && dx < -min) return GESTURE_SWIPE_LEFT;
    }
    return GESTURE_NONE;
}
