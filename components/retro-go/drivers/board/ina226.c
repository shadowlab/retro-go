#include "ina226.h"

// Only built for targets with this chip, define INA226_STANDALONE to build it on a host
#if defined(RG_TARGET_M5STACK_TAB5) || defined(INA226_STANDALONE)

float ina226_bus_volts(int16_t raw)
{
    return raw * 0.00125f;
}

float ina226_shunt_amps(int16_t raw, float shunt_ohms)
{
    return (raw * 0.0000025f) / shunt_ohms;
}

float tab5_battery_percent(float pack_volts)
{
    // Open circuit voltage per cell of a typical Li-ion cell. The pack is measured under load or while charging,
    // so this is an estimate at best.
    static const struct { float volts, percent; } curve[] = {
        {3.30f, 0.f}, {3.50f, 8.f}, {3.65f, 20.f}, {3.75f, 40.f}, {3.85f, 60.f}, {4.00f, 85.f}, {4.15f, 100.f},
    };
    const int count = sizeof(curve) / sizeof(curve[0]);
    const float cell = pack_volts / 2.f;

    if (cell <= curve[0].volts)
        return 0.f;
    for (int i = 1; i < count; ++i)
    {
        if (cell <= curve[i].volts)
        {
            float t = (cell - curve[i - 1].volts) / (curve[i].volts - curve[i - 1].volts);
            return curve[i - 1].percent + t * (curve[i].percent - curve[i - 1].percent);
        }
    }
    return 100.f;
}

#endif
