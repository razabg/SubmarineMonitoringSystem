#include "buzzer.h"
#include "objectdetection.h"
#include <stdbool.h>

#define NOTE_COUNT 24
#define LITTLE_YONATAN_LENGTH (sizeof(little_yonatan) / sizeof(little_yonatan[0]))

/* The alarm's rising/falling siren: two notes a clear interval apart,
 * alternated indefinitely by Buzzer_DurationElapsed() until
 * Buzzer_Stop(). */
#define ALARM_NOTE_MS 300u
static const Note alarm_notes[2] = { NOTE_A1, NOTE_E2 };

/* Object Detection's sonar ping: one short, higher note, then
 * silence, on repeat -- a "ping ... ping ... ping" rhythm, distinct
 * from the alarm's continuous alternating siren both in pitch pattern
 * and in having a gap at all. */
#define SONAR_PING_MS 150u
#define SONAR_GAP_MS  850u
static const Note sonar_note = NOTE_C2;

typedef struct {
    uint16_t period;
    uint16_t pulse;
} NoteValues;

static const NoteValues note_table[NOTE_COUNT] = { //index 0 == note c etc...
    {3815, 1907}, {3609, 1804}, {3400, 1700}, {3214, 1607},
    {3029, 1514}, {2864, 1432}, {2701, 1350}, {2550, 1275},
    {2408, 1204}, {2271, 1135}, {2144, 1072}, {2023, 1011},
    {1911,  955}, {1804,  902}, {1702,  851}, {1606,  803},
    {1516,  758}, {1431,  715}, {1350,  675}, {1274,  637},
    {1202,  601}, {1135,  567}, {1071,  535}, {1011,  505}
};

struct Buzzer_Handle {
    TIM_HandleTypeDef *pwm_timer;
    uint32_t channel;
    uint32_t elapsed_us;   /* accumulated since the current note/gap started */
    uint32_t target_us;    /* current note/gap's requested duration */
    bool     alarm_active; /* true while the two-note siren loop is running */
    uint8_t  alarm_step;   /* index into alarm_notes[] of the current one */
    bool     sonar_active; /* true while the ping/gap loop is running */
    bool     sonar_in_gap; /* true = currently in the silent gap, not the ping */
};

static struct Buzzer_Handle g_buzzer;

/* Loads one note's frequency/duty into the timer and (re)starts both
 * the PWM output and the update interrupt that Buzzer_DurationElapsed()
 * uses to time it. */
static void buzzer_arm_note(Buzzer_Handle *h, Note note, uint32_t duration_ms)
{
    h->elapsed_us = 0;
    h->target_us = duration_ms * 1000u; //300ms

    __HAL_TIM_SET_COUNTER(h->pwm_timer, 0);
    __HAL_TIM_SET_AUTORELOAD(h->pwm_timer, note_table[note].period); //period
    __HAL_TIM_SET_COMPARE(h->pwm_timer, h->channel, note_table[note].pulse); //pulse duty cycle

    HAL_TIM_PWM_Start(h->pwm_timer, h->channel);
    HAL_TIM_Base_Start_IT(h->pwm_timer);
}

/* Times a silent gap the same way buzzer_arm_note() times a note --
 * duty cycle 0 keeps the pin silent, but the timer (and its interrupt)
 * keep running exactly as before, so Buzzer_DurationElapsed() still
 * gets called when the gap is over. ARR is left at whatever the last
 * note set it to; the elapsed-time math only cares about its value,
 * not whether the channel is actually audible. */
static void buzzer_arm_gap(Buzzer_Handle *h, uint32_t duration_ms)
{
    h->elapsed_us = 0;
    h->target_us = duration_ms * 1000u;
    __HAL_TIM_SET_COMPARE(h->pwm_timer, h->channel, 0);
}

Buzzer_Handle *Buzzer_Create(TIM_HandleTypeDef *pwm_timer, uint32_t channel)
{
    if (pwm_timer == NULL) {
        return NULL;
    }
    g_buzzer.pwm_timer = pwm_timer;
    g_buzzer.channel = channel;
    g_buzzer.elapsed_us = 0;
    g_buzzer.target_us = 0;
    g_buzzer.alarm_active = false;
    g_buzzer.alarm_step = 0;
    g_buzzer.sonar_active = false;
    g_buzzer.sonar_in_gap = false;
    return &g_buzzer;
}

void Buzzer_PlayNote(Buzzer_Handle *h, Note note, uint32_t duration_ms)
{
    if (h == NULL) {
        return;
    }
    h->alarm_active = false;
    h->sonar_active = false;
    buzzer_arm_note(h, note, duration_ms);
}

void Buzzer_Stop(Buzzer_Handle *h)
{
    if (h == NULL) {
        return;
    }
    h->alarm_active = false;
    h->sonar_active = false;
    HAL_TIM_PWM_Stop(h->pwm_timer, h->channel);
    HAL_TIM_Base_Stop_IT(h->pwm_timer);
}

void Buzzer_StartAlarm(Buzzer_Handle *h)
{
    if (h == NULL) {
        return;
    }
    h->alarm_active = true;
    h->sonar_active = false;
    h->alarm_step = 0;
    buzzer_arm_note(h, alarm_notes[0], ALARM_NOTE_MS);
}

void Buzzer_StartSonar(Buzzer_Handle *h)
{
    if (h == NULL) {
        return;
    }
    h->alarm_active = false;
    h->sonar_active = true;
    h->sonar_in_gap = false;
    buzzer_arm_note(h, sonar_note, SONAR_PING_MS);
}

void Buzzer_DurationElapsed(Buzzer_Handle *h)
{
    uint32_t period_us;

    if (h == NULL) {
        return;
    }

    /* One PWM cycle just completed -- its length in microseconds is the
     * note's ARR+1 ticks at TIM3's 1 MHz counter rate (Prescaler=79).
     * Accumulate until the current note's actual requested duration
     * has passed. */
    period_us = h->pwm_timer->Instance->ARR + 1u;//reads the live hardware register directly
    h->elapsed_us += period_us;
    if (h->elapsed_us < h->target_us) {
        return; /* not yet -- keep sounding, wait for the next cycle */
    }

    /* Finish line: the note/gap has run for its full duration. */
    if (h->alarm_active) {
        if (h->alarm_step == 0) {
            h->alarm_step = 1;
        } else {
            h->alarm_step = 0;
        }
        buzzer_arm_note(h, alarm_notes[h->alarm_step], ALARM_NOTE_MS);
    } else if (h->sonar_active) {
        if (h->sonar_in_gap) {
            h->sonar_in_gap = false;
            buzzer_arm_note(h, sonar_note, SONAR_PING_MS);
        } else {
            h->sonar_in_gap = true;
            buzzer_arm_gap(h, SONAR_GAP_MS);
        }
    } else {
        HAL_TIM_PWM_Stop(h->pwm_timer, h->channel);
        HAL_TIM_Base_Stop_IT(h->pwm_timer);
    }
}

/* TIM3's own update-elapsed interrupt (NVIC.TIM3_IRQn, already enabled
 * in the .ioc) drives note timing -- see Buzzer_DurationElapsed().
 *
 * TIM5's branch is Object Detection's 10 s presence timeout -- lives
 * here (not objectdetection.c) purely because only one strong
 * definition of this HAL callback is allowed in the whole program,
 * and buzzer.c already had it; it just delegates immediately. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3) {
        Buzzer_DurationElapsed(&g_buzzer);
    } else if (htim->Instance == TIM5) {
        objdet_on_timeout();
    }
}



typedef struct {
    Note note;
    uint32_t duration_ms;
} MelodyNote;

static const MelodyNote little_yonatan[] = {
    {NOTE_G1, 200}, {NOTE_E1, 200}, {NOTE_E1, 400},
    {NOTE_F1, 200}, {NOTE_D1, 200}, {NOTE_D1, 400},
    {NOTE_C1, 200}, {NOTE_D1, 200}, {NOTE_E1, 200}, {NOTE_F1, 200},
    {NOTE_G1, 200}, {NOTE_G1, 200}, {NOTE_G1, 400},
    {NOTE_G1, 200}, {NOTE_E1, 200}, {NOTE_E1, 400},
    {NOTE_F1, 200}, {NOTE_D1, 200}, {NOTE_D1, 400},
    {NOTE_C1, 200}, {NOTE_E1, 200}, {NOTE_G1, 200}, {NOTE_G1, 200}, {NOTE_C1, 800},

    {NOTE_D1, 200}, {NOTE_D1, 200}, {NOTE_D1, 200}, {NOTE_D1, 200},
    {NOTE_D1, 200}, {NOTE_E1, 200}, {NOTE_F1, 400},
    {NOTE_E1, 200}, {NOTE_E1, 200}, {NOTE_E1, 200}, {NOTE_E1, 200},
    {NOTE_E1, 200}, {NOTE_F1, 200}, {NOTE_G1, 400},
    {NOTE_E1, 200}, {NOTE_E1, 200}, {NOTE_E1, 400},
    {NOTE_F1, 200}, {NOTE_D1, 200}, {NOTE_D1, 400},
    {NOTE_C1, 200}, {NOTE_E1, 200}, {NOTE_G1, 200}, {NOTE_G1, 200}, {NOTE_C1, 800},
};



void Buzzer_PlayLittleYonatan(Buzzer_Handle *h)
{
    for (uint32_t i = 0; i < LITTLE_YONATAN_LENGTH; i++)
    {
        Buzzer_PlayNote(h, little_yonatan[i].note, little_yonatan[i].duration_ms);
        HAL_Delay(little_yonatan[i].duration_ms + 100);
    }
}






//
// Buzzer_Create(&htim3, TIM_CHANNEL_1) is called from event_create() --
// Event owns the buzzer, since it's the only consumer (the alarm).


//Implementation of UART interrupt

//uint8_t rx_byte;
//
//HAL_UART_Receive_IT(&huart2, &rx_byte, 1);   // start listening for 1 byte at a time
//
//void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
//{
//    if (huart->Instance == USART2)
//    {
//        switch (rx_byte)
//        {
//            case '1': Buzzer_PlayNote(buzzer, NOTE_C1, 500); break;
//            case '2': Buzzer_PlayNote(buzzer, NOTE_D1, 500); break;
//            case '3': Buzzer_PlayNote(buzzer, NOTE_E1, 500); break;
//            case '4': Buzzer_PlayNote(buzzer, NOTE_F1, 500); break;
//            case '5': Buzzer_PlayNote(buzzer, NOTE_G1, 500); break;
//            case '6': Buzzer_PlayNote(buzzer, NOTE_A1, 500); break;
//            case '7': Buzzer_PlayNote(buzzer, NOTE_B1, 500); break;
//            case '8': Buzzer_PlayNote(buzzer, NOTE_C2, 500); break;
//            case '9': Buzzer_PlayNote(buzzer, NOTE_D2, 500); break;
//            default: break;
//        }
//
//        HAL_UART_Receive_IT(&huart2, &rx_byte, 1);  // re-arm for the NEXT byte
//    }
//}
