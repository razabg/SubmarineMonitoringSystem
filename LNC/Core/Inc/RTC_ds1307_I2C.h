#ifndef RTC_DS1307_H
#define RTC_DS1307_H

#include "stm32l4xx_hal.h"   /* adjust if your project uses a different HAL header */
#include <stdint.h>

/* 8-bit I2C address (already shifted, includes R/W bit slot) */
#define DS1307_I2C_ADDR   0xD0 // this is address of the external real time clock

typedef struct {
    uint8_t sec;    /* 0-59 */
    uint8_t min;    /* 0-59 */
    uint8_t hour;   /* 0-23 (24h mode) */
    uint8_t dow;    /* 1-7  (day of week) */
    uint8_t date;   /* 1-31 (day of month) */
    uint8_t month;  /* 1-12 */
    uint8_t year;   /* 0-99 (offset from 2000) */
} RTC_Time_t;

/**
 * Writes sec/min/hour/dow/date/month/year to the RTC starting at register 0,
 * and clears the control register. Call this ONCE to set the clock.
 *
 * Returns HAL_OK on success, or the HAL error code on failure.
 */
HAL_StatusTypeDef RTC_SetTime(I2C_HandleTypeDef *hi2c, RTC_Time_t *t);

/**
 * Reads the current time/date from the RTC into *t.
 * Returns HAL_OK on success, or the HAL error code on failure.
 */
HAL_StatusTypeDef RTC_GetTime(I2C_HandleTypeDef *hi2c, RTC_Time_t *t);

#endif /* RTC_DS1307_H */
