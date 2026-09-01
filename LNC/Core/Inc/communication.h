#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Communication Communication;
/* Opaque handle — its fields live only in communication.c. */

/* Brings up the one Communication instance: creates its queues, semaphore,
 * and its two FreeRTOS tasks, and arms the first UART RX byte. Call once
 * from main(), after osKernelInitialize() and before osKernelStart().
 * Returns the handle, or NULL if an RTOS object failed to create. */
Communication *communication_create(void);

/* Tears down the tasks/queues/semaphore. Null-safe (comm == NULL is a
 * no-op), like free(). Provided for ADT completeness; not expected to be
 * called in practice on real hardware. */
void communication_destroy(Communication *comm);

/* Queues one outgoing TLV frame on `comm`. Priority is worked out
 * internally from `tag` — you never pick a queue yourself. `value` may be
 * NULL only if value_len == 0. Non-blocking; returns 0 if queued, -1 if
 * `comm`/`value` are invalid or the tag's queue is full (dropped).
 * Do not call this from an ISR. */
int comm_send(Communication *comm, uint8_t tag,
              const uint8_t *value, uint8_t value_len);

#ifdef __cplusplus
}
#endif

#endif /* COMMUNICATION_H */
