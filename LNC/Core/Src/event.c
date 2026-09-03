/*
 * event.c - LNC Event module
 * See event.h for the public API and the class-level overview.
 *
 * LED color is a pure function of the *new* mode (Warning always yellow,
 * Error always red, Normal always green, per section 2.3's table -- both
 * rows that reach a given mode agree on its color), so
 * event_mode_changed() only needs one switch on new_mode for LED, then a
 * separate check on which mode edge it is for alarm/essential-only.
 */
#include "event.h"
#include "main.h"
#include "tlv.h"
#include "sdfatfs.h"
#include "buzzer.h"
#include <stdio.h>

#define EVENTS_FILENAME "EVENTS.TXT"

struct Event {
    Communication *comm;
    Buzzer_Handle *buzzer;
    bool alarm_active;
    bool essential_only;
};

static struct Event g_event;

/* ===============================================================
 * Public API: create / destroy
 * =============================================================== */

Event *event_create(Communication *comm)
{
    g_event.comm = comm;
    g_event.buzzer = Buzzer_Create(&htim3, TIM_CHANNEL_1);
    if (g_event.buzzer == NULL) {
        return NULL;
    }
    g_event.alarm_active = false;
    g_event.essential_only = false;
    return &g_event;
}

void event_destroy(Event *e)
{
    if (e == NULL) {
        return;
    }
    if (e->alarm_active) {
        Buzzer_Stop(e->buzzer);
    }
}

/* ===============================================================
 * LED / alarm helpers
 * =============================================================== */

static void led_off(void)
{
    HAL_GPIO_WritePin(RGB_RED_GPIO_Port, RGB_RED_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RGB_GREEN_GPIO_Port, RGB_GREEN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RGB_BLUE_GPIO_Port, RGB_BLUE_Pin, GPIO_PIN_RESET);
}

static void led_red(void)
{
    led_off();
    HAL_GPIO_WritePin(RGB_RED_GPIO_Port, RGB_RED_Pin, GPIO_PIN_SET);
}

static void led_yellow(void)
{
    led_off();
    HAL_GPIO_WritePin(RGB_RED_GPIO_Port, RGB_RED_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(RGB_GREEN_GPIO_Port, RGB_GREEN_Pin, GPIO_PIN_SET);
}

static void led_green(void)
{
    led_off();
    HAL_GPIO_WritePin(RGB_GREEN_GPIO_Port, RGB_GREEN_Pin, GPIO_PIN_SET);
}

static void led_set_for_mode(monitor_mode_t mode)
{
    switch (mode) {
    case MODE_NORMAL:  led_green();  break;
    case MODE_WARNING: led_yellow(); break;
    case MODE_ERROR:   led_red();    break;
    }
}

static void alarm_start(void)
{
    if (!g_event.alarm_active) {
        Buzzer_StartAlarm(g_event.buzzer);
        g_event.alarm_active = true;
    }
}

static void alarm_stop_if_active(void)
{
    if (g_event.alarm_active) {
        Buzzer_Stop(g_event.buzzer);
        g_event.alarm_active = false;
    }
}

/* ===============================================================
 * Timestamp / events file
 * =============================================================== */

/* HAL quirk: on this family, reading the RTC's shadow time/date registers
 * only latches correctly if GetTime is immediately followed by GetDate
 * (even when only the time is needed), so both are always read together
 * here. RTC_FORMAT_BIN gets plain decimal fields directly, no BCD
 * conversion needed. */
static void format_timestamp(char *out, size_t out_len)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;

    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);

    snprintf(out, out_len, "20%02u-%02u-%02u %02u:%02u:%02u",
             d.Year, d.Month, d.Date, t.Hours, t.Minutes, t.Seconds);
}

/* Appends one timestamped line to the events file on the SD card. */
static void write_events_file(const char *message)
{
    char timestamp[24];
    char line[128];

    format_timestamp(timestamp, sizeof(timestamp));
    snprintf(line, sizeof(line), "[%s] %s\r\n", timestamp, message);
    (void)SDFatFS_SaveString(EVENTS_FILENAME, line);
}

/* ===============================================================
 * Dispatch: Monitor -> Event
 * =============================================================== */

/* Provisional wire format for TLV_TAG_MODE_CHANGE -- nothing on the
 * Central Computer side parses this tag yet, so this is a placeholder
 * the Management Command module will need to match once it's built. */
typedef struct __attribute__((packed)) {
    uint8_t old_mode;
    uint8_t new_mode;
    int16_t temp_c;
    uint8_t humidity_pct;
    uint8_t light_pct;
    uint8_t battery_pct;
} mode_change_payload_t;

void event_mode_changed(const monitor_measurement_t *data,
                         monitor_mode_t old_mode, monitor_mode_t new_mode)
{
    char line[96];
    mode_change_payload_t payload;

    led_set_for_mode(new_mode);

    if (new_mode == MODE_ERROR) {
        alarm_start();
        g_event.essential_only = true;
    } else if (old_mode == MODE_ERROR) {
        /* Error -> Warning or Error -> Normal: stop if active, resume
         * full operation. Normal -> Warning and Warning -> Normal need
         * neither (alarm never started outside of Error). */
        alarm_stop_if_active();
        g_event.essential_only = false;
    }

    snprintf(line, sizeof(line),
             "mode %s -> %s (temp=%dC hum=%u%% light=%u%% batt=%u%%)",
             monitor_mode_name(old_mode), monitor_mode_name(new_mode),
             data->temp_c, data->humidity_pct, data->light_pct, data->battery_pct);
    write_events_file(line);

    payload.old_mode = (uint8_t)old_mode;
    payload.new_mode = (uint8_t)new_mode;
    payload.temp_c = data->temp_c;
    payload.humidity_pct = data->humidity_pct;
    payload.light_pct = data->light_pct;
    payload.battery_pct = data->battery_pct;
    (void)comm_send(g_event.comm, TLV_TAG_MODE_CHANGE,
                     (const uint8_t *)&payload, sizeof(payload));
}

/* ===============================================================
 * Dispatch: Object Detection -> Event (module not built yet)
 * thnk of use the seperate blue and red led to that
 * =============================================================== */

void event_object_detected(void)
{
    led_red();
    alarm_start();
    write_events_file("object detected");
    (void)comm_send(g_event.comm, TLV_TAG_OBJECT_DETECTED, NULL, 0);
}

void event_object_cleared(void)
{
    led_green();
    alarm_stop_if_active();
    write_events_file("object cleared");
    (void)comm_send(g_event.comm, TLV_TAG_OBJECT_CLEARED, NULL, 0);
}

/* ===============================================================
 * Dispatch: Configuration / Init -> Event (modules not built yet)
 * =============================================================== */

void event_config_changed(const char *description)
{
    char line[96];

    snprintf(line, sizeof(line), "config changed: %s", description ? description : "");
    write_events_file(line);
    /* No message to the Central Computer, per section 2.3. */
}

void event_startup(bool was_watchdog_reset)
{
    char line[64];

    snprintf(line, sizeof(line), "startup (watchdog reset: %s)",
             was_watchdog_reset ? "yes" : "no");
    write_events_file(line);
}

/* ===============================================================
 * Button -> Event
 * =============================================================== */

void event_button_pressed(void)
{
    alarm_stop_if_active();
}

/* The NVIC vector (EXTI3_IRQHandler) is CubeMX-generated in
 * stm32l4xx_it.c and regenerates whenever the .ioc changes; it just
 * calls HAL_GPIO_EXTI_IRQHandler(), which dispatches to the weak
 * callback below -- only the callback belongs here. */

/* EXTI fires a burst of edges per physical press, so only a falling
 * edge more than BUTTON_DEBOUNCE_MS after the last accepted one counts
 * as a real press. Kept to a tick comparison (no software timer object)
 * per the project's "ISRs do the minimum" rule. */
#define BUTTON_DEBOUNCE_MS 50

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    static uint32_t last_press_tick = 0;
    uint32_t now;

    if (gpio_pin != BUTTON_D3_Pin) {
        return;
    }

    now = HAL_GetTick();
    if (now - last_press_tick > BUTTON_DEBOUNCE_MS) {
        last_press_tick = now;
        event_button_pressed();
    }
}

/* ===============================================================
 * Queries
 * =============================================================== */

bool event_is_essential_only(void)
{
    return g_event.essential_only;
}
