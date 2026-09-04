#ifndef BUZZER_H
#define BUZZER_H

#include "main.h"
#include <stdint.h>

typedef struct Buzzer_Handle Buzzer_Handle;

typedef enum {
    NOTE_C1, NOTE_CS1, NOTE_D1, NOTE_DS1, NOTE_E1, NOTE_F1,
    NOTE_FS1, NOTE_G1, NOTE_GS1, NOTE_A1, NOTE_AS1, NOTE_B1,
    NOTE_C2, NOTE_CS2, NOTE_D2, NOTE_DS2, NOTE_E2, NOTE_F2,
    NOTE_FS2, NOTE_G2, NOTE_GS2, NOTE_A2, NOTE_AS2, NOTE_B2
} Note;

/* One timer does both jobs: `pwm_timer`/`channel` (TIM3_CH1) generates
 * the audible tone, and that same timer's own update-elapsed interrupt
 * (already enabled in the .ioc -- NVIC.TIM3_IRQn) times how long each
 * note plays. No separate duration timer needed. */
Buzzer_Handle *Buzzer_Create(TIM_HandleTypeDef *pwm_timer, uint32_t channel);

/* Plays one note for duration_ms, then stops. Cancels the alarm loop
 * (below) if it was running. */
void Buzzer_PlayNote(Buzzer_Handle *h, Note note, uint32_t duration_ms);

/* Stops whatever is currently sounding -- a single note or the alarm
 * loop. */
void Buzzer_Stop(Buzzer_Handle *h);

/* Starts an indefinite rising/falling two-note siren loop; keeps going
 * until Buzzer_Stop(). */
void Buzzer_StartAlarm(Buzzer_Handle *h);

/* Starts an indefinite "ping ... ping ... ping" loop (Object
 * Detection's sonar, distinct from the alarm siren above); keeps
 * going until Buzzer_Stop(). Only one pattern can sound at a time --
 * starting this cancels the alarm if it was running, and vice versa. */
void Buzzer_StartSonar(Buzzer_Handle *h);

/* Called from TIM3's HAL_TIM_PeriodElapsedCallback -- once per PWM
 * cycle (i.e. far more often than once per note), since that's the
 * only update event this shared timer produces. Internally accumulates
 * elapsed time and only acts once the current note's requested
 * duration has actually passed. */
void Buzzer_DurationElapsed(Buzzer_Handle *h);

void Buzzer_PlayLittleYonatan(Buzzer_Handle *h);
void Buzzer_PlayWellerman(Buzzer_Handle *h);

#endif
