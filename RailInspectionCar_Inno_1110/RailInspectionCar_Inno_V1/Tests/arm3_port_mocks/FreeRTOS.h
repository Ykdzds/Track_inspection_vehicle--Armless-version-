#ifndef ARM3_PORT_TEST_FREERTOS_H
#define ARM3_PORT_TEST_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef void *TaskHandle_t;
#define configTICK_RATE_HZ 1000U
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#define configASSERT(x) assert(x)
void test_port_enter(void);
void test_port_exit(void);
#define taskENTER_CRITICAL() test_port_enter()
#define taskEXIT_CRITICAL() test_port_exit()
#endif
