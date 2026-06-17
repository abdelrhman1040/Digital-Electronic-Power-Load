#ifndef __ENCODER_H
#define __ENCODER_H

#include "main.h"

/* Encoder Data Structure */
typedef struct {
    TIM_HandleTypeDef* htim_enc;      // Encoder timer (e.g., TIM2)
    TIM_HandleTypeDef* htim_timebase; // Time base/Speed timer (e.g., TIM3)
    int16_t rotation;                 // Number of rotations or steps
    uint32_t speed;                   // Raw value representing speed
    uint8_t is_changed;               // Flag to indicate if a change occurred
} RotaryEncoder_t;

/* Initialization and Control Functions */
void Encoder_Init(RotaryEncoder_t *encoder, TIM_HandleTypeDef *htim_enc, TIM_HandleTypeDef *htim_timebase);
void Encoder_Reset(RotaryEncoder_t *encoder);

/* Interrupt Service Routine (ISR) Handlers */
void Encoder_TIM_ISR(RotaryEncoder_t *encoder, TIM_HandleTypeDef *htim);
void Encoder_EXTI_ISR(RotaryEncoder_t *encoder, uint16_t GPIO_Pin);

#endif /* __ENCODER_H */
