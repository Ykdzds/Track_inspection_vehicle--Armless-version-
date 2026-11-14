#include "app_main.h"
#include "OLED_Text.h"
#include "remote_display.h"
#include "NRF24L01.h"
#include "remote_status_protocol.h"
#include "remote_status_model.h"
#include "remote_diagnostic_config.h"
#include "usart.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

TIM_HandleTypeDef htim1 = {TIM1}, htim2 = {TIM2};
ADC_HandleTypeDef hadc1;
IWDG_HandleTypeDef hiwdg;
uint8_t NRF24L01_TxPacket[32];
extern uint16_t adc_data[4];
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);

static uint32_t ticks;
static uint16_t keys_short, keys_long, keys_double;
static uint16_t speed_steps_mock[2], k9_mock;
static uint8_t held_mock;
uint16_t key_get_held_mask(void) { return held_mock; }
void key_get_speed_input(uint8_t *held, uint16_t steps[2], uint16_t *clicks)
{
    *held = held_mock; steps[0] = speed_steps_mock[0]; steps[1] = speed_steps_mock[1]; *clicks = k9_mock;
}
static NRF24L01_TxResult tx_result;
static uint8_t ack[32], ack_pending, tx_active;
static uint8_t last_control[32];
static unsigned sends, aborts, watchdogs, page_writes;
static char buffer[8][180], panel[8][180];
static uint8_t next_oled_page;
static uint8_t oled_ready = 1U, oled_fail_next, oled_recover;
UART_HandleTypeDef huart1;
static unsigned uart_starts;
static uint8_t *uart_data;
static uint16_t uart_length;
static char uart_copy[1536];

void NRF24L01_GetStats(NRF24L01_Stats *stats)
{ memset(stats, 0, sizeof(*stats)); }

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t length)
{
    assert(uart == &huart1 && length < sizeof(uart_copy));
    assert(huart1.gState == HAL_UART_STATE_READY);
    memcpy(uart_copy, data, length);
    uart_data = data; uart_length = length;
    uart_starts++;
    huart1.gState = HAL_UART_STATE_BUSY_TX;
    return HAL_OK;
}

uint32_t HAL_GetTick(void) { return ticks; }
void HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *timer) { (void)timer; }
void HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc, uint32_t *data, uint32_t length)
{ (void)adc; (void)data; assert(length == 4U); }
void HAL_IWDG_Refresh(IWDG_HandleTypeDef *watchdog) { (void)watchdog; watchdogs++; }
void key_scan(void) {}
void key_get_and_clear_events(uint16_t *s, uint16_t *l, uint16_t *d)
{
    *s = keys_short; *l = keys_long; *d = keys_double;
    keys_short = keys_long = keys_double = 0U;
}
void OLED_Init(void) {}
void OLED_Service(void) { if (oled_recover) { oled_ready = 1U; oled_recover = 0U; } }
uint8_t OLED_IsReady(void) { return oled_ready; }
void OLED_Clear(void) { memset(buffer, 0, sizeof(buffer)); }
void OLED_ShowUTF8(int16_t x, int16_t y, const char *text, uint16_t length, uint8_t font)
{
    assert(x >= 0 && y >= 0 && y <= 56);
    assert(font == OLED_UTF8_12X12 || font == 6U);
    assert(strlen(buffer[y / 8]) + length < sizeof(buffer[0]));
    strncat(buffer[y / 8], text, length);
}
void OLED_DrawPoint(int16_t x, int16_t y) { assert(x >= 0 && x < 128 && y >= 0 && y < 64); }
void OLED_ReverseArea(int16_t x, int16_t y, uint8_t width, uint8_t height)
{ assert(x >= 0 && x + width <= 128 && y >= 0 && y + height <= 64); }
void OLED_UpdateArea(int16_t x, int16_t y, uint8_t width, uint8_t height)
{
    assert(x == 0 && width == 128U && height == 8U);
    assert(y == (int16_t)(next_oled_page * 8U));
    assert(oled_ready);
    if (oled_fail_next) {
        oled_ready = oled_fail_next = 0U;
        next_oled_page = 0U;
        return;
    }
    strcpy(panel[next_oled_page], buffer[next_oled_page]);
    next_oled_page = (uint8_t)((next_oled_page + 1U) % 8U);
    page_writes++;
}
void NRF24L01_Init(void) {}
void NRF24L01_Service(void) {}
uint8_t NRF24L01_IsInitialized(void) { return 1U; }
uint8_t NRF24L01_HasAckPayload(void) { return 1U; }
uint8_t NRF24L01_StartSend(uint8_t *data, uint8_t length)
{
    assert(length == 32U && !tx_active);
    assert(data[0] == 0xA5U && data[1] == 0x5AU && data[5] == 17U);
    assert(remote_status_crc16(data, 28U) == (uint16_t)((uint16_t)data[28] << 8U | data[29]));
    assert(!(data[14] & 0x00C0U) && !(data[16] & 0x00C0U) && !(data[18] & 0x00C0U));
    assert(data[23] == (uint8_t)(0xA0U | held_mock));
    assert(((uint16_t)data[24] << 8U | data[25]) == speed_steps_mock[0]);
    assert(((uint16_t)data[26] << 8U | data[27]) == speed_steps_mock[1]);
    memcpy(last_control, data, 32U); sends++; tx_active = 1U;
    return 1U;
}
NRF24L01_TxResult NRF24L01_PollSend(void)
{
    if (!tx_active) return NRF24L01_TX_IDLE;
    if (tx_result != NRF24L01_TX_BUSY) tx_active = 0U;
    return tx_result;
}
void NRF24L01_AbortSend(void) { aborts++; tx_active = 0U; }
uint8_t NRF24L01_PopAckPayload(uint8_t *data, uint8_t *length)
{
    if (!ack_pending) return 0U;
    memcpy(data, ack, 32U); *length = 32U; ack_pending = 0U;
    return 1U;
}

static void advance(uint32_t ms)
{
    while (ms--) {
        ticks++;
        HAL_TIM_PeriodElapsedCallback(&htim1);
        HAL_TIM_PeriodElapsedCallback(&htim2);
    }
}

static void send_tick(void)
{
    advance(10U); remote_radio_task();
}

static void complete_display(void)
{
    unsigned i;
    for (i = 0U; i < 10U; i++) state_display();
}

static uint16_t event_seq(void)
{
    return (uint16_t)((uint16_t)last_control[20] << 8U | last_control[21]);
}

static void status(uint8_t sequence, uint8_t session, uint8_t authorization)
{
    uint16_t crc;
    memset(ack, 0, sizeof(ack));
    ack[0] = 0x5AU; ack[1] = 0xA5U; ack[2] = 1U; ack[3] = 0x81U;
    ack[4] = sequence; ack[5] = 22U; ack[6] = session; ack[7] = 7U; ack[8] = 3U;
    ack[12] = 7U; ack[13] = 4U; ack[14] = authorization;
    ack[30] = 13U; ack[31] = 10U;
    crc = remote_status_crc16(ack, 28U);
    ack[28] = (uint8_t)(crc >> 8U); ack[29] = (uint8_t)crc;
    ack_pending = 1U;
}

int main(void)
{
    unsigned previous_sends;
    uint16_t sequence;
    app_init();
    for (unsigned i = 0U; i < 4U; i++) adc_data[i] = 2048U;
    tx_result = NRF24L01_TX_SUCCESS;
    advance(100U);complete_display();
    assert(strstr(panel[0],"1/9") && strstr(panel[2],"T:"));
    page_writes=0U;
    keys_long=0x0040U;remote_radio_task(); /* switch to Chinese HOME */
    keys_short = 0x0040U; keys_long = keys_double = 0x0080U;
    send_tick();
    assert(event_seq() == 0U && last_control[22] == 0U);
    keys_short = 0x0040U;
    send_tick();
    keys_short = 1U;
    send_tick(); sequence = event_seq();
    assert(sequence == 1U && last_control[14] == 1U);
    tx_result = NRF24L01_TX_MAX_RT;
    keys_short = 32U;
    send_tick();
    assert(event_seq() == sequence && last_control[14] == 1U);
    tx_result = NRF24L01_TX_SUCCESS;
    send_tick();
    assert(event_seq() == sequence + 1U && last_control[14] == 32U);
    send_tick();
    assert(event_seq() == sequence + 1U && last_control[22] == 0U);
    advance(50U); complete_display();
    assert(page_writes == 8U && strstr(panel[0], "13/14"));
    assert(strstr(panel[4], "短061") && strstr(panel[0], "13/14"));
    /* 刷新中翻页不改变当前8个硬件页的帧缓冲。 */
    advance(100U); state_display();
    keys_short = 0x0080U; remote_radio_task();
    assert(strstr(buffer[0], "13/14"));
    keys_short = 0x0080U; remote_radio_task();
    complete_display();
    assert(strstr(panel[0], "13/14"));
    advance(100U); complete_display();
    assert(strstr(panel[0], "01/14"));
    status(1U, 1U, 7U); remote_radio_task();
    advance(100U); complete_display();
    assert(strstr(panel[0], "01/14"));
    remote_radio_task();
    previous_sends = sends;
    status(1U, 2U, 0U); remote_radio_task();
    assert(sends == previous_sends); /* 状态不会主动生成新的控制发送。 */
    advance(100U); complete_display();
    assert(strstr(panel[0], "01/14"));
    keys_short = 0x0040U; remote_radio_task();
    keys_short = 0x0040U; remote_radio_task();
    advance(100U); complete_display();
    assert(strstr(panel[0], "13/14") && strstr(panel[4], "短0C0") != NULL);
    /* 停留其他页期间按键保持提示也会过期。 */
    keys_short = 0x0080U; remote_radio_task();
    for (unsigned i = 0U; i < 6U; i++) { advance(100U); complete_display(); }
    keys_short = 0x0040U; remote_radio_task();
    advance(100U); complete_display();
    assert(strstr(panel[4], "短040"));
    previous_sends = sends;
    for (unsigned i = 0U; i < 10U; i++) { advance(10U); main_loop_task(); }
    assert(sends == previous_sends + 10U && watchdogs >= 2U);
    assert(last_control[22] == 0U && event_seq() == sequence + 1U);
    assert(aborts == 0U);
    /* 普通长按发送尚未完成，K5短按优先，但不修改正在发送的帧。 */
    keys_long = 0x0010U;
    send_tick();
    sequence = event_seq();
    assert(last_control[16] == 0x0010U);
    tx_result = NRF24L01_TX_BUSY;
    keys_short = 0x0010U;
    remote_radio_task();
    assert(event_seq() == sequence && last_control[16] == 0x0010U);
    assert(memcmp(last_control, NRF24L01_TxPacket, 32U) == 0);
    tx_result = NRF24L01_TX_MAX_RT;
    send_tick();
    assert(event_seq() == sequence + 1U && last_control[14] == 0x0010U);
    assert(last_control[16] == 0U && last_control[18] == 0U);
    sequence = event_seq();
    send_tick();
    assert(event_seq() == sequence && last_control[14] == 0x0010U);
    tx_result = NRF24L01_TX_SUCCESS;
    send_tick();
    assert(event_seq() == sequence && last_control[22] == 0U);
    /* 非K5事件被安全延期，K5用独立的新事件序号发送。 */
    keys_short = 32U; send_tick();
    sequence = event_seq();
    tx_result = NRF24L01_TX_MAX_RT;
    keys_short = 0x0010U; send_tick();
    assert(event_seq() == sequence + 1U && last_control[14] == 0x0010U);
    tx_result = NRF24L01_TX_SUCCESS;
    send_tick();
    assert(event_seq() == sequence + 2U && last_control[14] == 32U);
    /* K1至K4保留三类上传，不改变七页翻页状态。 */
    send_tick();
    sequence = event_seq();
    keys_short = keys_long = keys_double = 0x000FU;
    send_tick();
    assert(event_seq() == sequence + 1U);
    assert(last_control[14] == 0x000FU && last_control[16] == 0x000FU && last_control[18] == 0x000FU);
    send_tick();
    sequence = event_seq();
    keys_short = 0x00C0U; keys_long = keys_double = 0x00C0U;
    send_tick();
    assert(event_seq() == sequence && last_control[22] == 0U);
    keys_short = 0x0044U;
    send_tick();
    assert(event_seq() == sequence + 1U && last_control[14] == 4U);
    previous_sends = sends;
    for (unsigned tick = 0U; tick < 400U; tick++) {
        advance(10U);
        main_loop_task();
    }
    assert(sends == previous_sends + 400U);
#if REMOTE_DIAGNOSTIC_ENABLED
    assert(uart_starts == 1U && uart_data != NULL);
    assert(memcmp(uart_data, uart_copy, uart_length) == 0);
    HAL_UART_ErrorCallback(&huart1);
    advance(1000U); main_loop_task();
    assert(uart_starts == 1U);
    huart1.gState = HAL_UART_STATE_READY;
    HAL_UART_TxCpltCallback(&huart1);
    advance(1000U); main_loop_task();
    assert(uart_starts == 2U && strstr(uart_copy, "txdrop=") != NULL);
#else
    assert(uart_starts == 0U);
#endif
    /* K9 repeated clicks queue separately, retries retain event ID; K5 preempts. */
    tx_result = NRF24L01_TX_SUCCESS; send_tick(); send_tick();
    k9_mock += 2U; send_tick(); sequence = event_seq();
    assert(last_control[15] & 1U);
    tx_result = NRF24L01_TX_MAX_RT; send_tick();
    assert(event_seq() == sequence);
    keys_short = 0x0010U; send_tick();
    assert(last_control[14] == 0x10U && !(last_control[15] & 1U));
    tx_result = NRF24L01_TX_SUCCESS; send_tick(); sequence = event_seq();
    assert(last_control[15] & 1U);
    send_tick(); assert(event_seq() == (uint16_t)(sequence + 1U) && (last_control[15] & 1U));
    held_mock = 1U; speed_steps_mock[0] = 65535U; speed_steps_mock[1] = 123U; send_tick();
    assert(last_control[23] == 0xA1U && last_control[24] == 255U && last_control[25] == 255U && last_control[27] == 123U);
    /* A language request cannot overwrite a partial English snapshot. */
    complete_display();advance(100U);state_display();state_display();state_display();
    assert(next_oled_page==2U && strstr(buffer[0],"/9"));
    { char frozen_buffer[sizeof(buffer)];memcpy(frozen_buffer,buffer,sizeof(buffer));
      keys_long=0x0040U;remote_radio_task();
      assert(!memcmp(frozen_buffer,buffer,sizeof(buffer)));
      complete_display();assert(strstr(panel[0],"/9"));
    }
    advance(100U);complete_display();assert(strstr(panel[0],"01/14"));
    /* Discard a partial snapshot on failure; keys expire offline and recovery
       renders current logical page before restarting from hardware page zero. */
    complete_display();
    for (unsigned i = 0U; i < REMOTE_DISPLAY_ZH_PAGE_COUNT && !strstr(panel[0], "13/14"); i++) {
        keys_short = 0x80U; remote_radio_task();
        advance(100U); complete_display();
    }
    advance(100U); state_display(); state_display(); state_display();
    assert(next_oled_page == 2U);
    oled_fail_next = 1U; state_display(); assert(!oled_ready && next_oled_page == 0U);
    previous_sends = page_writes;
    keys_short = 0x20U; remote_radio_task();
    keys_long=0x0040U;remote_radio_task(); /* English HOME requested offline. */
    keys_short=0x0040U;remote_radio_task(); /* English DBG, not Chinese page 14. */
    for (unsigned i = 0U; i < 6U; i++) { advance(100U); state_display(); remote_radio_task(); }
    assert(page_writes == previous_sends);
    oled_recover = 1U; state_display();
    assert(page_writes == previous_sends); /* initialization has its own service window */
    complete_display(); assert(page_writes == previous_sends + 8U);
    assert(strstr(panel[0], "9/9") && strstr(panel[3], "S:000000000000")); /* hints expired offline */
    puts("PASS application: local keys, event retry, cadence, frozen/recovered OLED, offline hint decay, session, UART busy");
    return 0;
}
