/*
 * objectdetection.c - LNC Object Detection module
 * See objectdetection.h for the public API and the class-level overview.
 */
#include "objectdetection.h"
#include "main.h"
#include "event.h"
#include "cmsis_os.h"
#include <stdbool.h>
#include <stdio.h>

/* TEMPORARY debug -- edge_isr_count is bumped from the ISR (a plain
 * increment, always ISR-safe) so the task-context printf below can
 * report whether the ISR ever actually fired, not just whether the
 * task woke. Remove both once the IR chain is confirmed working. */
static volatile uint32_t edge_isr_count = 0;

/* Woken for two different reasons -- an edge arrived while the object
 * was considered absent (EDGE), or TIM5 timed out with 10 s of no
 * edges (TIMEOUT). Only the task itself ever writes `present`, so a
 * plain bool read from either ISR is safe -- no writer race. */
#define OBJDET_FLAG_EDGE    (1U << 0)
#define OBJDET_FLAG_TIMEOUT (1U << 1)

struct ObjectDetection {
    osThreadId_t task_handle;
    bool present;
};

static struct ObjectDetection g_objdet;

/* ===============================================================
 * ISR-safe entry points -- both do the minimum: a plain register
 * write or a thread-flag signal, nothing that can block or touch
 * the SD card.
 * =============================================================== */

void objdet_on_edge(void)
{
    edge_isr_count++; /* TEMPORARY debug */

    __HAL_TIM_SET_COUNTER(&htim5, 0);

    if (!g_objdet.present) {
        (void)osThreadFlagsSet(g_objdet.task_handle, OBJDET_FLAG_EDGE);
    }
}

void objdet_on_timeout(void)
{
    (void)osThreadFlagsSet(g_objdet.task_handle, OBJDET_FLAG_TIMEOUT);
}

/* ===============================================================
 * Task -- the only place that actually calls Event, since that's
 * where touching the SD card through FatFS is safe.
 * =============================================================== */

static void objdet_task(void *argument)
{
    (void)argument;

    for (;;) {
        uint32_t flags = osThreadFlagsWait(OBJDET_FLAG_EDGE | OBJDET_FLAG_TIMEOUT,
                                            osFlagsWaitAny, osWaitForever);

        /* TEMPORARY debug -- safe here (task context, not an ISR).
         * edge_isr_count confirms whether the EXTI ISR is firing at
         * all; flags/present show what the task decided to do about
         * it. Remove once the IR chain is confirmed working. */
        printf("objdet: woke flags=0x%02lX edge_isr_count=%lu present=%d\r\n",
               (unsigned long)flags, (unsigned long)edge_isr_count,
               (int)g_objdet.present);

        if ((flags & OBJDET_FLAG_EDGE) && !g_objdet.present) {
            g_objdet.present = true;
            event_object_detected();
        }
        if ((flags & OBJDET_FLAG_TIMEOUT) && g_objdet.present) {
            g_objdet.present = false;
            event_object_cleared();
        }
    }
}

/* ===============================================================
 * Public API: create / destroy
 * =============================================================== */

ObjectDetection *objdet_create(void)
{
    const osThreadAttr_t task_attr = {
        .name = "objDetTask",
        .stack_size = 256 * 4,
        .priority = osPriorityNormal,
    };

    g_objdet.present = false;

    g_objdet.task_handle = osThreadNew(objdet_task, NULL, &task_attr);
    if (g_objdet.task_handle == NULL) {
        return NULL;
    }

    if (HAL_TIM_Base_Start_IT(&htim5) != HAL_OK) {
        return NULL;
    }

    return &g_objdet;
}

void objdet_destroy(ObjectDetection *o)
{
    if (o == NULL) {
        return;
    }
    HAL_TIM_Base_Stop_IT(&htim5);
}
