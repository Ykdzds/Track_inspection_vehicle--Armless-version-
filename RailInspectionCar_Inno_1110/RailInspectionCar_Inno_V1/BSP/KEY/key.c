#include "key.h"

#include "queue.h"
#include "main.h"

#define KEY_DEBOUNCE_TICKS     (4U)
#define KEY_LONG_TICKS         (200U)
#define KEY_DOUBLE_GAP_TICKS   (60U)
#define KEY_EVENT_QUEUE_SIZE   (12U)

/* 按键状态结构体，用于记录按键消抖、长按和双击检测状态。 */
typedef struct
{
    /* 消抖后的稳定按下状态：
     * 0表示按键当前稳定释放；
     * 1表示按键当前稳定按下。
     */
    uint8_t stable_pressed;

    /* 按键采样计数，用于判断按键电平是否持续稳定，
     * 达到消抖阈值后才更新stable_pressed。
     */
    uint8_t sample_count;

    /* 按键持续按下的时间计数，通常单位为按键任务周期或系统Tick。
     * 用于判断是否达到长按时间。
     */
    uint16_t hold_ticks;

    /* 两次按键之间的时间计数。
     * 用于判断第二次按下是否发生在双击有效时间窗口内。
     */
    uint16_t double_ticks;

    /* 是否正在等待第二次按键：
     * 0表示当前没有等待第二次按键；
     * 1表示第一次按键已经发生，正在等待第二次按键。
     */
    uint8_t waiting_second;

    /* 长按事件是否已经上报：
     * 0表示本次按下尚未上报长按事件；
     * 1表示长按事件已经上报，避免持续按住时重复上报。
     */
    uint8_t long_reported;

} key_state_t;

static key_state_t s_keys[KEY_ID_COUNT];
static QueueHandle_t s_event_queue;

static uint8_t key_read_pressed(key_id_t key)
{
    if (key == KEY_ID_1)
    {
        return HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET;
    }
    if (key == KEY_ID_2)
    {
        return HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_10) == GPIO_PIN_RESET;
    }
    return HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_11) == GPIO_PIN_RESET;
}


static void key_emit(key_id_t key, key_event_type_t type)
{
    key_event_t event;

    if (s_event_queue == NULL)
    {
        return;
    }
    event.key = key;
    event.type = type;
    event.tick = xTaskGetTickCount();
    (void)xQueueSend(s_event_queue, &event, 0U);
}

void key_init(void)
{
    uint8_t index;

    for (index = 0U; index < KEY_ID_COUNT; index++)
    {
        s_keys[index].stable_pressed = 0U;
        s_keys[index].sample_count = 0U;
        s_keys[index].hold_ticks = 0U;
        s_keys[index].double_ticks = 0U;
        s_keys[index].waiting_second = 0U;
        s_keys[index].long_reported = 0U;
    }
    s_event_queue = xQueueCreate(KEY_EVENT_QUEUE_SIZE, sizeof(key_event_t));
}

void key_scan_5ms(void)
{
    uint8_t index;
    uint8_t pressed;
    key_state_t *state;

    for (index = 0U; index < KEY_ID_COUNT; index++)
    {
        state = &s_keys[index];
        pressed = key_read_pressed((key_id_t)index);
        if (pressed != state->stable_pressed)
        {
            state->sample_count++;
            if (state->sample_count >= KEY_DEBOUNCE_TICKS)
            {
                state->sample_count = 0U;
                state->stable_pressed = pressed;
                if (pressed != 0U)
                {
                    state->hold_ticks = 0U;
                    state->long_reported = 0U;
                }
                else if (state->long_reported == 0U)
                {
                    if (state->waiting_second != 0U)
                    {
                        key_emit((key_id_t)index, KEY_EVENT_DOUBLE);
                        state->waiting_second = 0U;
                        state->double_ticks = 0U;
                    }
                    else
                    {
                        state->waiting_second = 1U;
                        state->double_ticks = 0U;
                    }
                }
            }
        }
        else
        {
            state->sample_count = 0U;
        }
        if (state->stable_pressed != 0U)
        {
            if (state->hold_ticks < KEY_LONG_TICKS)
            {
                state->hold_ticks++;
            }
            if ((state->hold_ticks >= KEY_LONG_TICKS) &&
                (state->long_reported == 0U))
            {
                key_emit((key_id_t)index, KEY_EVENT_LONG);
                state->long_reported = 1U;
                state->waiting_second = 0U;
            }
        }
        else if (state->waiting_second != 0U)
        {
            state->double_ticks++;
            if (state->double_ticks >= KEY_DOUBLE_GAP_TICKS)
            {
                key_emit((key_id_t)index, KEY_EVENT_SHORT);
                state->waiting_second = 0U;
                state->double_ticks = 0U;
            }
        }
    }
}

bool key_get_event(key_event_t *event, TickType_t wait_ticks)
{
    if ((event == NULL) || (s_event_queue == NULL))
    {
        return false;
    }
    return xQueueReceive(s_event_queue, event, wait_ticks) == pdTRUE;
}
