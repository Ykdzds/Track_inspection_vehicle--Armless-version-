#include "app_config.h"

/* APP_SERVO_OUTPUT_POLARITY_CONFIRMED表示脉冲输出门控有效电平已经确认。 */
/* APP_SERVO_DIRECTION_CONFIRMED表示方向控制电平已经确认。 */
uint8_t app_config_motion_hardware_ready(void)
{
    return (uint8_t)(app_config_motion_ramp_valid() &&
                     APP_SERVO_OUTPUT_POLARITY_CONFIRMED &&
                     APP_SERVO_DIRECTION_CONFIRMED);
}

/* 判断伺服总线地址是否已经确认；不代表实际通信已经建立。 */
uint8_t app_config_servo_bus_ready(void)
{
    /* 地址确认后返回1，否则返回0。 */
    return (uint8_t)(APP_SERVO_ADDRESS_CONFIRMED != 0U);
}

/* 表示伺服脉冲格式和电子齿轮参数已经完成项目确认。 */
/* 返回1表示确认，返回0表示尚未确认。 */
uint8_t app_config_servo_parameters_confirmed(void)
{
    return (uint8_t)(APP_SERVO_PULSE_FORMAT_CONFIRMED &&
                     APP_SERVO_ELECTRONIC_GEAR_CONFIRMED);
}
/* 根据编译期查询模式判断指定轴是否参与Modbus轮询。 */
uint8_t app_config_servo_axis_selected(uint8_t axis)
{
    if (axis > 1U)
    {
        return 0U;
    }
#if APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_BOTH
    return 1U;
#elif APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_LEFT_ONLY
    return (uint8_t)(axis == 0U);
#else
    return (uint8_t)(axis == 1U);
#endif
}

/* 返回只读测试总开关状态，供通信、CLI和健康任务统一使用。 */
uint8_t app_config_servo_readonly_test(void)
{
    return (uint8_t)(APP_SERVO_READONLY_TEST != 0U);
}

/* 返回当前编译配置指定的实际控制轴。 */
uint8_t app_config_servo_control_axis(void)
{
    return (uint8_t)APP_SERVO_CONTROL_AXIS;
}

/* 只有查询模式选中的控制轴才允许提交实际控制命令。 */
uint8_t app_config_servo_axis_controllable(uint8_t axis)
{
    uint8_t bit;

    if (axis > 1U)
    {
        return 0U;
    }
    bit = (uint8_t)(1U << axis);
    return (uint8_t)(((app_config_servo_control_axis() & bit) != 0U) &&
                     app_config_servo_axis_selected(axis) &&
                     (APP_SERVO_READONLY_TEST == 0U));
}

uint8_t app_config_servo_control_axis_mask(void)
{
    return (uint8_t)APP_SERVO_CONTROL_AXIS;
}

uint8_t app_config_servo_control_axis_count(void)
{
    uint8_t mask = app_config_servo_control_axis_mask();
    uint8_t count = 0U;

    if ((mask & APP_SERVO_CONTROL_AXIS_LEFT) != 0U)
    {
        count++;
    }
    if ((mask & APP_SERVO_CONTROL_AXIS_RIGHT) != 0U)
    {
        count++;
    }
    return count;
}

const char *app_config_servo_control_axis_name(void)
{
#if APP_SERVO_CONTROL_AXIS == APP_SERVO_CONTROL_AXIS_LEFT
    return "left";
#elif APP_SERVO_CONTROL_AXIS == APP_SERVO_CONTROL_AXIS_RIGHT
    return "right";
#else
    return "both";
#endif
}
