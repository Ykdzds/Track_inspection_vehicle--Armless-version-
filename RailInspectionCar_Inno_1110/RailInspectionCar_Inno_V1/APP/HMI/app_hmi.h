#ifndef APP_HMI_H
#define APP_HMI_H

#include <stdint.h>

void app_hmi_init(void);
void app_hmi_service(void);
/* Only queued descriptors count as work: a partial frame must await RTU quiet. */
uint8_t app_hmi_work_pending(void);
uint8_t app_hmi_cli_command(const char *line);

#endif
