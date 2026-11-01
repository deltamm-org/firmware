/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Tommi Tauriainen */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* Kernel configuration for the AnyTone D878UVII (GD32F303, Cortex-M4F at
 * 120 MHz). Static allocation only; the kernel has no heap. */

#define configCPU_CLOCK_HZ 120000000
#define configTICK_RATE_HZ 1000
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configMAX_PRIORITIES 8
#define configMINIMAL_STACK_SIZE 128
#define configMAX_TASK_NAME_LEN 8
#define configSTACK_DEPTH_TYPE unsigned
#define configIDLE_SHOULD_YIELD 1

#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configKERNEL_PROVIDED_STATIC_MEMORY 1

#define configUSE_IDLE_HOOK 1
#define configUSE_TICK_HOOK 1
#define configUSE_MALLOC_FAILED_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_TIMERS 0
#define configUSE_TASK_NOTIFICATIONS 1
#define configUSE_TRACE_FACILITY 0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configQUEUE_REGISTRY_SIZE 0
#define configENABLE_BACKWARD_COMPATIBILITY 0

/* Four priority bits. Interrupts at 0..4 never wait for the kernel and may
 * not call it; 5..15 may use the FromISR calls. */
#define configPRIO_BITS 4
#define configKERNEL_INTERRUPT_PRIORITY (15 << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (5 << (8 - configPRIO_BITS))

#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_vTaskDelete 0
#define INCLUDE_vTaskSuspend 1

/* A failed kernel check is a fatal error (os layer). */
#ifdef __cplusplus
extern "C" {
#endif
void dmmKernelCheckFailed(void);
#ifdef __cplusplus
}
#endif
#define configASSERT(x)         \
    if ((x) == 0) {             \
        dmmKernelCheckFailed(); \
    }

/* The port's handlers take the names the vector table uses. */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif
