#ifndef PCF8563_H
#define PCF8563_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCF8563_I2C_ADDR_7BIT   0x51u
#define PCF8563_CENTURY_BASE   2000u

typedef enum
{
    PCF8563_OK = 0,
    PCF8563_ERROR,
    PCF8563_INVALID_ARG,
    PCF8563_I2C_ERROR,
    PCF8563_TIME_INVALID
} PCF8563_Status;

typedef struct
{
    uint8_t second;   /* 0..59 */
    uint8_t minute;   /* 0..59 */
    uint8_t hour;     /* 0..23 */
} PCF8563_Time;

typedef struct
{
    uint8_t day;      /* 1..31 */
    uint8_t weekday;  /* 0..6 */
    uint8_t month;    /* 1..12 */
    uint16_t year;    /* default convention: 2000..2199 */
} PCF8563_Date;

typedef struct
{
    PCF8563_Time time;
    PCF8563_Date date;
    bool voltage_low; /* mirrors VL bit from register 02h */
    bool century_bit; /* raw C bit from register 07h */
} PCF8563_DateTime;

typedef struct
{
    bool enable_minute;
    bool enable_hour;
    bool enable_day;
    bool enable_weekday;

    uint8_t minute;   /* 0..59 */
    uint8_t hour;     /* 0..23 */
    uint8_t day;      /* 1..31 */
    uint8_t weekday;  /* 0..6 */

    bool interrupt_enable;
} PCF8563_Alarm;

typedef enum
{
    PCF8563_TIMER_CLK_4096HZ = 0,
    PCF8563_TIMER_CLK_64HZ   = 1,
    PCF8563_TIMER_CLK_1HZ    = 2,
    PCF8563_TIMER_CLK_1_60HZ = 3
} PCF8563_TimerClock;

typedef struct
{
    uint8_t value; /* raw countdown register value */
    PCF8563_TimerClock clock;
    bool interrupt_enable;
    bool pulse_interrupt; /* false = level, true = pulse */
} PCF8563_TimerConfig;

/* Core APIs */
PCF8563_Status pcf8563_init(void);
PCF8563_Status pcf8563_reset(void);

PCF8563_Status pcf8563_getTime(PCF8563_Time *time);
PCF8563_Status pcf8563_setTime(const PCF8563_Time *time);

PCF8563_Status pcf8563_getDateTime(PCF8563_DateTime *date_time);
PCF8563_Status pcf8563_setDateTime(const PCF8563_DateTime *date_time);

/* Alarm APIs */
PCF8563_Status pcf8563_setAlarm(const PCF8563_Alarm *alarm);
PCF8563_Status pcf8563_disableAlarm(void);
PCF8563_Status pcf8563_clearAlarmFlag(void);
PCF8563_Status pcf8563_isAlarmTriggered(bool *triggered);

/* Timer APIs */
PCF8563_Status pcf8563_setTimer(const PCF8563_TimerConfig *cfg);
PCF8563_Status pcf8563_disableTimer(void);

/* Optional helpers */
PCF8563_Status pcf8563_setClkoutEnabled(bool enable);
PCF8563_Status pcf8563_setClkoutFrequency(uint8_t fd);

#ifdef __cplusplus
}
#endif

#endif