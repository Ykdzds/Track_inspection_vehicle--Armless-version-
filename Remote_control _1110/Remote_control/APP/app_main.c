#include "app_main.h"
#include "NRF24L01.h"
#include "remote_display.h"
#include "OLED_Text.h"
#include "remote_diagnostic.h"
#include "key.h"
#include "remote_arm3.h"

#define REMOTE_FRAME_SIZE             32U
#define REMOTE_SEND_PERIOD_MS         10U
#define REMOTE_DISPLAY_PERIOD_MS      100U
#define REMOTE_KEY_MASK               0x0FFFU
#define REMOTE_KEY_DISPLAY_HOLD_COUNT 5U

#define RADIO_DISPLAY_IDLE            0U
#define RADIO_DISPLAY_BUSY            1U
#define RADIO_DISPLAY_OK              2U
#define RADIO_DISPLAY_MAX_RT          3U
#define RADIO_DISPLAY_ERROR           4U

uint16_t adc_data[4];

static volatile uint8_t radio_send_due = 0U;
static volatile uint8_t display_due = 0U;
static uint16_t filtered_adc[4];
static uint16_t joystick_value[4];
static uint8_t filter_initialized = 0U;
static uint8_t frame_sequence = 0U;
static uint16_t event_sequence = 0U;
static uint16_t pending_short_events = 0U;
static uint16_t pending_long_events = 0U;
static uint16_t pending_double_events = 0U;
static uint16_t active_short_events = 0U;
static uint16_t active_long_events = 0U;
static uint16_t active_double_events = 0U;
static uint8_t radio_tx_active = 0U;
static uint16_t pending_k9_clicks, last_k9_clicks;
static uint8_t speed_keys_held;
static uint8_t arm_legacy_stop_pending, arm_last_k5_held;
static uint16_t arm_legacy_stop_sequence;
static uint16_t speed_key_steps[2];
static uint8_t display_short_hold[12];
static uint8_t display_long_hold[12];
static uint8_t display_double_hold[12];
static uint8_t radio_display_status = RADIO_DISPLAY_IDLE;
static uint8_t radio_last_result = RADIO_DISPLAY_IDLE;
static remote_status_model_t status_model;
static remote_display_t display_view;
static remote_debug_view_t debug_view;

static uint16_t remote_crc16(const uint8_t *data, uint8_t length);
static void remote_collect_key_events(void);
static void remote_update_joystick(void);
static void remote_build_frame(void);
static void remote_hold_key_events(uint16_t events, uint8_t *hold_count);
static void remote_format_key_events(const uint8_t *hold_count, char *text);
static void remote_decrease_key_hold(void);
static const char *remote_get_radio_status_text(void);

void app_init(void)
{
    OLED_Init();
    OLED_Clear();
    remote_status_init(&status_model);
    remote_diagnostic_init();
    remote_display_init(&display_view);
    remote_arm3_init();
    NRF24L01_Init();

    HAL_TIM_Base_Start_IT(&htim1);
    HAL_TIM_Base_Start_IT(&htim2);

    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&adc_data, 4U);

    HAL_IWDG_Refresh(&hiwdg);
}

void main_loop_task(void)
{
    remote_radio_task();
    state_display();
    remote_radio_task();
    iwdg_feeding();
    remote_diagnostic_service(&status_model, HAL_GetTick(), display_view.diagnostic_title);
}

volatile uint16_t iwdg_feed_ms = 0U;

void iwdg_feeding(void)
{
    if (iwdg_feed_ms >= 500U)
    {
        iwdg_feed_ms = 0U;
        HAL_IWDG_Refresh(&hiwdg);
    }
}

void state_display(void)
{
    static uint8_t update_page = 8U;
    uint8_t row;
    uint8_t built = 0U;
    uint8_t was_oled_ready;
    uint8_t recovered;

    /* Let an in-flight ACK finish before bounded but blocking OLED work.
     * This also keeps a timeout from delaying the radio's settling stage. */
    if (radio_tx_active != 0U) {
        remote_radio_task();
        if (radio_tx_active != 0U) {
            if (display_due != 0U && OLED_IsReady() == 0U) {
                display_due = 0U;
                remote_decrease_key_hold();
            }
            return;
        }
    }
    was_oled_ready = OLED_IsReady();
    OLED_Service();
    recovered = (uint8_t)(was_oled_ready == 0U && OLED_IsReady() != 0U);
    if (was_oled_ready == 0U)
    {
        /* Discard the failed snapshot; render current state after recovery. */
        update_page = 8U;
    }
    if (display_due != 0U || recovered != 0U) {
        if (update_page >= 8U && OLED_IsReady() != 0U) {
            memcpy(debug_view.joystick, joystick_value, sizeof(joystick_value));
            debug_view.frame_sequence = frame_sequence;
            debug_view.event_sequence = event_sequence;
            debug_view.radio = remote_get_radio_status_text();
            remote_format_key_events(display_short_hold, debug_view.short_keys);
            remote_format_key_events(display_long_hold, debug_view.long_keys);
            remote_format_key_events(display_double_hold, debug_view.double_keys);
            remote_display_build(&display_view, &status_model, &debug_view, HAL_GetTick(),
                                  NRF24L01_IsInitialized(), NRF24L01_HasAckPayload());
            remote_arm3_display(&display_view, HAL_GetTick());
            OLED_Clear();
            for (row = 0U; row < display_view.item_count; row++) {
                const remote_display_item_t *item = &display_view.items[row];
                OLED_ShowUTF8(item->x, item->y, item->text,
                    (uint16_t)strlen(item->text), item->font);
                /* Eight text rows need intermediate radio settling/ACK service.
                 * The display snapshot already owns all rendered values. */
                if ((row & 1U) != 0U) remote_radio_task();
            }
            for (row = 0U; row < display_view.region_count; row++) {
                const remote_display_region_t *area = &display_view.regions[row];
                if (area->underline) {
                    uint8_t offset;
                    for (offset = 0U; offset < area->width; offset += 4U) {
                        OLED_DrawPoint((int16_t)(area->x + offset), area->y);
                        if (offset + 1U < area->width)
                            OLED_DrawPoint((int16_t)(area->x + offset + 1U), area->y);
                    }
                } else OLED_ReverseArea(area->x, area->y, area->width, area->height);
            }
            built = 1U;
            update_page = 0U;
        }
        if (display_due != 0U) {
            display_due = 0U;
            /* 即使OLED离线，按键提示仍按全局100ms节拍衰减。 */
            remote_decrease_key_hold();
        }
    }

    /* Recovery initialization and page transfer get separate radio service windows. */
    if (built == 0U && recovered == 0U && radio_tx_active == 0U &&
        update_page < 8U && OLED_IsReady() != 0U)
    {
        OLED_UpdateArea(0, (int16_t)(update_page * 8U), 128U, 8U);
        /* A failed page is discarded; recovery starts again at page zero. */
        if (OLED_IsReady() != 0U)
        {
            update_page++;
        }
        else
        {
            update_page = 8U;
        }
    }
}

static uint16_t remote_crc16(const uint8_t *data, uint8_t length)
{
    uint16_t crc;
    uint8_t i;
    uint8_t bit;

    crc = 0xFFFFU;
    for (i = 0U; i < length; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static void remote_collect_key_events(void)
{
    uint16_t short_events;
    uint16_t long_events;
    uint16_t double_events;
    uint16_t k9_clicks, k9_delta;

    key_get_and_clear_events(&short_events, &long_events, &double_events);
    short_events &= REMOTE_KEY_MASK;
    long_events &= REMOTE_KEY_MASK;
    double_events &= REMOTE_KEY_MASK;

    remote_hold_key_events(short_events, display_short_hold);
    remote_hold_key_events(long_events, display_long_hold);
    remote_hold_key_events(double_events, display_double_hold);
    key_get_speed_input(&speed_keys_held, speed_key_steps, &k9_clicks);
    k9_delta = (uint16_t)(k9_clicks - last_k9_clicks);
    last_k9_clicks = k9_clicks;
    remote_arm3_keys(short_events, long_events, double_events,
                     key_get_held_mask(), k9_delta, HAL_GetTick());
    remote_display_keys(&display_view, short_events, &short_events,
                         &long_events, &double_events);
    if (remote_arm3_active()) {
        uint8_t k5 = (key_get_held_mask() & REMOTE_STOP_AUTH_KEY_MASK) != 0U;
        if (k5 && !arm_last_k5_held) {
            arm_legacy_stop_pending=1U;
            arm_legacy_stop_sequence=++event_sequence;
        }
        arm_last_k5_held=k5;
        /* ARM inputs never reach the legacy chassis or K6 watchdog path. */
        pending_short_events = pending_long_events = pending_double_events = 0U;
        active_short_events = active_long_events = active_double_events = 0U;
        pending_k9_clicks = 0U;
        return;
    }
    if ((uint32_t)k9_delta + pending_k9_clicks > 65535UL) pending_k9_clicks = 65535U;
    else pending_k9_clicks = (uint16_t)(pending_k9_clicks + k9_delta);
    short_events &= (uint16_t)~0x0100U;
    pending_short_events |= short_events;
    pending_long_events |= long_events;
    pending_double_events |= double_events;
}

static void remote_hold_key_events(uint16_t events, uint8_t *hold_count)
{
    uint8_t i;

    for (i = 0U; i < 12U; i++)
    {
        if ((events & (uint16_t)(1U << i)) != 0U)
        {
            hold_count[i] = REMOTE_KEY_DISPLAY_HOLD_COUNT;
        }
    }
}

static void remote_format_key_events(const uint8_t *hold_count, char *text)
{
    uint8_t i;

    for (i = 0U; i < 12U; i++)
    {
        text[i] = (hold_count[i] != 0U) ? '1' : '0';
    }
    text[12] = '\0';
}

static void remote_decrease_key_hold(void)
{
    uint8_t i;

    for (i = 0U; i < 12U; i++)
    {
        if (display_short_hold[i] != 0U)
        {
            display_short_hold[i]--;
        }
        if (display_long_hold[i] != 0U)
        {
            display_long_hold[i]--;
        }
        if (display_double_hold[i] != 0U)
        {
            display_double_hold[i]--;
        }
    }
}

static const char *remote_get_radio_status_text(void)
{
    const char *status_text;

    uint8_t display_status = radio_last_result == RADIO_DISPLAY_IDLE ? radio_display_status : radio_last_result;
    status_text = "ID";
    if (display_status == RADIO_DISPLAY_BUSY)
    {
        status_text = "BY";
    }
    else if (display_status == RADIO_DISPLAY_OK)
    {
        status_text = "OK";
    }
    else if (display_status == RADIO_DISPLAY_MAX_RT)
    {
        status_text = "MX";
    }
    else if (display_status == RADIO_DISPLAY_ERROR)
    {
        status_text = "ER";
    }

    return status_text;
}

static void remote_update_joystick(void)
{
    uint32_t mapped_value;
    int32_t difference;
    uint16_t raw_value;
    uint8_t i;

    for (i = 0U; i < 4U; i++)
    {
        raw_value = adc_data[i];
        if (raw_value > 4095U)
        {
            raw_value = 4095U;
        }

        if (filter_initialized == 0U)
        {
            filtered_adc[i] = raw_value;
        }
        else
        {
            difference = (int32_t)raw_value - (int32_t)filtered_adc[i];
            filtered_adc[i] = (uint16_t)((int32_t)filtered_adc[i] +
                                         (difference / 5));
        }

        mapped_value = ((uint32_t)filtered_adc[i] * 2000U + 2047U) / 4095U;
        if (mapped_value > 2000U)
        {
            mapped_value = 2000U;
        }
        joystick_value[i] = (uint16_t)mapped_value;
    }
    filter_initialized = 1U;
}

static void remote_build_frame(void)
{
    uint16_t crc;
    uint8_t i;

    memset(NRF24L01_TxPacket, 0, REMOTE_FRAME_SIZE);
    NRF24L01_TxPacket[0] = 0xA5U;
    NRF24L01_TxPacket[1] = 0x5AU;
    NRF24L01_TxPacket[2] = 0x01U;
    NRF24L01_TxPacket[3] = 0x01U;
    NRF24L01_TxPacket[4] = frame_sequence;
    NRF24L01_TxPacket[5] = 17U;

    for (i = 0U; i < 4U; i++)
    {
        NRF24L01_TxPacket[6U + i * 2U] =
            (uint8_t)(joystick_value[i] >> 8);
        NRF24L01_TxPacket[7U + i * 2U] =
            (uint8_t)(joystick_value[i] & 0xFFU);
    }

    NRF24L01_TxPacket[14] = (uint8_t)(active_short_events & 0xFFU);
    NRF24L01_TxPacket[15] = (uint8_t)((active_short_events >> 8) & 0x0FU);
    NRF24L01_TxPacket[16] = (uint8_t)(active_long_events & 0xFFU);
    NRF24L01_TxPacket[17] = (uint8_t)((active_long_events >> 8) & 0x0FU);
    NRF24L01_TxPacket[18] = (uint8_t)(active_double_events & 0xFFU);
    NRF24L01_TxPacket[19] = (uint8_t)((active_double_events >> 8) & 0x0FU);
    NRF24L01_TxPacket[20] = (uint8_t)(event_sequence >> 8);
    NRF24L01_TxPacket[21] = (uint8_t)(event_sequence & 0xFFU);
    if ((active_short_events | active_long_events |
         active_double_events) != 0U)
    {
        NRF24L01_TxPacket[22] = 0x01U;
    }

    NRF24L01_TxPacket[23] = (uint8_t)(0xA0U | speed_keys_held);
    NRF24L01_TxPacket[24] = (uint8_t)(speed_key_steps[0] >> 8);
    NRF24L01_TxPacket[25] = (uint8_t)speed_key_steps[0];
    NRF24L01_TxPacket[26] = (uint8_t)(speed_key_steps[1] >> 8);
    NRF24L01_TxPacket[27] = (uint8_t)speed_key_steps[1];
    crc = remote_crc16(NRF24L01_TxPacket, 28U);
    NRF24L01_TxPacket[28] = (uint8_t)(crc >> 8);
    NRF24L01_TxPacket[29] = (uint8_t)(crc & 0xFFU);
    NRF24L01_TxPacket[30] = 0x0DU;
    NRF24L01_TxPacket[31] = 0x0AU;
}

void remote_radio_task(void)
{
    NRF24L01_TxResult tx_result;
    uint8_t ack_data[32], ack_length;

    remote_collect_key_events();
    NRF24L01_Service();
    remote_status_tick(&status_model, HAL_GetTick());

    if (radio_tx_active != 0U)
    {
        tx_result = NRF24L01_PollSend();
        if (tx_result == NRF24L01_TX_SUCCESS)
        {
            if (NRF24L01_TxPacket[2]==1U &&
                (NRF24L01_TxPacket[14]&REMOTE_STOP_AUTH_KEY_MASK))
                arm_legacy_stop_pending=0U;
            radio_tx_active = 0U;
            radio_display_status = radio_last_result = RADIO_DISPLAY_OK;
            active_short_events = 0U;
            active_long_events = 0U;
            active_double_events = 0U;
        }
        else if (tx_result == NRF24L01_TX_MAX_RT)
        {
            radio_tx_active = 0U;
            radio_display_status = radio_last_result = RADIO_DISPLAY_MAX_RT;
        }
        else if ((tx_result == NRF24L01_TX_ERROR) ||
                 (tx_result == NRF24L01_TX_IDLE))
        {
            radio_tx_active = 0U;
            radio_display_status = radio_last_result = RADIO_DISPLAY_ERROR;
        }
    }

    while (NRF24L01_PopAckPayload(ack_data, &ack_length))
    {
        uint32_t now = HAL_GetTick();
        remote_status_note_payload(&status_model, now);
        if (!remote_arm3_ack(ack_data, ack_length, now))
            (void)remote_status_accept(&status_model, ack_data, ack_length, now);
    }

    if (radio_send_due == 0U)
    {
        return;
    }
    radio_send_due = 0U;

    if (radio_tx_active != 0U)
    {
        NRF24L01_AbortSend();
        radio_tx_active = 0U;
        radio_display_status = radio_last_result = RADIO_DISPLAY_ERROR;
    }

    if ((pending_short_events & REMOTE_STOP_AUTH_KEY_MASK) != 0U)
    {
        /* 只在当前发送结束后抢占；旧K5长按不得在停车事件之后重放。 */
        if (active_short_events & 0x0100U) {
            if (pending_k9_clicks < 65535U) pending_k9_clicks++;
        }
        pending_short_events |= (uint16_t)(active_short_events &
            (uint16_t)~(REMOTE_STOP_AUTH_KEY_MASK | 0x0100U));
        pending_long_events |= (uint16_t)(active_long_events & ~REMOTE_STOP_AUTH_KEY_MASK);
        pending_double_events |= (uint16_t)(active_double_events & ~REMOTE_STOP_AUTH_KEY_MASK);
        active_short_events = REMOTE_STOP_AUTH_KEY_MASK;
        active_long_events = active_double_events = 0U;
        pending_short_events &= (uint16_t)~REMOTE_STOP_AUTH_KEY_MASK;
        pending_long_events &= (uint16_t)~REMOTE_STOP_AUTH_KEY_MASK;
        pending_double_events &= (uint16_t)~REMOTE_STOP_AUTH_KEY_MASK;
        event_sequence++;
    }
    else if (((active_short_events | active_long_events |
          active_double_events) == 0U) &&
        ((pending_short_events | pending_long_events |
          pending_double_events) != 0U || pending_k9_clicks != 0U))
    {
        active_short_events = pending_short_events;
        if (pending_k9_clicks) { active_short_events |= 0x0100U; pending_k9_clicks--; }
        active_long_events = pending_long_events;
        active_double_events = pending_double_events;
        pending_short_events = 0U;
        pending_long_events = 0U;
        pending_double_events = 0U;
        event_sequence++;
    }

    remote_update_joystick();
    frame_sequence++;
    remote_build_frame();
    if (remote_arm3_active()) {
        static uint8_t arm_legacy_divider;
        if (++arm_legacy_divider >= 10U || arm_legacy_stop_pending) {
            uint16_t crc;
            uint8_t i;
            arm_legacy_divider = 0U;
            for (i = 0U; i < 4U; ++i) {
                NRF24L01_TxPacket[6U+i*2U] = 3U;
                NRF24L01_TxPacket[7U+i*2U] = 0xE8U;
            }
            memset(NRF24L01_TxPacket+14U,0,14U);
            /* Independent old K5 path remains available to an old receiver. */
            if (arm_legacy_stop_pending) {
                NRF24L01_TxPacket[14]=REMOTE_STOP_AUTH_KEY_MASK;
                NRF24L01_TxPacket[20]=(uint8_t)(arm_legacy_stop_sequence>>8);
                NRF24L01_TxPacket[21]=(uint8_t)arm_legacy_stop_sequence;
                NRF24L01_TxPacket[22]=1U;
            }
            crc=remote_crc16(NRF24L01_TxPacket,28U);
            NRF24L01_TxPacket[28]=(uint8_t)(crc>>8);NRF24L01_TxPacket[29]=(uint8_t)crc;
        } else (void)remote_arm3_frame(NRF24L01_TxPacket,frame_sequence,HAL_GetTick());
    }
    if (NRF24L01_StartSend(NRF24L01_TxPacket, REMOTE_FRAME_SIZE) != 0U)
    {
        radio_tx_active = 1U;
        radio_display_status = RADIO_DISPLAY_BUSY;
    }
    else
    {
        radio_display_status = radio_last_result = RADIO_DISPLAY_ERROR;
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    static uint8_t radio_period_ms = 0U;
    static uint8_t display_period_ms = 0U;

    if (htim->Instance == TIM1) /* TIM1每1 ms进入一次中断 */
    {
        iwdg_feed_ms++;
        key_scan();
    }
    else if (htim->Instance == TIM2) /* TIM2每1 ms进入一次中断 */
    {
        radio_period_ms++;
        display_period_ms++;
        if (radio_period_ms >= REMOTE_SEND_PERIOD_MS)
        {
            radio_period_ms = 0U;
            radio_send_due = 1U;
        }
        if (display_period_ms >= REMOTE_DISPLAY_PERIOD_MS)
        {
            display_period_ms = 0U;
            display_due = 1U;
        }
    }
}
