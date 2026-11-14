#include "app_config.h"
#if APP_SERVO_PULSE_ENABLED
#include "servo_pulse.h"

#include "app_config.h"
#include "main.h"
#include "tim.h"

typedef struct
{
    TIM_HandleTypeDef *timer;
    uint32_t channel;
    GPIO_TypeDef *direction_port;
    uint16_t direction_pin;
    volatile uint8_t permitted; /* 该轴是否允许输出脉冲 */
    volatile uint32_t frequency_hz; /* 当前脉冲输出频率 */
    volatile uint8_t forward; /* 当前运动方向 */
    volatile int64_t command_position; /* 软件累计的命令位置 */
    volatile uint32_t pulse_fraction; /* 软件位置累计中的小数部分 */
    volatile uint32_t pulse_count; /* 定时器更新事件确认的实际输出脉冲数 */
    volatile uint32_t pulse_target; /* 一次性脉冲目标，0表示连续输出 */
    volatile uint8_t pulse_counting; /* 是否正在执行一次性脉冲计数 */
} servo_pulse_axis_t;
static servo_pulse_axis_t s_axes[SERVO_AXIS_COUNT];

/* PC8和PC9是同一芯片的互补门禁，不是左右伺服使能；一次BSRR同步更新。*/
/*
* enable = true  -> PC8 = 1, PC9 = 0
* enable = false -> PC8 = 0, PC9 = 1
*/
static void servo_pulse_gate(bool enable)
{
    GPIOC->BSRR = enable ?
        ((uint32_t)GPIO_PIN_9 << 16U) | GPIO_PIN_8 :
        ((uint32_t)GPIO_PIN_8 << 16U) | GPIO_PIN_9;
}

static GPIO_PinState servo_pulse_level(uint8_t level)
{
    return level != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

/* 到数中断也调用此函数：先预置低电平，再切换为推挽GPIO，避免悬空。 */
static void servo_pulse_pin_idle(servo_axis_t axis)
{
    GPIO_TypeDef *port = axis == SERVO_AXIS_LEFT ? GPIOE : GPIOB;
    uint32_t pin_number = axis == SERVO_AXIS_LEFT ? 5U : 9U;
    uint32_t mode_shift = pin_number * 2U;

    port->BSRR = (1UL << pin_number) << 16U;
    port->OTYPER &= ~(1UL << pin_number);
    port->PUPDR &= ~(3UL << mode_shift);
    port->MODER = (port->MODER & ~(3UL << mode_shift)) |
                  (1UL << mode_shift);
}

/* 仅在启动新脉冲时恢复复用，停止期间不依赖定时器的空闲输出电平。 */
static void servo_pulse_pin_pwm(servo_axis_t axis)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = axis == SERVO_AXIS_LEFT ? GPIO_PIN_5 : GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = axis == SERVO_AXIS_LEFT ? GPIO_AF3_TIM9 :
                                             GPIO_AF3_TIM11;
    HAL_GPIO_Init(axis == SERVO_AXIS_LEFT ? GPIOE : GPIOB, &gpio);
}

void servo_pulse_init(void)
{
    s_axes[SERVO_AXIS_LEFT].timer = &htim9;
    s_axes[SERVO_AXIS_LEFT].channel = TIM_CHANNEL_1;
    s_axes[SERVO_AXIS_LEFT].direction_port = GPIOB;
    s_axes[SERVO_AXIS_LEFT].direction_pin = GPIO_PIN_0;
    s_axes[SERVO_AXIS_RIGHT].timer = &htim11;
    s_axes[SERVO_AXIS_RIGHT].channel = TIM_CHANNEL_1;
    s_axes[SERVO_AXIS_RIGHT].direction_port = GPIOB;
    s_axes[SERVO_AXIS_RIGHT].direction_pin = GPIO_PIN_1;
    servo_pulse_stop_all();
    /* 默认物理电平为高，不依赖运动许可；同步对应的软件方向。 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_1, GPIO_PIN_SET);
    s_axes[SERVO_AXIS_LEFT].forward =
        APP_SERVO_LEFT_FORWARD_LEVEL != 0U ? 1U : 0U;
    s_axes[SERVO_AXIS_RIGHT].forward =
        APP_SERVO_RIGHT_FORWARD_LEVEL != 0U ? 1U : 0U;
}

void servo_pulse_set_target(servo_axis_t axis, uint32_t pulses)
{
    if ((axis >= SERVO_AXIS_COUNT) || (s_axes[axis].timer == NULL))
    {
        return;
    }
    s_axes[axis].pulse_count = 0U;
    s_axes[axis].pulse_target = pulses;
    s_axes[axis].pulse_counting = pulses != 0U ? 1U : 0U;
}

uint32_t servo_pulse_get_pulse_count(servo_axis_t axis)
{
    return axis < SERVO_AXIS_COUNT ? s_axes[axis].pulse_count : 0U;
}

uint32_t servo_pulse_get_target(servo_axis_t axis)
{
    return axis < SERVO_AXIS_COUNT ? s_axes[axis].pulse_target : 0U;
}

bool servo_pulse_target_reached(servo_axis_t axis)
{
    if (axis >= SERVO_AXIS_COUNT)
    {
        return false;
    }
    return (s_axes[axis].pulse_counting == 0U) &&
           (s_axes[axis].pulse_target != 0U) &&
           (s_axes[axis].pulse_count >= s_axes[axis].pulse_target);
}

void servo_pulse_complete_axis(servo_axis_t axis)
{
    TIM_TypeDef *timer;

    if ((axis >= SERVO_AXIS_COUNT) || (s_axes[axis].timer == NULL))
    {
        return;
    }
    timer = s_axes[axis].timer->Instance;

    /*
     * 正常定脉冲完成只关闭本轴脉冲输出。
     * 不清除permitted、pulse_count和pulse_target，也不触碰伺服使能。
     */
    timer->DIER &= ~TIM_DIER_UIE;
    timer->CCER &= ~TIM_CCER_CC1E;
    /* 任务上下文释放HAL通道状态，下一条显式命令才能重新启动PWM。 */
    (void)HAL_TIM_PWM_Stop(s_axes[axis].timer, s_axes[axis].channel);
    __HAL_TIM_SET_COMPARE(s_axes[axis].timer, s_axes[axis].channel, 0U);
    timer->EGR = TIM_EGR_UG;
    timer->SR &= ~TIM_SR_UIF;
    servo_pulse_pin_idle(axis);
    s_axes[axis].frequency_hz = 0U;
    s_axes[axis].pulse_counting = 0U;

    if ((s_axes[0].frequency_hz == 0U) &&
        (s_axes[1].frequency_hz == 0U))
    {
        servo_pulse_gate(false);
    }

    /* 正常完成只关闭PULSE，保留停止前的DIR和伺服使能状态。 */
}

uint8_t servo_pulse_output_active(servo_axis_t axis)
{
    return (axis < SERVO_AXIS_COUNT) &&
           (s_axes[axis].frequency_hz != 0U);
}

static void servo_pulse_update_axis_isr(servo_axis_t axis)
{
    TIM_TypeDef *timer;

    if (axis >= SERVO_AXIS_COUNT)
    {
        return;
    }
    timer = s_axes[axis].timer->Instance;
    if ((timer->SR & TIM_SR_UIF) != 0U)
    {
        /* 清除更新标志；每次更新事件对应一个完整PWM周期。 */
        timer->SR &= ~TIM_SR_UIF;
        if (s_axes[axis].pulse_counting == 0U)
        {
            return;
        }

        s_axes[axis].pulse_count++;
        if (s_axes[axis].pulse_count >= s_axes[axis].pulse_target)
        {
            /* 中断中只操作定时器寄存器，避免调用HAL和执行阻塞操作。 */
            timer->DIER &= ~TIM_DIER_UIE;
            timer->CCER &= ~TIM_CCER_CC1E;
            __HAL_TIM_SET_COMPARE(s_axes[axis].timer, s_axes[axis].channel, 0U);
            timer->EGR = TIM_EGR_UG;
            timer->SR &= ~TIM_SR_UIF;
            servo_pulse_pin_idle(axis);
            s_axes[axis].frequency_hz = 0U;
            s_axes[axis].pulse_counting = 0U;
            if ((s_axes[0].frequency_hz == 0U) &&
                (s_axes[1].frequency_hz == 0U))
            {
                servo_pulse_gate(false);
            }
        }
    }
}

void servo_pulse_update_tim9_isr(void)
{
    servo_pulse_update_axis_isr(SERVO_AXIS_LEFT);
}

void servo_pulse_update_tim11_isr(void)
{
    servo_pulse_update_axis_isr(SERVO_AXIS_RIGHT);
}
servo_pulse_result_t servo_pulse_set_enable(servo_axis_t axis, bool enable)
{
    if (axis >= SERVO_AXIS_COUNT)
    {
        return SERVO_PULSE_INVALID_ARGUMENT;
    }
    if (!app_config_servo_axis_selected((uint8_t)axis))
    {
        return SERVO_PULSE_NOT_CONFIGURED;
    }
    if (!enable)
    {
        /* 任一轴撤销许可时，共用门禁必须同时切断两轴。*/
        servo_pulse_stop_all();
        s_axes[0].permitted = 0U;
        s_axes[1].permitted = 0U;
        return SERVO_PULSE_OK;
    }
    if (!app_config_motion_hardware_ready())
    {
        return SERVO_PULSE_NOT_CONFIGURED;
    }
    s_axes[axis].permitted = 1U;
    return SERVO_PULSE_OK;
}

servo_pulse_result_t servo_pulse_set_direction(servo_axis_t axis, bool forward)
{
    uint8_t level;

    if (axis >= SERVO_AXIS_COUNT)
    {
        return SERVO_PULSE_INVALID_ARGUMENT;
    }
    if (!app_config_motion_hardware_ready())
    {
        return SERVO_PULSE_NOT_CONFIGURED;
    }
    if (s_axes[axis].frequency_hz != 0U)
    {
        return SERVO_PULSE_HAL_ERROR;
    }
    level = axis == SERVO_AXIS_LEFT ? APP_SERVO_LEFT_FORWARD_LEVEL :
                                     APP_SERVO_RIGHT_FORWARD_LEVEL;
    HAL_GPIO_WritePin(s_axes[axis].direction_port, s_axes[axis].direction_pin,
                      servo_pulse_level(forward ? level : (uint8_t)!level));
    s_axes[axis].forward = forward ? 1U : 0U;
    return SERVO_PULSE_OK;
}

servo_pulse_result_t servo_pulse_set_frequency(servo_axis_t axis,
                                               uint32_t frequency_hz)
{
    if (!APP_UNPROTECTED_MOTION_TEST_ENABLED && frequency_hz != 0U)
        return SERVO_PULSE_NOT_CONFIGURED;

    uint32_t period;
    HAL_StatusTypeDef status;

    if (axis >= SERVO_AXIS_COUNT)
    {
        return SERVO_PULSE_INVALID_ARGUMENT;
    }
    if (!app_config_servo_axis_selected((uint8_t)axis))
    {
        return SERVO_PULSE_NOT_CONFIGURED;
    }
    if (frequency_hz == 0U)
    {
        CLEAR_BIT(s_axes[axis].timer->Instance->DIER, TIM_DIER_UIE);
        CLEAR_BIT(s_axes[axis].timer->Instance->CCER, TIM_CCER_CC1E);
        if (s_axes[axis].frequency_hz != 0U)
        {
            (void)HAL_TIM_PWM_Stop(s_axes[axis].timer, s_axes[axis].channel);
        }
        __HAL_TIM_SET_COMPARE(s_axes[axis].timer, s_axes[axis].channel, 0U);
        /* 停止后重载零占空比，避免下次共用门禁开放时保留高电平。*/
        s_axes[axis].timer->Instance->EGR = TIM_EGR_UG;
        s_axes[axis].timer->Instance->SR &= ~TIM_SR_UIF;
        servo_pulse_pin_idle(axis);
        s_axes[axis].frequency_hz = 0U;
        if ((s_axes[0].frequency_hz == 0U) && (s_axes[1].frequency_hz == 0U))
        {
            servo_pulse_gate(false);
        }
        return SERVO_PULSE_OK;
    }
    /* 到数锁存优先于许可；只有新命令重设目标后才能输出。 */
    if (servo_pulse_target_reached(axis))
    {
        return SERVO_PULSE_HAL_ERROR;
    }
    if (!app_config_motion_hardware_ready() || !s_axes[axis].permitted)
    {
        return SERVO_PULSE_NOT_CONFIGURED;
    }
    if ((frequency_hz < APP_PULSE_MIN_HZ) ||
        (frequency_hz > APP_PULSE_MAX_HZ))
    {
        return SERVO_PULSE_INVALID_ARGUMENT;
    }
    /* 500kHz统一时基，低频时周期计数仍落在16位ARR范围内。*/
    period = (APP_PULSE_TIMER_TICK_HZ + frequency_hz / 2U) / frequency_hz;
    if (period < 2U)
    {
        period = 2U;
    }
    if (s_axes[axis].frequency_hz == frequency_hz)
    {
        return SERVO_PULSE_OK;
    }
    /* 暂禁更新事件，保证ARR和CCR在同一次自然溢出时一起生效。*/
    SET_BIT(s_axes[axis].timer->Instance->CR1, TIM_CR1_UDIS);
    __HAL_TIM_SET_AUTORELOAD(s_axes[axis].timer, period - 1U);
    __HAL_TIM_SET_COMPARE(s_axes[axis].timer, s_axes[axis].channel, period / 2U);
    CLEAR_BIT(s_axes[axis].timer->Instance->CR1, TIM_CR1_UDIS);
    if (s_axes[axis].frequency_hz == 0U)
    {
        s_axes[axis].timer->Instance->EGR = TIM_EGR_UG;
        /* 软件更新事件会重新置位UIF，必须在打开更新中断前再次清除。 */
        s_axes[axis].timer->Instance->SR &= ~TIM_SR_UIF;
        __HAL_TIM_SET_COUNTER(s_axes[axis].timer, 0U);
        servo_pulse_pin_pwm(axis);
        status = HAL_TIM_PWM_Start(s_axes[axis].timer, s_axes[axis].channel);
        if (status != HAL_OK)
        {
            servo_pulse_stop_all();
            return SERVO_PULSE_HAL_ERROR;
        }
        if (s_axes[axis].pulse_counting != 0U)
        {
            SET_BIT(s_axes[axis].timer->Instance->DIER, TIM_DIER_UIE);
        }
    }
    s_axes[axis].frequency_hz = frequency_hz;
    servo_pulse_gate(true);
    return SERVO_PULSE_OK;
}

void servo_pulse_stop_all(void)
{
    uint8_t axis;

    servo_pulse_gate(false); // AM26C31门控关闭
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (s_axes[axis].timer != NULL)
        {
            CLEAR_BIT(s_axes[axis].timer->Instance->DIER, TIM_DIER_UIE);
            (void)HAL_TIM_PWM_Stop(s_axes[axis].timer, s_axes[axis].channel);
            __HAL_TIM_SET_COMPARE(s_axes[axis].timer, s_axes[axis].channel, 0U);
            /*
             * 触发软件更新事件，使CNT清零并立即装载PSC、ARR等配置。
             */
            s_axes[axis].timer->Instance->EGR = TIM_EGR_UG;
            s_axes[axis].timer->Instance->SR &= ~TIM_SR_UIF;
            servo_pulse_pin_idle((servo_axis_t)axis);
        }
        s_axes[axis].frequency_hz = 0U;
        s_axes[axis].pulse_fraction = 0U;
        s_axes[axis].pulse_count = 0U;
        s_axes[axis].pulse_target = 0U;
        s_axes[axis].pulse_counting = 0U;
    }

    /* 停止只关闭PULSE和公共门控，DIR保留停止前的状态。 */
}

uint32_t servo_pulse_get_frequency(servo_axis_t axis)
{
    return axis < SERVO_AXIS_COUNT ? s_axes[axis].frequency_hz : 0U;
}

int64_t servo_pulse_get_command_position(servo_axis_t axis)
{
    return axis < SERVO_AXIS_COUNT ? s_axes[axis].command_position : 0;
}

void servo_pulse_accumulate(uint32_t period_ms)
{
    uint8_t axis;
    uint32_t scaled;
    uint32_t pulses;

    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        scaled = s_axes[axis].frequency_hz * period_ms +
                 s_axes[axis].pulse_fraction;
        pulses = scaled / 1000U;
        s_axes[axis].pulse_fraction = scaled % 1000U;
        if (s_axes[axis].forward != 0U)
        {
            s_axes[axis].command_position += pulses;
        }
        else
        {
            s_axes[axis].command_position -= pulses;
        }
    }
}

#else
#include "servo_pulse.h"
#include "main.h"
/* No HAL handle or mutable pulse state in the RS485-only build. This remains
 * callable from fault/stack-overflow handlers without the RTOS or a timer. */
void servo_pulse_stop_all(void)
{
    GPIOC->BSRR = ((uint32_t)GPIO_PIN_8 << 16U) | GPIO_PIN_9;
    GPIOE->BSRR = (uint32_t)GPIO_PIN_5 << 16U;
    GPIOB->BSRR = (uint32_t)GPIO_PIN_9 << 16U;
    GPIOE->OTYPER &= ~(uint32_t)GPIO_PIN_5;
    GPIOB->OTYPER &= ~(uint32_t)GPIO_PIN_9;
    GPIOE->PUPDR &= ~(3UL << 10U);
    GPIOB->PUPDR &= ~(3UL << 18U);
    GPIOE->MODER = (GPIOE->MODER & ~(3UL << 10U)) | (1UL << 10U);
    GPIOB->MODER = (GPIOB->MODER & ~(3UL << 18U)) | (1UL << 18U);
}
void servo_pulse_init(void) { servo_pulse_stop_all(); }
servo_pulse_result_t servo_pulse_set_enable(servo_axis_t axis, bool enable)
{
    if (axis >= SERVO_AXIS_COUNT) return SERVO_PULSE_INVALID_ARGUMENT;
    servo_pulse_stop_all();
    return enable ? SERVO_PULSE_NOT_CONFIGURED : SERVO_PULSE_OK;
}
servo_pulse_result_t servo_pulse_set_direction(servo_axis_t axis, bool forward)
{ (void)forward; return axis < SERVO_AXIS_COUNT ? SERVO_PULSE_NOT_CONFIGURED : SERVO_PULSE_INVALID_ARGUMENT; }
servo_pulse_result_t servo_pulse_set_frequency(servo_axis_t axis, uint32_t hz)
{
    if (axis >= SERVO_AXIS_COUNT) return SERVO_PULSE_INVALID_ARGUMENT;
    if (hz) return SERVO_PULSE_NOT_CONFIGURED;
    servo_pulse_stop_all(); return SERVO_PULSE_OK;
}
void servo_pulse_complete_axis(servo_axis_t axis) { (void)axis;servo_pulse_stop_all(); }
uint8_t servo_pulse_output_active(servo_axis_t axis) { (void)axis;return 0U; }
uint32_t servo_pulse_get_frequency(servo_axis_t axis) { (void)axis;return 0U; }
int64_t servo_pulse_get_command_position(servo_axis_t axis) { (void)axis;return 0; }
void servo_pulse_accumulate(uint32_t ms) { (void)ms; }
void servo_pulse_set_target(servo_axis_t axis,uint32_t n) { (void)axis;(void)n; }
uint32_t servo_pulse_get_pulse_count(servo_axis_t axis) { (void)axis;return 0U; }
uint32_t servo_pulse_get_target(servo_axis_t axis) { (void)axis;return 0U; }
bool servo_pulse_target_reached(servo_axis_t axis) { (void)axis;return false; }
void servo_pulse_update_tim9_isr(void) {}
void servo_pulse_update_tim11_isr(void) {}

#endif
