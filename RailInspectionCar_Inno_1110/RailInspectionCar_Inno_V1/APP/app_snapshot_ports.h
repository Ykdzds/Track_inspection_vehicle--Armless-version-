#ifndef APP_SNAPSHOT_PORTS_H
#define APP_SNAPSHOT_PORTS_H

#include <stdint.h>
#include "HMI/app_control_arbiter.h"

struct nrf_status_input;
struct nrf_snapshot_context;
struct app_can_status_input;
uint8_t app_system_snapshot_read_nrf(struct nrf_status_input *input,
    struct nrf_snapshot_context *context);
uint8_t app_system_snapshot_read_can(struct app_can_status_input *input);

#endif
