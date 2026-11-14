#include "app_config.h"
#if APP_CHASSIS_LCD_ENABLED != 1
#error Source default mismatch: APP_CHASSIS_LCD_ENABLED
#endif
#if APP_DEBUG_CLI_ENABLED != 0
#error Source default mismatch: APP_DEBUG_CLI_ENABLED
#endif
#if APP_TUNE_UART_ENABLED != 0
#error Source default mismatch: APP_TUNE_UART_ENABLED
#endif
#if APP_SERVO_CLI_DEBUG_ENABLED != 0
#error Source default mismatch: APP_SERVO_CLI_DEBUG_ENABLED
#endif
#if APP_SERVO_PULSE_ENABLED != 0
#error Source default mismatch: APP_SERVO_PULSE_ENABLED
#endif
#if APP_HMI_LINK_DIAG_ENABLED != 0
#error Source default mismatch: APP_HMI_LINK_DIAG_ENABLED
#endif
#if APP_NRF_DIAGNOSTIC_ENABLED != 0
#error Source default mismatch: APP_NRF_DIAGNOSTIC_ENABLED
#endif
#if APP_CAN_SERVO_DIAG_ENABLED != 0
#error Source default mismatch: APP_CAN_SERVO_DIAG_ENABLED
#endif
#if APP_CAN_ARM_DIAG_ENABLED != 0
#error Source default mismatch: APP_CAN_ARM_DIAG_ENABLED
#endif
#if APP_SERVO_RS485_DIAG_ENABLED != 0
#error Source default mismatch: APP_SERVO_RS485_DIAG_ENABLED
#endif
#if APP_CAN_DEBUG_ENABLED != 0
#error Source default mismatch: APP_CAN_DEBUG_ENABLED
#endif
#if APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED != 0
#error Source default mismatch: APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED
#endif
#if APP_UART_RX_ACTIVE != 0
#error Source default mismatch: APP_UART_RX_ACTIVE
#endif
#if APP_CLI_SERVO_ACTIVE != 0
#error Source default mismatch: APP_CLI_SERVO_ACTIVE
#endif
#define SOURCE_DEFAULT_STRING_(x) #x
#define SOURCE_DEFAULT_STRING(x) SOURCE_DEFAULT_STRING_(x)
const char source_default_APP_CHASSIS_LCD_ENABLED[] = SOURCE_DEFAULT_STRING(APP_CHASSIS_LCD_ENABLED);
const char source_default_APP_DEBUG_CLI_ENABLED[] = SOURCE_DEFAULT_STRING(APP_DEBUG_CLI_ENABLED);
const char source_default_APP_TUNE_UART_ENABLED[] = SOURCE_DEFAULT_STRING(APP_TUNE_UART_ENABLED);
const char source_default_APP_SERVO_CLI_DEBUG_ENABLED[] = SOURCE_DEFAULT_STRING(APP_SERVO_CLI_DEBUG_ENABLED);
const char source_default_APP_SERVO_PULSE_ENABLED[] = SOURCE_DEFAULT_STRING(APP_SERVO_PULSE_ENABLED);
const char source_default_APP_HMI_LINK_DIAG_ENABLED[] = SOURCE_DEFAULT_STRING(APP_HMI_LINK_DIAG_ENABLED);
const char source_default_APP_NRF_DIAGNOSTIC_ENABLED[] = SOURCE_DEFAULT_STRING(APP_NRF_DIAGNOSTIC_ENABLED);
const char source_default_APP_CAN_SERVO_DIAG_ENABLED[] = SOURCE_DEFAULT_STRING(APP_CAN_SERVO_DIAG_ENABLED);
const char source_default_APP_CAN_ARM_DIAG_ENABLED[] = SOURCE_DEFAULT_STRING(APP_CAN_ARM_DIAG_ENABLED);
const char source_default_APP_SERVO_RS485_DIAG_ENABLED[] = SOURCE_DEFAULT_STRING(APP_SERVO_RS485_DIAG_ENABLED);
const char source_default_APP_CAN_DEBUG_ENABLED[] = SOURCE_DEFAULT_STRING(APP_CAN_DEBUG_ENABLED);
const char source_default_APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED[] = SOURCE_DEFAULT_STRING(APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED);
const char source_default_APP_UART_RX_ACTIVE[] = SOURCE_DEFAULT_STRING(APP_UART_RX_ACTIVE);
const char source_default_APP_CLI_SERVO_ACTIVE[] = SOURCE_DEFAULT_STRING(APP_CLI_SERVO_ACTIVE);
