#ifndef APP_TUNE_CLI_H
#define APP_TUNE_CLI_H
#include <stdint.h>
typedef struct {
    uint32_t boot, ms, generation, sample_ms[2], sequence[2], age[2];
    int16_t actual[2], commanded[2];
    uint8_t owner, authorized, valid[2], source_valid[3], phase;
} app_tune_input_t;
void app_tune_snapshot(app_tune_input_t *s);
void app_tune_init(void);
uint8_t app_tune_command(char *line);
/* UART protocol only. The common worker independently services Flash storage. */
void app_tune_service(void);
#endif
