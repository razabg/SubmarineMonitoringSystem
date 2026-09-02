#ifndef DHT11_H
#define DHT11_H

#include "main.h"
#include <stdint.h>

/* Opaque handle — client sees only the pointer */
typedef struct DHT_Handle DHT_Handle;

typedef struct {
    uint8_t humidity_int;
    uint8_t humidity_dec;
    uint8_t temperature_int;
    uint8_t temperature_dec;
} DHT_Data;

typedef enum {
    DHT_OK,
    DHT_NO_RESPONSE,
    DHT_CHECKSUM_ERROR
} DHT_Result;

DHT_Handle *DHT_Create (GPIO_TypeDef *port, uint16_t pin, TIM_HandleTypeDef *timer);
void        DHT_Destroy(DHT_Handle *h);
DHT_Result  DHT_Read   (DHT_Handle *h, DHT_Data *out);

#endif
