#include "hid_parser.h"
#include <string.h>

#define MAX_USAGES 64

typedef struct
{
    uint16_t page;
    int32_t lmin, lmax;
    uint32_t size, count;
    uint8_t id;
} globals_t;

static int32_t read_item(const uint8_t *data, int size, bool is_signed)
{
    uint32_t v = 0;
    for (int i = 0; i < size; ++i)
        v |= (uint32_t)data[i] << (8 * i);
    if (is_signed && size > 0 && size < 4 && (v & (1u << (size * 8 - 1))))
        v |= ~0u << (size * 8);
    return (int32_t)v;
}

bool hid_parse_gamepad_descriptor(const uint8_t *desc, size_t len, hid_gamepad_desc_t *out)
{
    globals_t g = {0}, stack[4];
    int sp = 0;
    uint32_t usages[MAX_USAGES]; // (page << 16) | usage
    size_t n_usages = 0;
    int32_t umin = -1, umax = -1;
    int depth = 0;
    bool in_pad = false;
    bool has_dpad = false, has_button = false;
    uint32_t offsets[256] = {0};
    bool id_chosen = false;

    memset(out, 0, sizeof(*out));

    for (size_t i = 0; i < len;)
    {
        uint8_t prefix = desc[i++];
        if (prefix == 0xFE) // long item, skip
        {
            if (i >= len) break;
            i += 2 + desc[i];
            continue;
        }
        int size = prefix & 3;
        if (size == 3) size = 4;
        int type = (prefix >> 2) & 3;
        int tag = prefix >> 4;
        if (i + size > len)
            break;
        const uint8_t *d = &desc[i];
        i += size;

        if (type == 1) // Global
        {
            switch (tag)
            {
            case 0: g.page = read_item(d, size, false); break;
            case 1: g.lmin = read_item(d, size, true); break;
            case 2: g.lmax = read_item(d, size, g.lmin < 0); break;
            case 7: g.size = read_item(d, size, false); break;
            case 8: g.id = read_item(d, size, false); break;
            case 9: g.count = read_item(d, size, false); break;
            case 10: if (sp < 4) stack[sp++] = g; break; // push
            case 11: if (sp > 0) g = stack[--sp]; break; // pop
            }
        }
        else if (type == 2) // Local
        {
            uint32_t v = read_item(d, size, false);
            switch (tag)
            {
            case 0:
                if (n_usages < MAX_USAGES)
                    usages[n_usages++] = (size == 4) ? v : ((uint32_t)g.page << 16) | (v & 0xFFFF);
                break;
            case 1: umin = v; break;
            case 2: umax = v; break;
            }
        }
        else if (type == 0) // Main
        {
            if (tag == 0xA) // Collection
            {
                if (depth == 0 && n_usages > 0)
                {
                    uint32_t u = usages[0];
                    in_pad = (u >> 16) == 1 && ((u & 0xFFFF) == 4 || (u & 0xFFFF) == 5);
                }
                depth++;
            }
            else if (tag == 0xC) // End collection
            {
                if (depth > 0 && --depth == 0)
                    in_pad = false;
            }
            else if (tag == 8) // Input
            {
                uint32_t flags = read_item(d, size, false);
                bool constant = flags & 1, variable = flags & 2;
                if (in_pad && !constant && variable && (!id_chosen || g.id == out->report_id))
                {
                    for (uint32_t n = 0; n < g.count; ++n)
                    {
                        uint32_t u;
                        if (umin >= 0 && umax >= umin)
                            u = ((uint32_t)g.page << 16) | ((umin + n <= (uint32_t)umax) ? umin + n : umax);
                        else if (n < n_usages)
                            u = usages[n];
                        else if (n_usages > 0)
                            u = usages[n_usages - 1];
                        else
                            continue;

                        uint32_t page = u >> 16, usage = u & 0xFFFF;
                        int ftype = -1;
                        if (page == 9 && usage > 0)
                            ftype = HID_FIELD_BUTTON;
                        else if (page == 1 && usage >= 0x30 && usage <= 0x35)
                            ftype = HID_FIELD_AXIS;
                        else if (page == 1 && usage == 0x39)
                            ftype = HID_FIELD_HAT;

                        if (ftype >= 0 && out->count < HID_MAX_FIELDS && g.size <= 32)
                        {
                            out->fields[out->count++] = (hid_field_t){
                                .type = ftype,
                                .size = g.size,
                                .usage = usage,
                                .bit_offset = offsets[g.id] + n * g.size,
                                .min = g.lmin,
                                .max = g.lmax,
                            };
                            out->report_id = g.id;
                            id_chosen = true;
                            if (ftype == HID_FIELD_BUTTON && usage <= 32)
                                has_button = true;
                            else if (ftype == HID_FIELD_HAT || (ftype == HID_FIELD_AXIS && usage <= HID_USAGE_Y))
                                has_dpad = true;
                        }
                    }
                }
                offsets[g.id] += g.size * g.count;
            }
            else if (tag == 9 || tag == 0xB) // Output / Feature use no input bits
            {
            }
            n_usages = 0;
            umin = umax = -1;
        }
    }

    out->valid = has_button || has_dpad;
    return out->valid;
}

static uint32_t extract_bits(const uint8_t *data, size_t len, uint32_t bit, uint32_t size, bool *ok)
{
    uint32_t v = 0;
    if ((bit + size + 7) / 8 > len)
    {
        *ok = false;
        return 0;
    }
    for (uint32_t i = 0; i < size; ++i, ++bit)
        v |= (uint32_t)((data[bit >> 3] >> (bit & 7)) & 1) << i;
    return v;
}

bool hid_decode_gamepad_report(const hid_gamepad_desc_t *desc, const uint8_t *report, size_t len,
                               uint8_t *dpad, uint32_t *buttons)
{
    if (!desc->valid || len == 0)
        return false;

    if (desc->report_id != 0)
    {
        if (report[0] != desc->report_id)
            return false;
        report++, len--;
    }

    uint8_t pad = 0;
    uint32_t btn = 0;
    bool ok = true;

    for (size_t i = 0; i < desc->count; ++i)
    {
        const hid_field_t *f = &desc->fields[i];
        uint32_t raw = extract_bits(report, len, f->bit_offset, f->size, &ok);
        if (!ok)
            return false;

        if (f->type == HID_FIELD_BUTTON)
        {
            if (raw && f->usage <= 32)
                btn |= 1u << (f->usage - 1);
        }
        else if (f->type == HID_FIELD_HAT)
        {
            int32_t v = (int32_t)raw;
            if (v < f->min || v > f->max)
                continue; // neutral
            int pos = v - f->min;
            if (f->max - f->min == 3)
                pos *= 2; // 4 position hat
            static const uint8_t hat_map[8] = {
                HID_PAD_UP, HID_PAD_UP | HID_PAD_RIGHT, HID_PAD_RIGHT, HID_PAD_DOWN | HID_PAD_RIGHT,
                HID_PAD_DOWN, HID_PAD_DOWN | HID_PAD_LEFT, HID_PAD_LEFT, HID_PAD_UP | HID_PAD_LEFT,
            };
            pad |= hat_map[pos & 7];
        }
        else if (f->usage == HID_USAGE_X || f->usage == HID_USAGE_Y)
        {
            int32_t v = (int32_t)raw;
            if (f->min < 0 && f->size < 32 && (raw & (1u << (f->size - 1))))
                v = (int32_t)(raw | (~0u << f->size));
            int64_t range = (int64_t)f->max - f->min;
            if (range <= 0)
                continue;
            int pos = (int)(((int64_t)(v - f->min) * 100) / range);
            if (f->usage == HID_USAGE_X)
                pad |= pos < 30 ? HID_PAD_LEFT : pos > 70 ? HID_PAD_RIGHT : 0;
            else
                pad |= pos < 30 ? HID_PAD_UP : pos > 70 ? HID_PAD_DOWN : 0;
        }
    }

    *dpad = pad;
    *buttons = btn;
    return true;
}
