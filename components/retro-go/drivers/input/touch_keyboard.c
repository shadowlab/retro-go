#include "touch_keyboard.h"

// Share of the keyboard width of each button, in percent, in the order of the button row
static const struct
{
    tkb_target_t target;
    int percent;
} buttons[] = {
    {TKB_LAYOUT, 18}, {TKB_SPACE, 26}, {TKB_BACKSPACE, 16}, {TKB_CANCEL, 18}, {TKB_OK, 22},
};

#define BUTTON_COUNT (int)(sizeof(buttons) / sizeof(buttons[0]))

bool tkb_button_rect(const tkb_geometry_t *g, tkb_target_t target, int *x, int *y, int *w, int *h)
{
    if (g->button_h <= 0)
        return false;

    const int total = g->columns * g->key_w;
    int start = 0, cumulative = 0;

    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        cumulative += buttons[i].percent;
        // The last button takes what is left so that the row always covers the full width
        int end = (i == BUTTON_COUNT - 1) ? total : total * cumulative / 100;
        if (buttons[i].target == target)
        {
            *x = g->x + start;
            *y = g->y + g->rows * g->key_h;
            *w = end - start;
            *h = g->button_h;
            return true;
        }
        start = end;
    }
    return false;
}

tkb_hit_t tkb_hit(const tkb_geometry_t *g, int px, int py)
{
    const tkb_hit_t none = {TKB_NONE, -1};
    const int rx = px - g->x, ry = py - g->y;
    const int grid_w = g->columns * g->key_w, grid_h = g->rows * g->key_h;

    if (rx < 0 || rx >= grid_w || ry < 0)
        return none;

    if (ry < grid_h)
    {
        tkb_hit_t hit = {TKB_KEY, (ry / g->key_h) * g->columns + rx / g->key_w};
        return hit;
    }

    if (g->button_h > 0 && ry < grid_h + g->button_h)
    {
        for (int i = 0; i < BUTTON_COUNT; ++i)
        {
            int x, y, w, h;
            if (tkb_button_rect(g, buttons[i].target, &x, &y, &w, &h) && px >= x && px < x + w)
            {
                tkb_hit_t hit = {buttons[i].target, -1};
                return hit;
            }
        }
    }
    return none;
}
