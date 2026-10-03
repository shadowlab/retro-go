#include "rx8130.h"

// Only built for targets with this chip, define RX8130_STANDALONE to build it on a host
#if defined(RG_TARGET_M5STACK_TAB5) || defined(RX8130_STANDALONE)

static uint8_t to_bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static int from_bcd(uint8_t v, int min, int max)
{
    if ((v & 0x0F) > 9 || (v >> 4) > 9)
        return -1;
    int n = (v >> 4) * 10 + (v & 0x0F);
    return (n < min || n > max) ? -1 : n;
}

// Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's algorithm), no timezone involved
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = (int)(y - era * 400);
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void rx8130_encode(time_t utc, uint8_t regs[7])
{
    struct tm tm;
    gmtime_r(&utc, &tm);
    regs[0] = to_bcd(tm.tm_sec);
    regs[1] = to_bcd(tm.tm_min);
    regs[2] = to_bcd(tm.tm_hour); // 24 hour clock
    regs[3] = (uint8_t)(1 << tm.tm_wday); // One bit per day: Sunday = 0x01 ... Saturday = 0x40
    regs[4] = to_bcd(tm.tm_mday);
    regs[5] = to_bcd(tm.tm_mon + 1); // 1-12
    regs[6] = to_bcd(tm.tm_year % 100);
}

bool rx8130_decode(const uint8_t regs[7], time_t *utc)
{
    int sec = from_bcd(regs[0] & 0x7F, 0, 59);
    int min = from_bcd(regs[1] & 0x7F, 0, 59);
    int hour = from_bcd(regs[2] & 0x3F, 0, 23);
    int day = from_bcd(regs[4] & 0x3F, 1, 31);
    int mon = from_bcd(regs[5] & 0x1F, 1, 12);
    int year = from_bcd(regs[6], 0, 99);
    if (sec < 0 || min < 0 || hour < 0 || day < 0 || mon < 0 || year < 0)
        return false;
    // The weekday register is redundant, it is ignored. The chip only holds two digits of the year (2000-2099).
    *utc = (time_t)(days_from_civil(2000 + year, mon, day) * 86400 + hour * 3600 + min * 60 + sec);
    return true;
}

#endif
