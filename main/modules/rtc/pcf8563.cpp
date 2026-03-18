#include "pcf8563.h"
#include "board/i2c_port.h"

#include <stddef.h>

/* ---------------- Register map ---------------- */
#define PCF8563_REG_CONTROL_STATUS_1   0x00u
#define PCF8563_REG_CONTROL_STATUS_2   0x01u
#define PCF8563_REG_VL_SECONDS         0x02u
#define PCF8563_REG_MINUTES            0x03u
#define PCF8563_REG_HOURS              0x04u
#define PCF8563_REG_DAYS               0x05u
#define PCF8563_REG_WEEKDAYS           0x06u
#define PCF8563_REG_CENTURY_MONTHS     0x07u
#define PCF8563_REG_YEARS              0x08u
#define PCF8563_REG_MINUTE_ALARM       0x09u
#define PCF8563_REG_HOUR_ALARM         0x0Au
#define PCF8563_REG_DAY_ALARM          0x0Bu
#define PCF8563_REG_WEEKDAY_ALARM      0x0Cu
#define PCF8563_REG_CLKOUT_CONTROL     0x0Du
#define PCF8563_REG_TIMER_CONTROL      0x0Eu
#define PCF8563_REG_TIMER              0x0Fu

/* ---------------- Bit definitions ---------------- */
/* Control_status_1 */
#define PCF8563_BIT_CS1_TEST1          (1u << 7)
#define PCF8563_BIT_CS1_STOP           (1u << 5)
#define PCF8563_BIT_CS1_TESTC          (1u << 3)

/* Control_status_2 */
#define PCF8563_BIT_CS2_TI_TP          (1u << 4)
#define PCF8563_BIT_CS2_AF             (1u << 3)
#define PCF8563_BIT_CS2_TF             (1u << 2)
#define PCF8563_BIT_CS2_AIE            (1u << 1)
#define PCF8563_BIT_CS2_TIE            (1u << 0)

/* VL_seconds */
#define PCF8563_BIT_VL                 (1u << 7)

/* Century_months */
#define PCF8563_BIT_CENTURY            (1u << 7)

/* Alarm disable bits */
#define PCF8563_BIT_AE                 (1u << 7)

/* CLKOUT_control */
#define PCF8563_BIT_CLKOUT_FE          (1u << 7)

/* Timer_control */
#define PCF8563_BIT_TIMER_TE           (1u << 7)

/* ---------------- Internal helpers ---------------- */

static uint8_t pcf8563_dec_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}

static uint8_t pcf8563_bcd_to_dec(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10u) + (value & 0x0Fu));
}

static bool pcf8563_is_leap_year(uint16_t year)
{
    /* Good enough for this range; RTC itself only tracks 2-digit year. */
    return ((year % 4u) == 0u);
}

static uint8_t pcf8563_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] =
    {
        31u, 28u, 31u, 30u, 31u, 30u,
        31u, 31u, 30u, 31u, 30u, 31u
    };

    if ((month < 1u) || (month > 12u))
    {
        return 0u;
    }

    if ((month == 2u) && pcf8563_is_leap_year(year))
    {
        return 29u;
    }

    return days[month - 1u];
}

static bool pcf8563_is_valid_time(const PCF8563_Time *time)
{
    if (time == NULL)
    {
        return false;
    }

    return (time->second <= 59u) &&
           (time->minute <= 59u) &&
           (time->hour   <= 23u);
}

static bool pcf8563_is_valid_date(const PCF8563_Date *date)
{
    uint8_t dim;

    if (date == NULL)
    {
        return false;
    }

    if ((date->weekday > 6u) ||
        (date->month < 1u) || (date->month > 12u))
    {
        return false;
    }

    if ((date->year < PCF8563_CENTURY_BASE) ||
        (date->year > (PCF8563_CENTURY_BASE + 199u)))
    {
        return false;
    }

    dim = pcf8563_days_in_month(date->year, date->month);

    return (date->day >= 1u) && (date->day <= dim);
}

static PCF8563_Status pcf8563_write_regs(uint8_t start_reg,
                                         const uint8_t *data,
                                         uint8_t len)
{
    uint8_t buf[1u + 16u];
    uint8_t i;

    if ((data == NULL) || (len == 0u) || (len > 16u))
    {
        return PCF8563_INVALID_ARG;
    }

    buf[0] = start_reg;
    for (i = 0u; i < len; ++i)
    {
        buf[1u + i] = data[i];
    }

    return i2c_port_write(PCF8563_I2C_ADDR_7BIT, buf, (size_t)(len + 1u))
           ? PCF8563_OK
           : PCF8563_I2C_ERROR;
}

static PCF8563_Status pcf8563_read_regs(uint8_t start_reg,
                                        uint8_t *data,
                                        uint8_t len)
{
    if ((data == NULL) || (len == 0u))
    {
        return PCF8563_INVALID_ARG;
    }

    return i2c_port_write_read(PCF8563_I2C_ADDR_7BIT,
                               &start_reg, 1u,
                               data, len)
           ? PCF8563_OK
           : PCF8563_I2C_ERROR;
}

static PCF8563_Status pcf8563_write_reg(uint8_t reg, uint8_t value)
{
    return pcf8563_write_regs(reg, &value, 1u);
}

static PCF8563_Status pcf8563_read_reg(uint8_t reg, uint8_t *value)
{
    return pcf8563_read_regs(reg, value, 1u);
}

static PCF8563_Status pcf8563_read_cs2(uint8_t *cs2)
{
    return pcf8563_read_reg(PCF8563_REG_CONTROL_STATUS_2, cs2);
}

/*
 * AF/TF have special write semantics:
 * - write 0 => clear flag
 * - write 1 => leave unchanged
 */
static PCF8563_Status pcf8563_write_cs2(bool preserve_af,
                                        bool preserve_tf,
                                        bool aie,
                                        bool tie,
                                        bool ti_tp)
{
    uint8_t value = 0u;

    if (ti_tp)       { value |= PCF8563_BIT_CS2_TI_TP; }
    if (preserve_af) { value |= PCF8563_BIT_CS2_AF;    }
    if (preserve_tf) { value |= PCF8563_BIT_CS2_TF;    }
    if (aie)         { value |= PCF8563_BIT_CS2_AIE;   }
    if (tie)         { value |= PCF8563_BIT_CS2_TIE;   }

    return pcf8563_write_reg(PCF8563_REG_CONTROL_STATUS_2, value);
}

static PCF8563_Status pcf8563_read_time_block(uint8_t regs[7])
{
    return pcf8563_read_regs(PCF8563_REG_VL_SECONDS, regs, 7u);
}

static PCF8563_Status pcf8563_write_time_block(const uint8_t regs[7])
{
    return pcf8563_write_regs(PCF8563_REG_VL_SECONDS, regs, 7u);
}

/* ---------------- Public API ---------------- */

PCF8563_Status pcf8563_init(void)
{
    PCF8563_Status st;
    uint8_t alarm_disable[4] = { 0x80u, 0x80u, 0x80u, 0x80u };
    uint8_t sec_reg = 0u;

    /* Normal mode, clock running, no POR override */
    st = pcf8563_write_reg(PCF8563_REG_CONTROL_STATUS_1, 0x00u);
    if (st != PCF8563_OK) return st;

    /* Clear AF/TF and disable alarm/timer interrupts */
    st = pcf8563_write_reg(PCF8563_REG_CONTROL_STATUS_2, 0x00u);
    if (st != PCF8563_OK) return st;

    /* Disable all alarm compares */
    st = pcf8563_write_regs(PCF8563_REG_MINUTE_ALARM, alarm_disable, 4u);
    if (st != PCF8563_OK) return st;

    /* Disable CLKOUT to save power */
    st = pcf8563_write_reg(PCF8563_REG_CLKOUT_CONTROL, 0x00u);
    if (st != PCF8563_OK) return st;

    /* Disable timer, keep TD = 11 for power saving */
    st = pcf8563_write_reg(PCF8563_REG_TIMER_CONTROL, 0x03u);
    if (st != PCF8563_OK) return st;

    /* Check VL */
    st = pcf8563_read_reg(PCF8563_REG_VL_SECONDS, &sec_reg);
    if (st != PCF8563_OK) return st;

    if ((sec_reg & PCF8563_BIT_VL) != 0u)
    {
        return PCF8563_TIME_INVALID;
    }

    return PCF8563_OK;
}

PCF8563_Status pcf8563_reset(void)
{
    return pcf8563_init();
}

PCF8563_Status pcf8563_getDateTime(PCF8563_DateTime *date_time)
{
    PCF8563_Status st;
    uint8_t regs[7];
    uint16_t year_base;

    if (date_time == NULL)
    {
        return PCF8563_INVALID_ARG;
    }

    st = pcf8563_read_time_block(regs);
    if (st != PCF8563_OK)
    {
        return st;
    }

    date_time->voltage_low = ((regs[0] & PCF8563_BIT_VL) != 0u);

    date_time->time.second = pcf8563_bcd_to_dec((uint8_t)(regs[0] & 0x7Fu));
    date_time->time.minute = pcf8563_bcd_to_dec((uint8_t)(regs[1] & 0x7Fu));
    date_time->time.hour   = pcf8563_bcd_to_dec((uint8_t)(regs[2] & 0x3Fu));

    date_time->date.day     = pcf8563_bcd_to_dec((uint8_t)(regs[3] & 0x3Fu));
    date_time->date.weekday = (uint8_t)(regs[4] & 0x07u);
    date_time->date.month   = pcf8563_bcd_to_dec((uint8_t)(regs[5] & 0x1Fu));

    date_time->century_bit = ((regs[5] & PCF8563_BIT_CENTURY) != 0u);
    year_base = date_time->century_bit
              ? (PCF8563_CENTURY_BASE + 100u)
              :  PCF8563_CENTURY_BASE;

    date_time->date.year = (uint16_t)(year_base + pcf8563_bcd_to_dec(regs[6]));

    return PCF8563_OK;
}

PCF8563_Status pcf8563_setDateTime(const PCF8563_DateTime *date_time)
{
    uint8_t regs[7];
    uint16_t year_offset;

    if ((date_time == NULL) ||
        !pcf8563_is_valid_time(&date_time->time) ||
        !pcf8563_is_valid_date(&date_time->date))
    {
        return PCF8563_INVALID_ARG;
    }

    year_offset = (uint16_t)(date_time->date.year - PCF8563_CENTURY_BASE);

    regs[0] = pcf8563_dec_to_bcd(date_time->time.second); /* also clears VL */
    regs[1] = pcf8563_dec_to_bcd(date_time->time.minute);
    regs[2] = pcf8563_dec_to_bcd(date_time->time.hour);
    regs[3] = pcf8563_dec_to_bcd(date_time->date.day);
    regs[4] = (uint8_t)(date_time->date.weekday & 0x07u);
    regs[5] = pcf8563_dec_to_bcd(date_time->date.month);
    regs[6] = pcf8563_dec_to_bcd((uint8_t)(year_offset % 100u));

    if (year_offset >= 100u)
    {
        regs[5] |= PCF8563_BIT_CENTURY;
    }

    return pcf8563_write_time_block(regs);
}

PCF8563_Status pcf8563_getTime(PCF8563_Time *time)
{
    PCF8563_DateTime dt;
    PCF8563_Status st;

    if (time == NULL)
    {
        return PCF8563_INVALID_ARG;
    }

    st = pcf8563_getDateTime(&dt);
    if (st != PCF8563_OK)
    {
        return st;
    }

    *time = dt.time;
    return PCF8563_OK;
}

PCF8563_Status pcf8563_setTime(const PCF8563_Time *time)
{
    PCF8563_DateTime dt;
    PCF8563_Status st;

    if (!pcf8563_is_valid_time(time))
    {
        return PCF8563_INVALID_ARG;
    }

    /*
     * PCF8563 should write seconds..years in one go.
     * So read full datetime, replace time, write full block back.
     */
    st = pcf8563_getDateTime(&dt);
    if ((st != PCF8563_OK) && (st != PCF8563_TIME_INVALID))
    {
        return st;
    }

    dt.time = *time;
    return pcf8563_setDateTime(&dt);
}

PCF8563_Status pcf8563_setAlarm(const PCF8563_Alarm *alarm)
{
    PCF8563_Status st;
    uint8_t regs[4];
    uint8_t cs2 = 0u;
    bool preserve_tf;
    bool tie;
    bool ti_tp;

    if (alarm == NULL)
    {
        return PCF8563_INVALID_ARG;
    }

    if (alarm->enable_minute && (alarm->minute > 59u)) return PCF8563_INVALID_ARG;
    if (alarm->enable_hour   && (alarm->hour   > 23u)) return PCF8563_INVALID_ARG;
    if (alarm->enable_day    && ((alarm->day < 1u) || (alarm->day > 31u))) return PCF8563_INVALID_ARG;
    if (alarm->enable_weekday && (alarm->weekday > 6u)) return PCF8563_INVALID_ARG;

    regs[0] = alarm->enable_minute ? pcf8563_dec_to_bcd(alarm->minute) : 0x80u;
    regs[1] = alarm->enable_hour   ? pcf8563_dec_to_bcd(alarm->hour)   : 0x80u;
    regs[2] = alarm->enable_day    ? pcf8563_dec_to_bcd(alarm->day)    : 0x80u;
    regs[3] = alarm->enable_weekday ? (uint8_t)(alarm->weekday & 0x07u) : 0x80u;

    st = pcf8563_write_regs(PCF8563_REG_MINUTE_ALARM, regs, 4u);
    if (st != PCF8563_OK)
    {
        return st;
    }

    st = pcf8563_read_cs2(&cs2);
    if (st != PCF8563_OK)
    {
        return st;
    }

    preserve_tf = true;
    tie   = ((cs2 & PCF8563_BIT_CS2_TIE)   != 0u);
    ti_tp = ((cs2 & PCF8563_BIT_CS2_TI_TP) != 0u);

    /* Clear AF, keep TF, program AIE */
    return pcf8563_write_cs2(false, preserve_tf, alarm->interrupt_enable, tie, ti_tp);
}

PCF8563_Status pcf8563_disableAlarm(void)
{
    PCF8563_Status st;
    uint8_t regs[4] = { 0x80u, 0x80u, 0x80u, 0x80u };
    uint8_t cs2 = 0u;
    bool tie;
    bool ti_tp;

    st = pcf8563_write_regs(PCF8563_REG_MINUTE_ALARM, regs, 4u);
    if (st != PCF8563_OK)
    {
        return st;
    }

    st = pcf8563_read_cs2(&cs2);
    if (st != PCF8563_OK)
    {
        return st;
    }

    tie   = ((cs2 & PCF8563_BIT_CS2_TIE)   != 0u);
    ti_tp = ((cs2 & PCF8563_BIT_CS2_TI_TP) != 0u);

    /* Disable AIE, clear AF, preserve TF */
    return pcf8563_write_cs2(false, true, false, tie, ti_tp);
}

PCF8563_Status pcf8563_clearAlarmFlag(void)
{
    PCF8563_Status st;
    uint8_t cs2 = 0u;
    bool aie;
    bool tie;
    bool ti_tp;

    st = pcf8563_read_cs2(&cs2);
    if (st != PCF8563_OK)
    {
        return st;
    }

    aie   = ((cs2 & PCF8563_BIT_CS2_AIE)   != 0u);
    tie   = ((cs2 & PCF8563_BIT_CS2_TIE)   != 0u);
    ti_tp = ((cs2 & PCF8563_BIT_CS2_TI_TP) != 0u);

    return pcf8563_write_cs2(false, true, aie, tie, ti_tp);
}

PCF8563_Status pcf8563_isAlarmTriggered(bool *triggered)
{
    PCF8563_Status st;
    uint8_t cs2 = 0u;

    if (triggered == NULL)
    {
        return PCF8563_INVALID_ARG;
    }

    st = pcf8563_read_cs2(&cs2);
    if (st != PCF8563_OK)
    {
        return st;
    }

    *triggered = ((cs2 & PCF8563_BIT_CS2_AF) != 0u);
    return PCF8563_OK;
}

PCF8563_Status pcf8563_setTimer(const PCF8563_TimerConfig *cfg)
{
    PCF8563_Status st;
    uint8_t cs2 = 0u;
    uint8_t tc = 0u;
    bool aie;

    if (cfg == NULL)
    {
        return PCF8563_INVALID_ARG;
    }

    if ((uint8_t)cfg->clock > 3u)
    {
        return PCF8563_INVALID_ARG;
    }

    /*
     * Program timer value first with timer disabled,
     * then enable timer_control.
     */
    st = pcf8563_write_reg(PCF8563_REG_TIMER_CONTROL, (uint8_t)cfg->clock);
    if (st != PCF8563_OK)
    {
        return st;
    }

    st = pcf8563_write_reg(PCF8563_REG_TIMER, cfg->value);
    if (st != PCF8563_OK)
    {
        return st;
    }

    tc = (uint8_t)((uint8_t)cfg->clock | PCF8563_BIT_TIMER_TE);
    st = pcf8563_write_reg(PCF8563_REG_TIMER_CONTROL, tc);
    if (st != PCF8563_OK)
    {
        return st;
    }

    st = pcf8563_read_cs2(&cs2);
    if (st != PCF8563_OK)
    {
        return st;
    }

    aie = ((cs2 & PCF8563_BIT_CS2_AIE) != 0u);

    /* Preserve AF, clear TF, program TIE/TI_TP */
    return pcf8563_write_cs2(true, false, aie,
                             cfg->interrupt_enable,
                             cfg->pulse_interrupt);
}

PCF8563_Status pcf8563_disableTimer(void)
{
    PCF8563_Status st;
    uint8_t cs2 = 0u;
    bool aie;
    bool preserve_af;

    /* TE=0, TD=11 for power saving */
    st = pcf8563_write_reg(PCF8563_REG_TIMER_CONTROL, 0x03u);
    if (st != PCF8563_OK)
    {
        return st;
    }

    st = pcf8563_read_cs2(&cs2);
    if (st != PCF8563_OK)
    {
        return st;
    }

    aie = ((cs2 & PCF8563_BIT_CS2_AIE) != 0u);
    preserve_af = true;

    /* Disable TIE, clear TF, preserve AF */
    return pcf8563_write_cs2(preserve_af, false, aie, false, false);
}

PCF8563_Status pcf8563_setClkoutEnabled(bool enable)
{
    PCF8563_Status st;
    uint8_t reg = 0u;

    st = pcf8563_read_reg(PCF8563_REG_CLKOUT_CONTROL, &reg);
    if (st != PCF8563_OK)
    {
        return st;
    }

    if (enable)
    {
        reg |= PCF8563_BIT_CLKOUT_FE;
    }
    else
    {
        reg &= (uint8_t)~PCF8563_BIT_CLKOUT_FE;
    }

    return pcf8563_write_reg(PCF8563_REG_CLKOUT_CONTROL, reg);
}

PCF8563_Status pcf8563_setClkoutFrequency(uint8_t fd)
{
    PCF8563_Status st;
    uint8_t reg = 0u;

    if (fd > 3u)
    {
        return PCF8563_INVALID_ARG;
    }

    st = pcf8563_read_reg(PCF8563_REG_CLKOUT_CONTROL, &reg);
    if (st != PCF8563_OK)
    {
        return st;
    }

    reg &= (uint8_t)~0x03u;
    reg |= (fd & 0x03u);

    return pcf8563_write_reg(PCF8563_REG_CLKOUT_CONTROL, reg);
}