#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* Ensure stdint is only used by the compiler, not the assembler. */
#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
 #include <stdint.h>
 extern uint32_t SystemCoreClock;
#endif

/* -------------------- Kernel configuration -------------------- */
#define configUSE_PREEMPTION              1
#define configCPU_CLOCK_HZ                (SystemCoreClock)  /* 100 MHz */
#define configTICK_RATE_HZ                ((TickType_t)1000)
#define configMAX_PRIORITIES              (7)
#define configMINIMAL_STACK_SIZE          ((uint16_t)128)    /* 128 words = 512 bytes */
#define configTOTAL_HEAP_SIZE             ((size_t)(8 * 1024)) /* 8 KB */
#define configMAX_TASK_NAME_LEN           (16)
#define configUSE_16_BIT_TICKS            0
#define configIDLE_SHOULD_YIELD           1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1

/* -------------------- Hook functions -------------------- */
#define configUSE_IDLE_HOOK               0
#define configUSE_TICK_HOOK               0
#define configUSE_MALLOC_FAILED_HOOK      0
#define configCHECK_FOR_STACK_OVERFLOW    2     /* paint + watermark */

/* -------------------- Synchronization primitives -------------------- */
#define configUSE_MUTEXES                 0
#define configUSE_RECURSIVE_MUTEXES       0
#define configUSE_COUNTING_SEMAPHORES     0
#define configQUEUE_REGISTRY_SIZE         0

/* -------------------- Software timers -------------------- */
#define configUSE_TIMERS                  0
#define configTIMER_TASK_PRIORITY         (2)
#define configTIMER_QUEUE_LENGTH          5
#define configTIMER_TASK_STACK_DEPTH      (configMINIMAL_STACK_SIZE)

/* -------------------- Memory allocation -------------------- */
#define configSUPPORT_STATIC_ALLOCATION   0
#define configSUPPORT_DYNAMIC_ALLOCATION  1

/* -------------------- Debug / trace -------------------- */
#define configUSE_TRACE_FACILITY          0
#define configGENERATE_RUN_TIME_STATS     0
#define configUSE_APPLICATION_TASK_TAG    0

/* -------------------- Co-routines (unused) -------------------- */
#define configUSE_CO_ROUTINES             0
#define configMAX_CO_ROUTINE_PRIORITIES   (2)

/* -------------------- Optional API includes -------------------- */
#define INCLUDE_vTaskPrioritySet          0
#define INCLUDE_uxTaskPriorityGet         0
#define INCLUDE_vTaskDelete               0
#define INCLUDE_vTaskCleanUpResources     0
#define INCLUDE_vTaskSuspend              1     /* required for portMAX_DELAY */
#define INCLUDE_vTaskDelayUntil           1
#define INCLUDE_vTaskDelay                1
#define INCLUDE_xTaskGetSchedulerState    1

/* -------------------- Cortex-M4 interrupt priorities -------------------- */
#ifdef __NVIC_PRIO_BITS
 #define configPRIO_BITS         __NVIC_PRIO_BITS   /* STM32F4 = 4 bits */
#else
 #define configPRIO_BITS         4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY       15

/* TIM2 (prio 1) and I2C (prio 2) are above this threshold.
 * They do NOT call any FreeRTOS API — this is safe. */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5

#define configKERNEL_INTERRUPT_PRIORITY         \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* -------------------- Assert -------------------- */
#define configASSERT(x) if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;); }

/* -------------------- Handler name mapping -------------------- */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
/* NOTE: SysTick_Handler is NOT mapped here — it is implemented manually
 * in stm32f4xx_it.c to handle pre-scheduler HAL_IncTick() and post-scheduler
 * FreeRTOS tick via xPortSysTickHandler(). */

#endif /* FREERTOS_CONFIG_H */
