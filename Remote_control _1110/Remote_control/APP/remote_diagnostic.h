#ifndef REMOTE_DIAGNOSTIC_H
#define REMOTE_DIAGNOSTIC_H

#include <stdint.h>
#include "remote_status_model.h"

void remote_diagnostic_init(void);
void remote_diagnostic_service(remote_status_model_t *model, uint32_t now_ms,
                                const char *oled_title);

#endif
