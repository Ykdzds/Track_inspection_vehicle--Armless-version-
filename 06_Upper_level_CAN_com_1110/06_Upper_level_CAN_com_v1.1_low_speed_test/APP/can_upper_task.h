#ifndef CAN_UPPER_TASK_H
#define CAN_UPPER_TASK_H
#include <stdint.h>
typedef struct
{
    uint32_t reset_flags, oled_refresh_count, oled_last_ok, oled_fail_count;
    uint32_t oled_recovery_count, oled_last_error, oled_last_state, last_fault;
    uint32_t oled_frame_generation, control_progress, key_progress, oled_failure_at;
    uint16_t stack_min_words[8];
    uint8_t oled_state, oled_page, oled_failures, oled_hal_status;
    uint8_t oled_failed_page;
} UpperDiagnostics_t;
extern volatile UpperDiagnostics_t upper_diagnostics;
void UpperApp_Init(void);
void UpperApp_Start(void);
void UpperApp_Fault(uint32_t reason);
#endif
