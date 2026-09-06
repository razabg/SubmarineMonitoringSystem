/*
 * objectdetection.h - LNC Object Detection module
 *
 * Section 2.2: the VS1838B IR receiver on PB10 stands in for a sonar
 * sensor (see CLAUDE.md's "Object Detection sensor" bullet) --
 * presence-by-recency, not by level. Every edge on PB10 refreshes a
 * 10 s hardware countdown (TIM5); if nothing refreshes it in time,
 * the object is considered gone. An edge arriving while the object
 * was considered absent is what actually fires "object detected" --
 * not one message per edge (a single IR transmission is a burst of
 * many edges, not one clean transition, same reasoning as the
 * button's debounce).
 *
 * Both PB10's EXTI callback and TIM5's timeout callback run in real
 * interrupt context, and event_object_detected()/event_object_cleared()
 * touch the SD card through FatFS (which uses an RTOS semaphore
 * internally) -- illegal to call from an ISR. So neither callback
 * calls Event directly; both just wake this module's own dedicated
 * task (via osThreadFlagsSet(), confirmed ISR-safe), which does the
 * actual state transition and Event call in safe task context.
 *
 * ADT, matching monitor.c's/event.c's/log.c's/init.c's/config.c's
 * shape: opaque handle, one static instance, create()/destroy().
 */
#ifndef OBJECTDETECTION_H
#define OBJECTDETECTION_H

typedef struct ObjectDetection ObjectDetection;
/* Opaque handle -- fields live only in objectdetection.c. */

/* Call once from main() (via Init, same as every other module).
 * Starts TIM5 and the dedicated task. Returns the handle, or NULL if
 * either failed to create. */
ObjectDetection *objdet_create(void);

/* Provided for ADT completeness; not expected to be called in practice
 * on real hardware. Null-safe. */
void objdet_destroy(ObjectDetection *o);

/* Called from event.c's shared HAL_GPIO_EXTI_Callback when
 * IR_RECEIVER_SONAR_Pin fires -- ISR context, does the minimum
 * (resets TIM5's counter directly, then signals the task only if the
 * object wasn't already considered present). */
void objdet_on_edge(void);

/* Called from buzzer.c's shared HAL_TIM_PeriodElapsedCallback when
 * TIM5 times out (10 s with no edge) -- ISR context, does the minimum
 * (just signals the task). */
void objdet_on_timeout(void);

#endif /* OBJECTDETECTION_H */
