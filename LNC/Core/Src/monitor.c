/*
 * monitor.c - LNC Monitor module
 * See monitor.h for the public API and the class-level overview.
 *
 * Flow: monitor_task() wakes every 5 s (osDelayUntil) -> monitor_sample()
 * reads the sensors -> classify_mode() turns the reading into a mode
 * (section 2.10's rule) -> log_write() runs every round, event_mode_changed()
 * only when the mode actually differs from last round.
 */
#include "monitor.h"
#include "main.h"
#include "cmsis_os.h"
#include "DHT11.h"

/* ===============================================================
 * Limits
 *
 * Placeholder defaults -- Configuration will own the real values once
 * it exists (section 2.6), persisted in Flash and changeable at
 * runtime. Chosen with submarine-cabin reasoning for now: temperature
 * has a real range (too hot *and* too cold are both risks, one hull
 * thickness from cold seawater); humidity/light/battery are lower-bound
 * only, matching the TLV command tags already fixed in tlv.h (SET_HUM_*/
// * SET_LIGHT_*/SET_BATT_* are documented there as lower bounds, no
// * ceiling concept exists in the wire protocol as it stands).
// * =============================================================== */

#define TEMP_NORMAL_MIN     15
#define TEMP_NORMAL_MAX     27
#define TEMP_WARNING_MIN    10
#define TEMP_WARNING_MAX    30

#define HUMIDITY_NORMAL_MIN   30
#define HUMIDITY_WARNING_MIN  20

#define LIGHT_NORMAL_MIN    40
#define LIGHT_WARNING_MIN   20

#define BATTERY_NORMAL_MIN   40
#define BATTERY_WARNING_MIN  20

/* ===============================================================
 * Module state
 * =============================================================== */

struct Monitor {
    monitor_mode_t mode;      /* mode as of the last completed round */
    osThreadId_t   task_handle;
    DHT_Handle    *dht;
    int16_t        last_temp_c;
    uint8_t        last_humidity_pct;
};

static struct Monitor g_monitor;

/* ===============================================================
 * Public API: create / destroy
 * =============================================================== */

static void monitor_task(void *argument);

Monitor *monitor_create(void)
{
    const osThreadAttr_t task_attr = {
        .name = "monitorTask",
        .stack_size = 256 * 4,
        .priority = osPriorityNormal,
    };

    g_monitor.mode = MODE_UNKNOWN;
    g_monitor.last_temp_c = 0;
    g_monitor.last_humidity_pct = 0;

    g_monitor.dht = DHT_Create(DHT_GPIO_Port, DHT_Pin, &htim2);
    if (g_monitor.dht == NULL) {
        return NULL;
    }

    g_monitor.task_handle = osThreadNew(monitor_task, &g_monitor, &task_attr);
    if (g_monitor.task_handle == NULL) {
        DHT_Destroy(g_monitor.dht);
        return NULL;
    }

    return &g_monitor;
}

void monitor_destroy(Monitor *m)
{
    if (m == NULL) {
        return;
    }
    if (m->task_handle != NULL) {
        osThreadTerminate(m->task_handle);
    }
    if (m->dht != NULL) {
        DHT_Destroy(m->dht);
    }
}

/* ===============================================================
 * Classification
 *
 * Per-sensor tier first, then aggregated per section 2.10's exact rule:
 * Error if *any* sensor is in its own Error tier; else Warning if *any*
 * sensor is in its own Warning tier; else every sensor was Normal.
 * =============================================================== */

typedef enum { TIER_NORMAL, TIER_WARNING, TIER_ERROR } sensor_tier_t;

static sensor_tier_t classify_range(int32_t value,
                                     int32_t normal_min, int32_t normal_max,
                                     int32_t warning_min, int32_t warning_max)
{
    if (value >= normal_min && value <= normal_max) {
        return TIER_NORMAL;
    }
    if (value >= warning_min && value <= warning_max) {
        return TIER_WARNING;
    }
    return TIER_ERROR;
}

static sensor_tier_t classify_lower_bound(int32_t value, int32_t normal_min, int32_t warning_min)
{
    if (value >= normal_min) {
        return TIER_NORMAL;
    }
    if (value >= warning_min) {
        return TIER_WARNING;
    }
    return TIER_ERROR;
}

static monitor_mode_t classify_mode(const monitor_measurement_t *data)
{
    sensor_tier_t temp  = classify_range(data->temp_c, TEMP_NORMAL_MIN, TEMP_NORMAL_MAX,
                                          TEMP_WARNING_MIN, TEMP_WARNING_MAX);
    sensor_tier_t hum   = classify_lower_bound(data->humidity_pct, HUMIDITY_NORMAL_MIN, HUMIDITY_WARNING_MIN);
    sensor_tier_t light = classify_lower_bound(data->light_pct, LIGHT_NORMAL_MIN, LIGHT_WARNING_MIN);
    sensor_tier_t batt  = classify_lower_bound(data->battery_pct, BATTERY_NORMAL_MIN, BATTERY_WARNING_MIN);

    if (temp == TIER_ERROR || hum == TIER_ERROR || light == TIER_ERROR || batt == TIER_ERROR) {
        return MODE_ERROR;
    }
    if (temp == TIER_WARNING || hum == TIER_WARNING || light == TIER_WARNING || batt == TIER_WARNING) {
        return MODE_WARNING;
    }
    return MODE_NORMAL;
}

/* ===============================================================
 * Mode <-> string
 * =============================================================== */

const char *monitor_mode_name(monitor_mode_t mode)
{
    switch (mode) {
    case MODE_UNKNOWN: return "UNKNOWN";
    case MODE_NORMAL:  return "NORMAL";
    case MODE_WARNING: return "WARNING";
    case MODE_ERROR:   return "ERROR";
    default:           return "?";
    }
}

/* ===============================================================
 * Dispatch stubs
 *
 * Empty placeholders so the firmware links before Log/Event exist --
 * same pattern communication.c already uses for Configuration/Init/Log.
 * Each real module later defines the same-named non-weak function to
 * take over; no edits needed here when that happens.
 * =============================================================== */

__attribute__((weak)) void log_write(const monitor_measurement_t *data, monitor_mode_t mode)
{
    (void)data;
    (void)mode;
}

__attribute__((weak)) void event_mode_changed(const monitor_measurement_t *data,
                                               monitor_mode_t old_mode, monitor_mode_t new_mode)
{
    (void)data;
    (void)old_mode;
    (void)new_mode;
}

/* ===============================================================
 * Sampling
 * =============================================================== */

static uint8_t read_adc_pct(ADC_HandleTypeDef *hadc, uint32_t max_value)
{
    uint32_t raw;

    HAL_ADC_Start(hadc);
    HAL_ADC_PollForConversion(hadc, HAL_MAX_DELAY);
    raw = HAL_ADC_GetValue(hadc);
    HAL_ADC_Stop(hadc);

    return (uint8_t)((raw * 100u) / max_value);
}

static void monitor_sample(Monitor *self, monitor_measurement_t *data)
{
    DHT_Data dht_data;

    data->battery_pct = read_adc_pct(&hadc1, 4095u);
    data->light_pct   = read_adc_pct(&hadc2, 255u);

    if (DHT_Read(self->dht, &dht_data) == DHT_OK) {
        self->last_temp_c = (int16_t)dht_data.temperature_int;
        self->last_humidity_pct = dht_data.humidity_int;
    }
    /* else: DHT11 read failed (no response / bad checksum) -- keep the
     * last known-good reading rather than report garbage, or force a
     * spurious mode transition off one noisy sample. */

    data->temp_c = self->last_temp_c;
    data->humidity_pct = self->last_humidity_pct;
}

/* ===============================================================
 * Task
 * =============================================================== */

static void monitor_task(void *argument)
{
    Monitor *self = (Monitor *)argument;
    uint32_t tick = osKernelGetTickCount();

    for (;;) {
        monitor_measurement_t data;
        monitor_mode_t new_mode;

        monitor_sample(self, &data);
        new_mode = classify_mode(&data);

        if (new_mode != self->mode) {
            event_mode_changed(&data, self->mode, new_mode);
        }
        log_write(&data, new_mode);

        self->mode = new_mode;

        tick += 5000U;
        osDelayUntil(tick);
    }
}

/* Kept for reference -- this was main.c's TEMPORARY debug log_write(),
 * removed once the real Log module took over the strong definition
 * (having both would be a duplicate-symbol link error, same class as
 * the EXTI3/USART2 IRQHandler collisions). Useful again only if Log
 * needs a UART-visible fallback for on-hardware debugging.
 *
 * void log_write(const monitor_measurement_t *data, monitor_mode_t mode)
 * {
 *     printf("temp=%dC hum=%u%% light=%u%% batt=%u%% mode=%s\r\n",
 *            data->temp_c, data->humidity_pct, data->light_pct,
 *            data->battery_pct, monitor_mode_name(mode));
 * }
 */
