#ifndef ADS1115_H
#define ADS1115_H

#include "stm32f1xx_hal.h"

#define ADS1115_ADDRESS 0x90

#define ADS1115_CH_VOLT    0
#define ADS1115_CH_VOLT2   1
#define ADS1115_CH_CURRENT 2
#define ADS1115_CH_ANALOG  3

void ADS1115_StartConversion(I2C_HandleTypeDef *hi2c, uint8_t channel);
float ADS1115_ReadConversion(I2C_HandleTypeDef *hi2c);

#endif /* ADS1115_H */
