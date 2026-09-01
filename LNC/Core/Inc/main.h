/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
extern UART_HandleTypeDef huart2;
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define I2C3_SCL_RTC_Pin GPIO_PIN_0
#define I2C3_SCL_RTC_GPIO_Port GPIOC
#define I2C3_SDA_RTC_Pin GPIO_PIN_1
#define I2C3_SDA_RTC_GPIO_Port GPIOC
#define P_METER_BATTERY_Pin GPIO_PIN_0
#define P_METER_BATTERY_GPIO_Port GPIOA
#define LIGHT_SENSOR_Pin GPIO_PIN_1
#define LIGHT_SENSOR_GPIO_Port GPIOA
#define USART_TX_Pin GPIO_PIN_2
#define USART_TX_GPIO_Port GPIOA
#define USART_RX_Pin GPIO_PIN_3
#define USART_RX_GPIO_Port GPIOA
#define SPI1_SCK_SD_Pin GPIO_PIN_5
#define SPI1_SCK_SD_GPIO_Port GPIOA
#define SPI1_MISO_SD_Pin GPIO_PIN_6
#define SPI1_MISO_SD_GPIO_Port GPIOA
#define SPI_MOSI_SD_Pin GPIO_PIN_7
#define SPI_MOSI_SD_GPIO_Port GPIOA
#define IR_Sensor_Pin GPIO_PIN_10
#define IR_Sensor_GPIO_Port GPIOB
#define RGB_RED_Pin GPIO_PIN_13
#define RGB_RED_GPIO_Port GPIOB
#define RGB_BLUE_Pin GPIO_PIN_14
#define RGB_BLUE_GPIO_Port GPIOB
#define RGB_GREEN_Pin GPIO_PIN_15
#define RGB_GREEN_GPIO_Port GPIOB
#define BUTTON_D2_Pin GPIO_PIN_10
#define BUTTON_D2_GPIO_Port GPIOA
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define BUTTON_D3_Pin GPIO_PIN_3
#define BUTTON_D3_GPIO_Port GPIOB
#define BUZZER_Pin GPIO_PIN_4
#define BUZZER_GPIO_Port GPIOB
#define DHT_Pin GPIO_PIN_5
#define DHT_GPIO_Port GPIOB
#define SD_CS_Pin GPIO_PIN_6
#define SD_CS_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
