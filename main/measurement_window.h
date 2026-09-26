#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool active;
    bool physics;
    uint32_t generation;
    int64_t start_ms;
} measurement_window_t;

typedef enum { WINDOW_WAIT, WINDOW_RESET, WINDOW_COMPLETE } window_action_t;

// A generation change remembers a brief HV transition even if it completed
// between observations. A physics-state change also starts a fresh interval.
static inline window_action_t measurement_window_step(measurement_window_t *w,
        int64_t now_ms, uint32_t generation, bool enabled, bool physics)
{
    if (!enabled) {
        bool was_active = w->active;
        w->active = false;
        return was_active ? WINDOW_RESET : WINDOW_WAIT;
    }
    if (!w->active || generation != w->generation || physics != w->physics) {
        *w = (measurement_window_t){true, physics, generation, now_ms};
        return WINDOW_RESET;
    }
    return now_ms - w->start_ms >= 60000 ? WINDOW_COMPLETE : WINDOW_WAIT;
}

static inline bool measurement_physics_valid(const measurement_window_t *w,
        int64_t now_ms, uint32_t generation, bool ready)
{
    int64_t duration = now_ms - w->start_ms;
    return w->active && w->physics && ready && generation == w->generation &&
           duration >= 60000 && duration <= 61000;
}
