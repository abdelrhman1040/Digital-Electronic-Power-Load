#ifndef MCP4725_H
#define MCP4725_H

#include "stm32f1xx_hal.h"

/* * MCP4725 Default I2C Address:
 * - If A0 pin is connected to GND: 0xC0 (Most common for modules)
 * - If A0 pin is connected to VCC: 0xC2
 */
#define MCP4725_I2C_ADDR 0xC0

/* Function Prototypes */
void MCP4725_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef MCP4725_SetVoltage(I2C_HandleTypeDef *hi2c, uint16_t dac_value);
HAL_StatusTypeDef MCP4725_SetVoltageEEPROM(I2C_HandleTypeDef *hi2c, uint16_t dac_value);

#endif /* MCP4725_H */
