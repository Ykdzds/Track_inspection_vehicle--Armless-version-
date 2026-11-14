#ifndef SERVO_PULSE_H
#define SERVO_PULSE_H

#include <stdbool.h>
#include <stdint.h>

#include "aimtor_servo.h"

typedef enum
{
    SERVO_PULSE_OK = 0,             /* 操作成功 */
    SERVO_PULSE_INVALID_ARGUMENT,   /* 参数无效或超出范围 */
    SERVO_PULSE_NOT_CONFIGURED,     /* 硬件或配置未确认，拒绝操作 */
    SERVO_PULSE_HAL_ERROR           /* 底层定时器操作失败 */
} servo_pulse_result_t;

void servo_pulse_init(void);
servo_pulse_result_t servo_pulse_set_enable(servo_axis_t axis, bool enable);
servo_pulse_result_t servo_pulse_set_direction(servo_axis_t axis, bool forward);
servo_pulse_result_t servo_pulse_set_frequency(servo_axis_t axis,
                                               uint32_t frequency_hz);
void servo_pulse_stop_all(void);
/* 正常定脉冲完成时关闭指定轴输出，但保留脉冲许可和伺服使能。 */
void servo_pulse_complete_axis(servo_axis_t axis);
/* 返回指定轴当前是否仍有PWM脉冲输出。 */
uint8_t servo_pulse_output_active(servo_axis_t axis);
uint32_t servo_pulse_get_frequency(servo_axis_t axis);
int64_t servo_pulse_get_command_position(servo_axis_t axis);
void servo_pulse_accumulate(uint32_t period_ms);

/* 为指定轴设置一次性脉冲目标；目标为0表示连续输出模式。 */
void servo_pulse_set_target(servo_axis_t axis, uint32_t pulses);
/* 返回指定轴已经由定时器更新事件确认的实际脉冲数。 */
uint32_t servo_pulse_get_pulse_count(servo_axis_t axis);
/* 返回指定轴的一次性脉冲目标数量。 */
uint32_t servo_pulse_get_target(servo_axis_t axis);
/* 返回指定轴的一次性脉冲目标是否已经达到。 */
bool servo_pulse_target_reached(servo_axis_t axis);
/* TIM9更新中断调用入口，处理左轴定脉冲计数。 */
void servo_pulse_update_tim9_isr(void);
/* TIM11更新中断调用入口，处理右轴定脉冲计数。 */
void servo_pulse_update_tim11_isr(void);

#endif
