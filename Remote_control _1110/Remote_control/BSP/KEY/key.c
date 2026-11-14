#include "key.h" 

KEY keys[12] = {{0}};

uint16_t key_get_held_mask(void)
{
    uint16_t mask = 0U;
    uint8_t i;
    for (i = 0U; i < 12U; ++i) if (keys[i].key_sta == 0U) mask |= (uint16_t)(1U << i);
    return mask;
}

static volatile uint8_t speed_held;
static uint8_t speed_debounce[2], speed_conflict;
static volatile uint16_t speed_steps[2], k9_click_count;
static uint16_t speed_ms[2], speed_repeat_ms[2];
static uint32_t speed_emitted[2];

/* Called at the fixed 10 ms scan cadence, independently of radio/OLED work. */
static void key_speed_scan(void)
{
    uint8_t i, before = speed_held;
    for (i = 0U; i < 2U; ++i) {
        uint8_t down = (uint8_t)(keys[i].key_sta == 0U);
        if (down != ((speed_held >> i) & 1U)) {
            if (++speed_debounce[i] >= 2U) {
                speed_held ^= (uint8_t)(1U << i);
                speed_debounce[i] = 0U;
            }
        } else speed_debounce[i] = 0U;
    }
    if (speed_held == 3U) speed_conflict = 1U;
    if (!speed_held) speed_conflict = 0U;
    for (i = 0U; i < 2U; ++i) {
        uint8_t bit = (uint8_t)(1U << i);
        if (!(speed_held & bit)) {
            if ((before & bit) && speed_ms[i] < 1000U) keys[i].key_flag = 1U;
            speed_ms[i] = speed_repeat_ms[i] = 0U;
            speed_emitted[i] = 0U;
            continue;
        }
        if (!(before & bit)) {
            speed_ms[i] = speed_repeat_ms[i] = 0U;
            speed_emitted[i] = 0U;
        } else if (speed_ms[i] < 10000U) speed_ms[i] += 10U;
        else speed_repeat_ms[i] += 10U;
        if (speed_ms[i] == 1000U && (before & bit)) keys[i].long_flag = 1U;
        if (speed_conflict || keys[i].key_sta != 0U) continue;
        if (speed_ms[i] < 10000U) {
            uint32_t total = speed_ms[i] < 300U ? 1U :
                1U + (uint32_t)(speed_ms[i] - 300U) * 399U / 9700U;
            speed_steps[i] = (uint16_t)(speed_steps[i] + total - speed_emitted[i]);
            speed_emitted[i] = total;
        } else {
            if (speed_emitted[i] < 400U) {
                speed_steps[i] = (uint16_t)(speed_steps[i] + 400U - speed_emitted[i]);
                speed_emitted[i] = 400U;
            }
            speed_steps[i] = (uint16_t)(speed_steps[i] + speed_repeat_ms[i] / 25U);
            speed_repeat_ms[i] %= 25U;
        }
    }
}

void key_get_speed_input(uint8_t *held, uint16_t steps[2], uint16_t *k9_clicks)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *held = speed_held;
    steps[0] = speed_steps[0]; steps[1] = speed_steps[1];
    *k9_clicks = k9_click_count;
    if (!primask) __enable_irq();
}

void key_scan(void)
{
    static uint8_t key_times = 0;
    
    key_times ++;
    if(key_times >= 10)
    {
        key_times = 0;
        
        keys[0].key_sta =  HAL_GPIO_ReadPin(K1_GPIO_Port,K1_Pin);
        keys[1].key_sta =  HAL_GPIO_ReadPin(K2_GPIO_Port,K2_Pin);
        keys[2].key_sta =  HAL_GPIO_ReadPin(K3_GPIO_Port,K3_Pin);
        keys[3].key_sta =  HAL_GPIO_ReadPin(K4_GPIO_Port,K4_Pin);
        keys[4].key_sta =  HAL_GPIO_ReadPin(K5_GPIO_Port,K5_Pin);
        keys[5].key_sta =  HAL_GPIO_ReadPin(K6_GPIO_Port,K6_Pin);
        keys[6].key_sta =  HAL_GPIO_ReadPin(K7_GPIO_Port,K7_Pin);
        keys[7].key_sta =  HAL_GPIO_ReadPin(K8_GPIO_Port,K8_Pin);
        keys[8].key_sta =  HAL_GPIO_ReadPin(K9_GPIO_Port,K9_Pin);
        keys[9].key_sta =  HAL_GPIO_ReadPin(K10_GPIO_Port,K10_Pin);
        keys[10].key_sta = HAL_GPIO_ReadPin(K11_GPIO_Port,K11_Pin);
        keys[11].key_sta = HAL_GPIO_ReadPin(K12_GPIO_Port,K12_Pin);
        
        key_speed_scan();
        for(int i = 2;i < 12;i ++)
        {
            switch(keys[i].key_judge)
            {
                case 0:
                {
                    if(keys[i].key_sta==0)keys[i].key_judge=1;                  
                    keys[i].key_times=0;
                    keys[i].affirm_longflag = 0;
                    // 双击窗口超时检测
                    if(keys[i].double_state == 1)
                    {
                        keys[i].double_times++;
                        if(keys[i].double_times >= DBL_CLICK_TIMEOUT)
                        {
                            keys[i].key_flag = 1;       // 超时确认单击
                            keys[i].double_state = 0;
                            keys[i].double_times = 0;
                        }
                    }
                }
                break;
                case 1:
                {
                    if(keys[i].key_sta==0)keys[i].key_judge=2;
                    else keys[i].key_judge=0;
                }
                break;
                case 2:
                {
                    if(keys[i].key_sta==1)
                    {
                        if(keys[i].key_times <= 100)
                        {
                            /* K5短按立即上报，不等待双击窗口；长按释放不触发。 */
                            if(i == REMOTE_STOP_AUTH_KEY_INDEX || i == 8)
                            {
                                keys[i].key_flag = 1;
                                if (i == 8) k9_click_count++;
                                keys[i].double_state = 0;
                                keys[i].double_times = 0;
                            }
                            else if(keys[i].double_state == 0)
                            {
                                keys[i].double_state = 1;   // 首次短按，进入等??
                                keys[i].double_times = 0;
                            }
                            else
                            {
                                keys[i].double_flag = 1;    // 窗口内第二次短按 ??双击
                                keys[i].double_state = 0;
                                keys[i].double_times = 0;
                            }
                        }
                        keys[i].key_judge=0;
                    }
                    else 
                    {
                        keys[i].key_times++;
                        if(keys[i].key_times >= 100)keys[i].key_times = 102;
                        if(keys[i].key_times >= 100 && keys[i].affirm_longflag == 0)
                        {
                            keys[i].long_flag = 1;
                            keys[i].affirm_longflag = 1;
                            keys[i].double_state = 0;   // 长按触发，取消双击等待
                            keys[i].key_times = 102;
                        }                      
                    }
                }
                break;
            }
        }        
    }    
}

void key_handle(void)
{
    /* 按键事件由无线任务统一提取并在发送成功后确认。 */
}

void key_get_and_clear_events(uint16_t *short_events,
                              uint16_t *long_events,
                              uint16_t *double_events)
{
    uint16_t short_value;
    uint16_t long_value;
    uint16_t double_value;
    uint32_t primask;
    uint8_t i;

    if ((short_events == NULL) || (long_events == NULL) ||
        (double_events == NULL))
    {
        return;
    }

    short_value = 0U;
    long_value = 0U;
    double_value = 0U;
    primask = __get_PRIMASK(); // 1. 保存当前中断状态
    __disable_irq();           // 2. 关闭中断

    for (i = 0U; i < 12U; i++)
    {
        if (keys[i].key_flag != 0U)
        {
            short_value |= (uint16_t)(1U << i);
            keys[i].key_flag = 0U;
        }
        if (keys[i].long_flag != 0U)
        {
            long_value |= (uint16_t)(1U << i);
            keys[i].long_flag = 0U;
        }
        if (keys[i].double_flag != 0U)
        {
            double_value |= (uint16_t)(1U << i);
            keys[i].double_flag = 0U;
        }
    }

    if (primask == 0U) // 3. 恢复之前保存的中断状态
    {
        __enable_irq();
    }

    *short_events = short_value;
    *long_events = long_value;
    *double_events = double_value;
}
