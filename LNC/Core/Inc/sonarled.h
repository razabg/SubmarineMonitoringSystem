/*
 * sonarled.h - LNC breathing red LED (Object Detection's sonar
 * indicator, distinct from Event's RGB status LED)
 *
 * `TIM8_CH4` on `PC9` (`RED_LED_SONAR_Pin`) generates the PWM; the
 * breathing fade is driven by a small dedicated task using
 * `osDelayUntil()`, not by TIM8's own interrupt -- TIM8 runs at 10kHz
 * (Prescaler=7, Period=999), far faster than a slow multi-second
 * visual fade needs, and this is a non-critical cosmetic effect with
 * no real-time requirement, matching the same "don't reach for
 * hardware-timer precision this doesn't need" reasoning already used
 * for Monitor/Keep-Alive/Watchdog (CLAUDE.md section 9).
 *
 * ADT, matching buzzer.c's/monitor.c's shape: opaque handle, one
 * static instance, create() (which also starts the task).
 */
#ifndef SONARLED_H
#define SONARLED_H

#include "main.h"

typedef struct SonarLed SonarLed;

/* Starts the dedicated breathing task (idle until SonarLed_Start()).
 * Returns the handle, or NULL if pwm_timer is NULL or the task failed
 * to create. */
SonarLed *SonarLed_Create(TIM_HandleTypeDef *pwm_timer, uint32_t channel);

/* Starts the fade in/out loop from off; keeps going until
 * SonarLed_Stop(). */
void SonarLed_Start(SonarLed *h);

/* Stops and turns the LED off. */
void SonarLed_Stop(SonarLed *h);

#endif /* SONARLED_H */
