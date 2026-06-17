#include "ads1115.h"

/**
 * @brief Starts the ADC conversion without waiting (Non-Blocking).
 */
void ADS1115_StartConversion(I2C_HandleTypeDef *hi2c, uint8_t channel)
{
    uint8_t config_data[3];
    if(channel > 3) channel = 0;

    /* * OS[15] = 1 (Start)
     * MUX[14:12] = (4 + channel) -> Single-Ended Mode (AINx to GND)
     * PGA[11:9] = 000 (+/- 6.144V)
     * MODE[8] = 1 (Single-shot)
     * DR[7:5] = 111 (860 SPS) -> 0x00E0
     * COMP[4:0] = 0x0003 (Disable comparator)
     */
    uint16_t config_value = 0x81E3 | ((4 + channel) << 12);

    config_data[0] = 0x01; /* Point to Config Register */
    config_data[1] = (config_value >> 8) & 0xFF; /* MSB */
    config_data[2] = config_value & 0xFF;        /* LSB */

    HAL_I2C_Master_Transmit(hi2c, ADS1115_ADDRESS, config_data, 3, 10);
}

/**
 * @brief Reads the finished conversion from the ADS1115.
 */
float ADS1115_ReadConversion(I2C_HandleTypeDef *hi2c)
{
    uint8_t reg_pointer = 0x00; /* Conversion Register */
    uint8_t read_data[2] = {0, 0}; // Initialize with zeros

    HAL_I2C_Master_Transmit(hi2c, ADS1115_ADDRESS, &reg_pointer, 1, 10);

    if (HAL_I2C_Master_Receive(hi2c, ADS1115_ADDRESS, read_data, 2, 10) != HAL_OK) {
        return 0.0f;
    }

    int16_t raw_adc = (read_data[0] << 8) | read_data[1];
    float voltage = ((int16_t)raw_adc * 6.144f) / 32768.0f;

    return (voltage < 0.0f) ? 0.0f : voltage;
}
