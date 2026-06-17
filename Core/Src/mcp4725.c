#include "mcp4725.h"

/**
 * @brief  Initializes the MCP4725 DAC.
 * (Dummy function for now, but good for future expansion
 * like checking device readiness).
 * @param  hi2c: Pointer to a I2C_HandleTypeDef structure
 */
void MCP4725_Init(I2C_HandleTypeDef *hi2c)
{
    /* Check if the DAC is ready on the I2C bus */
    HAL_I2C_IsDeviceReady(hi2c, MCP4725_I2C_ADDR, 3, HAL_MAX_DELAY);
}

/**
 * @brief  Sets the DAC output value using Fast Write Mode.
 * Updates the output immediately. Best for real-time control.
 * @param  hi2c: Pointer to a I2C_HandleTypeDef structure
 * @param  dac_value: 12-bit value (0 to 4095)
 * @retval HAL status
 */
HAL_StatusTypeDef MCP4725_SetVoltage(I2C_HandleTypeDef *hi2c, uint16_t dac_value)
{
    uint8_t buffer[2];

    /* Limit the value to 12 bits (Max 4095) */
    if (dac_value > 4095) {
        dac_value = 4095;
    }

    /* Fast Write Mode configuration */
    /* Byte 1: 0000 (Fast Mode) | PD1,PD0 (00) | D11, D10, D9, D8 */
    buffer[0] = (uint8_t)((dac_value >> 8) & 0x0F);

    /* Byte 2: D7 to D0 */
    buffer[1] = (uint8_t)(dac_value & 0xFF);

    /* Transmit via I2C */
    return HAL_I2C_Master_Transmit(hi2c, MCP4725_I2C_ADDR, buffer, 2, HAL_MAX_DELAY);
}

/**
 * @brief  Sets the DAC output value AND saves it to EEPROM.
 * The DAC will load this value automatically upon next power-up.
 * Takes longer to execute (EEPROM write time), use sparingly.
 * @param  hi2c: Pointer to a I2C_HandleTypeDef structure
 * @param  dac_value: 12-bit value (0 to 4095)
 * @retval HAL status
 */
HAL_StatusTypeDef MCP4725_SetVoltageEEPROM(I2C_HandleTypeDef *hi2c, uint16_t dac_value)
{
    uint8_t buffer[3];

    if (dac_value > 4095) {
        dac_value = 4095;
    }

    /* Normal Write Mode to DAC Register and EEPROM */
    buffer[0] = 0x60;                                /* Write Command: DAC & EEPROM */
    buffer[1] = (uint8_t)(dac_value >> 4);           /* Upper 8 bits: D11 to D4 */
    buffer[2] = (uint8_t)((dac_value & 0x0F) << 4);  /* Lower 4 bits: D3 to D0 (shifted left) */

    return HAL_I2C_Master_Transmit(hi2c, MCP4725_I2C_ADDR, buffer, 3, HAL_MAX_DELAY);
}
