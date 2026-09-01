#include "rtc_ds1307_I2C.h"

/* ---- BCD helpers ------------------------------------------------------ */

static uint8_t bin2bcd(uint8_t val)
{
    return (uint8_t)(((val / 10) << 4) | (val % 10));
}

static uint8_t bcd2bin(uint8_t val)
{
    return (uint8_t)(((val >> 4) * 10) + (val & 0x0F));
}

/* ---- Public API --------------------------------------------------------*/

HAL_StatusTypeDef RTC_SetTime(I2C_HandleTypeDef *hi2c, RTC_Time_t *t)
{
    uint8_t buf[9];

    buf[0] = 0x00;                 /* starting register address */
    buf[1] = bin2bcd(t->sec);      /* bit7 (CH) stays 0 -> oscillator runs */
    buf[2] = bin2bcd(t->min);
    buf[3] = bin2bcd(t->hour);     /* bit6 stays 0 -> 24h mode */
    buf[4] = bin2bcd(t->dow);
    buf[5] = bin2bcd(t->date);
    buf[6] = bin2bcd(t->month);
    buf[7] = bin2bcd(t->year);
    buf[8] = 0x00;                 /* control register: SQW output disabled */

    return HAL_I2C_Master_Transmit(hi2c, DS1307_I2C_ADDR, buf, 9, HAL_MAX_DELAY);
}

HAL_StatusTypeDef RTC_GetTime(I2C_HandleTypeDef *hi2c, RTC_Time_t *t)
{
    HAL_StatusTypeDef status;
    uint8_t reg = 0x00;
    uint8_t rx[9];

    /* point the RTC's internal register pointer at address 0 */
    status = HAL_I2C_Master_Transmit(hi2c, DS1307_I2C_ADDR, &reg, 1, HAL_MAX_DELAY);
    if (status != HAL_OK)
    {
        return status;
    }

    /* read 9 bytes back (7 time/date bytes + control reg + 1 spare) */
    status = HAL_I2C_Master_Receive(hi2c, DS1307_I2C_ADDR, rx, 9, HAL_MAX_DELAY);
    if (status != HAL_OK)
    {
        return status;
    }

    t->sec   = bcd2bin(rx[0] & 0x7F);   /* mask off CH bit */
    t->min   = bcd2bin(rx[1]);
    t->hour  = bcd2bin(rx[2] & 0x3F);   /* mask off 12/24h mode bit */
    t->dow   = bcd2bin(rx[3]);
    t->date  = bcd2bin(rx[4]);
    t->month = bcd2bin(rx[5]);
    t->year  = bcd2bin(rx[6]);
    /* rx[7] = control register, rx[8] = spare byte -> not used */

    return HAL_OK;
}



//what to write in main
//#if SET_RTC_TIME
//  RTC_Time_t setTime = {.sec=0, .min=21, .hour=17, .dow=3, .date=8, .month=7, .year=26};
//  RTC_SetTime(&hi2c3, &setTime);
//#endif
//
//  /* USER CODE END 2 */
//
//  /* Infinite loop */
//  /* USER CODE BEGIN WHILE */
//  while (1)
//  {
//    /* USER CODE END WHILE */
//
//    /* USER CODE BEGIN 3 */
//    // --- Your Application Code Lives Here ---
//	  RTC_Time_t now;
//	  if (RTC_GetTime(&hi2c3, &now) == HAL_OK)
//	  {
//	      printf("Time: %02u:%02u:%02u  Date: %02u/%02u/20%02u  DOW:%u\r\n",
//	             now.hour, now.min, now.sec, now.date, now.month, now.year, now.dow);
//	  }
//	  HAL_Delay(1000);
