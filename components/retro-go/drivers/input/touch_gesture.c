#include "touch_gesture.h"
#include <stdlib.h>

static void queue_press(touch_gesture_t *g, uint32_t keys)
{
    g->queued |= keys;
}

uint32_t touch_gesture_update(touch_gesture_t *g, const touch_keys_t *k, bool ui_active, bool touching, int x,
                              int y, int64_t now)
{
    if (touching && !g->down)
    {
        g->down = true;
        g->moved = g->long_fired = false;
        g->axis = 0;
        g->start_x = g->anchor_x = x;
        g->start_y = g->anchor_y = y;
        g->down_time = now;
    }
    else if (touching)
    {
        if (ui_active)
        {
            int dx = x - g->anchor_x, dy = y - g->anchor_y;
            if (g->axis == 0)
            {
                if (abs(x - g->start_x) >= TOUCH_STEP_X / 2 && abs(x - g->start_x) > abs(y - g->start_y))
                    g->axis = 1;
                else if (abs(y - g->start_y) >= TOUCH_STEP_Y / 2)
                    g->axis = 2;
            }
            if (g->axis == 2 && abs(dy) >= TOUCH_STEP_Y)
            {
                queue_press(g, dy < 0 ? k->down : k->up); // content follows the finger
                g->anchor_y += dy < 0 ? -TOUCH_STEP_Y : TOUCH_STEP_Y;
                g->moved = true;
            }
            else if (g->axis == 1 && abs(dx) >= TOUCH_STEP_X)
            {
                queue_press(g, dx < 0 ? k->left : k->right);
                g->anchor_x += dx < 0 ? -TOUCH_STEP_X : TOUCH_STEP_X;
                g->moved = true;
            }
            else if (g->axis != 0)
            {
                g->moved = true;
            }
            if (!g->moved && !g->long_fired && now - g->down_time >= TOUCH_LONG_US)
            {
                queue_press(g, k->b);
                g->long_fired = true;
            }
        }
    }
    else if (g->down)
    {
        g->down = false;
        bool tap = !g->moved && !g->long_fired && abs(g->anchor_x - g->start_x) < TOUCH_STEP_X / 2
                   && abs(g->anchor_y - g->start_y) < TOUCH_STEP_Y / 2 && now - g->down_time < TOUCH_TAP_MAX_US;
        if (tap)
        {
            if (ui_active)
                queue_press(g, k->a);
            else if (g->start_x < TOUCH_CORNER_W && g->start_y < TOUCH_CORNER_H)
                queue_press(g, k->menu);
        }
    }

    // Presses are emitted as short pulses with a gap so that consecutive ones register as separate events
    if (g->pulse_keys && now >= g->pulse_end)
    {
        g->pulse_keys = 0;
        g->pulse_end = now + TOUCH_GAP_US;
    }
    if (!g->pulse_keys && g->queued && now >= g->pulse_end)
    {
        // Take a single direction/button at a time
        uint32_t next = g->queued & -g->queued;
        g->queued &= ~next;
        g->pulse_keys = next;
        g->pulse_end = now + TOUCH_PULSE_US;
    }
    return g->pulse_keys;
}
