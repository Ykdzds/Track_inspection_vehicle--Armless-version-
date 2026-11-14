#ifndef TEST_RS485_SERVO_PULSE_H
#define TEST_RS485_SERVO_PULSE_H

#include <stdint.h>

#include "aimtor_servo.h"

void servo_pulse_stop_all(void);
void servo_pulse_update_tim9_isr(void);
void servo_pulse_update_tim11_isr(void);
void servo_pulse_complete_axis(servo_axis_t axis);
uint8_t servo_pulse_output_active(servo_axis_t axis);
uint32_t servo_pulse_get_pulse_count(servo_axis_t axis);
uint32_t servo_pulse_get_target(servo_axis_t axis);
#endif
