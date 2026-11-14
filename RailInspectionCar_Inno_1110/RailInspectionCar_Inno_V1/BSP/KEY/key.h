#ifndef KEY_H
#define KEY_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

typedef enum
{
    KEY_ID_1 = 0,
    KEY_ID_2,
    KEY_ID_3,
    KEY_ID_COUNT
} key_id_t;

typedef enum
{
    KEY_EVENT_SHORT = 0,
    KEY_EVENT_LONG,
    KEY_EVENT_DOUBLE
} key_event_type_t;

typedef struct
{
    key_id_t key;
    key_event_type_t type;
    TickType_t tick;
} key_event_t;

void key_init(void);
void key_scan_5ms(void);
bool key_get_event(key_event_t *event, TickType_t wait_ticks);

#endif
