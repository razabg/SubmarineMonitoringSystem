#include "DHT11.h"
#include <stdlib.h>

// ── Private struct definition (hidden from client) ────

struct DHT_Handle {
    GPIO_TypeDef      *port;
    uint16_t           pin;
    TIM_HandleTypeDef *timer;
};

// ── Private helpers ───────────────────────────────────

static void delay_us(DHT_Handle *h, uint32_t us)
{
    __HAL_TIM_SET_COUNTER(h->timer, 0);
    while (__HAL_TIM_GET_COUNTER(h->timer) < us);
}

static void set_pin_output(DHT_Handle *h)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = h->pin;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(h->port, &gpio);
}

static void set_pin_input(DHT_Handle *h)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = h->pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(h->port, &gpio);
}

static DHT_Result send_start_signal(DHT_Handle *h)
{
    set_pin_output(h);
    HAL_GPIO_WritePin(h->port, h->pin, GPIO_PIN_RESET);
    delay_us(h, 18000);
    HAL_GPIO_WritePin(h->port, h->pin, GPIO_PIN_SET);
    delay_us(h, 40);
    set_pin_input(h);
    return DHT_OK;
}

static DHT_Result wait_for_response(DHT_Handle *h)
{
    __HAL_TIM_SET_COUNTER(h->timer, 0);
    while (HAL_GPIO_ReadPin(h->port, h->pin) == GPIO_PIN_SET)
    {
        if (__HAL_TIM_GET_COUNTER(h->timer) > 100)
            return DHT_NO_RESPONSE;
    }

    __HAL_TIM_SET_COUNTER(h->timer, 0);
    while (HAL_GPIO_ReadPin(h->port, h->pin) == GPIO_PIN_RESET)
    {
        if (__HAL_TIM_GET_COUNTER(h->timer) > 100)
            return DHT_NO_RESPONSE;
    }

    __HAL_TIM_SET_COUNTER(h->timer, 0);
    while (HAL_GPIO_ReadPin(h->port, h->pin) == GPIO_PIN_SET)
    {
        if (__HAL_TIM_GET_COUNTER(h->timer) > 100)
            return DHT_NO_RESPONSE;
    }

    return DHT_OK;
}

/* Bit timeout: a bit's low+high phases are each well under 100us on a
 * healthy line (see wait_for_response()'s identical bound); this guards
 * against a missed/glitched edge spinning the caller forever instead of
 * reporting DHT_NO_RESPONSE. */
#define DHT_BIT_TIMEOUT_US 100

static int read_bit(DHT_Handle *h)
{
    __HAL_TIM_SET_COUNTER(h->timer, 0);
    while (HAL_GPIO_ReadPin(h->port, h->pin) == GPIO_PIN_RESET)
    {
        if (__HAL_TIM_GET_COUNTER(h->timer) > DHT_BIT_TIMEOUT_US)
            return -1;
    }

    __HAL_TIM_SET_COUNTER(h->timer, 0);
    while (HAL_GPIO_ReadPin(h->port, h->pin) == GPIO_PIN_SET)
    {
        if (__HAL_TIM_GET_COUNTER(h->timer) > DHT_BIT_TIMEOUT_US)
            return -1;
    }

    return (__HAL_TIM_GET_COUNTER(h->timer) > 50) ? 1 : 0;
}

static DHT_Result read_40_bits(DHT_Handle *h, uint8_t *bytes)
{
    for (int i = 0; i < 5; i++)
        bytes[i] = 0;

    for (int i = 0; i < 40; i++)
    {
        int bit = read_bit(h);
        if (bit < 0)
            return DHT_NO_RESPONSE;

        int byte_index = i / 8;
        bytes[byte_index] = (uint8_t)((bytes[byte_index] << 1) | (uint8_t)bit);
    }
    return DHT_OK;
}

// ── Public functions ──────────────────────────────────

DHT_Handle *DHT_Create(GPIO_TypeDef *port, uint16_t pin, TIM_HandleTypeDef *timer)
{
    DHT_Handle *h = malloc(sizeof(DHT_Handle)); //in some cases it is better to use static allocation!
    if (!h) return NULL;
    h->port  = port;
    h->pin   = pin;
    h->timer = timer;
    return h;
}

void DHT_Destroy(DHT_Handle *h)
{
    free(h);
}

DHT_Result DHT_Read(DHT_Handle *h, DHT_Data *out)
{
    uint8_t bytes[5];

    send_start_signal(h);

    if (wait_for_response(h) != DHT_OK)
        return DHT_NO_RESPONSE;

    if (read_40_bits(h, bytes) != DHT_OK)
        return DHT_NO_RESPONSE;

    uint8_t sum = bytes[0] + bytes[1] + bytes[2] + bytes[3];
    if (sum != bytes[4])
        return DHT_CHECKSUM_ERROR;

    out->humidity_int    = bytes[0];
    out->humidity_dec    = bytes[1];
    out->temperature_int = bytes[2];
    out->temperature_dec = bytes[3];

    return DHT_OK;
}


////important ! there is anther button interrupt callback in basics.c make sure its under comment to use this callback
////take a look at dht2 that is being built in a better way in terms of ADT
//void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
//{
//    if (GPIO_Pin == but1_Pin)
//    {
//        if (HAL_GPIO_ReadPin(but1_GPIO_Port, but1_Pin) == GPIO_PIN_RESET)
//        {
//            // Falling Edge — button pressed → read DHT
//            DHT_Data data;
//            DHT_Result result = DHT_Read(&data);
//
//            if (result == DHT_OK)
//            {
//                printf("Temperature: %d.%d C\r\n",
//                       data.temperature_int,
//                       data.temperature_dec);
//                printf("Humidity:    %d.%d%%\r\n",
//                       data.humidity_int,
//                       data.humidity_dec);
//            }
//            else if (result == DHT_NO_RESPONSE)
//            {
//                printf("DHT11 not responding\r\n");
//            }
//            else
//            {
//                printf("Checksum error\r\n");
//            }
//        }
//        // no need to handle release for this task
//    }
//}
