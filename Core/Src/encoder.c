#include "encoder.h"

/**
 * @brief  Initializes the encoder and starts the associated timers.
 * @param  encoder: Pointer to the RotaryEncoder_t structure.
 * @param  htim_enc: Pointer to the encoder timer handle.
 * @param  htim_timebase: Pointer to the time base timer handle.
 */
void Encoder_Init(RotaryEncoder_t *encoder, TIM_HandleTypeDef *htim_enc, TIM_HandleTypeDef *htim_timebase) {
    encoder->htim_enc = htim_enc;
    encoder->htim_timebase = htim_timebase;
    encoder->rotation = 0;
    encoder->speed = 0;
    encoder->is_changed = 0;

    // Start the timers
    HAL_TIM_Base_Start(encoder->htim_timebase);
    HAL_TIM_Encoder_Start_IT(encoder->htim_enc, TIM_CHANNEL_ALL);
}

/**
 * @brief  Resets the encoder counts and variables.
 * @param  encoder: Pointer to the RotaryEncoder_t structure.
 */
void Encoder_Reset(RotaryEncoder_t *encoder) {
    encoder->htim_enc->Instance->CNT = 0;
    encoder->htim_timebase->Instance->CNT = 0;
    encoder->rotation = 0;
    encoder->speed = 0;
    encoder->is_changed = 1;
}

/**
 * @brief  Handles the Timer Input Capture Interrupt.
 *         MUST be called inside HAL_TIM_IC_CaptureCallback().
 * @param  encoder: Pointer to the RotaryEncoder_t structure.
 * @param  htim: Timer handle that triggered the interrupt.
 */
void Encoder_TIM_ISR(RotaryEncoder_t *encoder, TIM_HandleTypeDef *htim) {
    if (htim->Instance == encoder->htim_enc->Instance) {
        encoder->rotation = encoder->htim_enc->Instance->CNT;
        encoder->speed = encoder->htim_timebase->Instance->CNT;
        encoder->htim_timebase->Instance->CNT = 0;
        encoder->is_changed = 1;
    }
}

/**
 * @brief  Handles the External Interrupt (Button Press).
 *         MUST be called inside HAL_GPIO_EXTI_Callback().
 * @param  encoder: Pointer to the RotaryEncoder_t structure.
 * @param  GPIO_Pin: Specifies the pins connected to the EXTI line.
 */
void Encoder_EXTI_ISR(RotaryEncoder_t *encoder, uint16_t GPIO_Pin) {
    // Disabled — PB0 is used for menu button
    (void)encoder;
    (void)GPIO_Pin;
        }
