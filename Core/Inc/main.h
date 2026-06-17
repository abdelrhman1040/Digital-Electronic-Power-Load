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
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

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
#define ENCA_Pin GPIO_PIN_0
#define ENCA_GPIO_Port GPIOA
#define ENCB_Pin GPIO_PIN_1
#define ENCB_GPIO_Port GPIOA
#define Temp1_Pin GPIO_PIN_2
#define Temp1_GPIO_Port GPIOA
#define Temp2_Pin GPIO_PIN_3
#define Temp2_GPIO_Port GPIOA
#define R_BTN_Pin GPIO_PIN_0
#define R_BTN_GPIO_Port GPIOB
#define R_BTN_EXTI_IRQn EXTI0_IRQn
#define ALRT_Pin GPIO_PIN_1
#define ALRT_GPIO_Port GPIOB
#define ESP_RESET_Pin GPIO_PIN_12
#define ESP_RESET_GPIO_Port GPIOB
#define BTN2_Pin GPIO_PIN_14
#define BTN2_GPIO_Port GPIOB
#define BTN2_EXTI_IRQn EXTI15_10_IRQn
#define BTN1_Pin GPIO_PIN_15
#define BTN1_GPIO_Port GPIOB
#define BTN1_EXTI_IRQn EXTI15_10_IRQn
#define ESP8266_RX_Pin GPIO_PIN_9
#define ESP8266_RX_GPIO_Port GPIOA
#define ESP8266_TX_Pin GPIO_PIN_10
#define ESP8266_TX_GPIO_Port GPIOA
#define FAN_PWM_Pin GPIO_PIN_8
#define FAN_PWM_GPIO_Port GPIOB
#define BUZZER_Pin GPIO_PIN_9
#define BUZZER_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
