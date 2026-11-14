#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "app_nrf_remote.h"
#include "nrf_remote_protocol.h"
#include "NRF24L01.h"
#include "app_config.h"

bool app_arm3_port_remote(const uint8_t p[32],uint32_t now){(void)p;(void)now;return false;}
void app_arm3_port_stop(void){}
static bool arm_active;
bool app_arm3_remote_active(void){return arm_active;}
static TickType_t tick;
static unsigned pending;
static uint8_t radio_ready = 1U;
static uint32_t generation;
uint8_t NRF24L01_RxPacket[32];
TickType_t xTaskGetTickCount(void) { return tick; }
uint32_t HAL_GetTick(void){return tick;}
bool NRF24L01_IsInitialized(void) { return true; }
bool NRF24L01_IsRadioReady(void) { return radio_ready != 0U; }
void NRF24L01_Service(void) {}
void NRF24L01_ReportControlFrame(void) {}
void NRF24L01_GetStats(NRF24L01_Stats *s)
{
    memset(s, 0, sizeof(*s)); s->initialized = 1;
    s->radio_ready = radio_ready; s->recovery_generation = generation;
}
uint8_t NRF24L01_Receive(void) { if (pending) { pending--; return 1; } return 0; }

static void send_frame(uint8_t seq, uint16_t event, uint8_t valid)
{
    uint16_t crc;
    uint8_t *p = NRF24L01_RxPacket;
    memset(p, 0, 32);
    p[0]=0xA5; p[1]=0x5A; p[2]=1; p[3]=1; p[4]=seq; p[5]=17;
    p[6]=3; p[7]=0xE8; p[14]=1; p[15]=8;
    p[20]=(uint8_t)(event>>8); p[21]=(uint8_t)event; p[22]=valid;
    p[30]=13; p[31]=10;
    crc=nrf_remote_crc16(p,28); p[28]=(uint8_t)(crc>>8); p[29]=(uint8_t)crc;
    pending=1;
    app_nrf_remote_service();
}

int main(void)
{
    nrf_remote_snapshot_t s;
    nrf_remote_frame_t f;
    unsigned i;
    const unsigned offsets[]={0,1,2,3,5,30,31,28};
    assert(nrf_remote_crc16((const uint8_t *)"123456789",9)==0x29B1);
    app_nrf_remote_init();
    send_frame(255,65535,1);
    assert(nrf_remote_parse(NRF24L01_RxPacket,&f)==NRF_REMOTE_PARSE_OK);
    for(i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        NRF24L01_RxPacket[offsets[i]]^=1;
        assert(nrf_remote_parse(NRF24L01_RxPacket,&f)!=NRF_REMOTE_PARSE_OK);
        NRF24L01_RxPacket[offsets[i]]^=1;
    }
    send_frame(0,0,1);
    send_frame(1,0,1);
    send_frame(3,0,0);
    app_nrf_remote_get_snapshot(&s);
    assert(s.dropped_frames==1 && s.event_batches==2 && s.duplicate_events==1);
    assert(s.short_counts[0]==2 && s.short_counts[11]==2 && s.short_press==0x801);
    tick=APP_REMOTE_SERVO_LINK_TIMEOUT_MS-1U;
    app_nrf_remote_service();
    app_nrf_remote_get_snapshot(&s);
    assert(s.link_up && s.link_loss_count==0U);
    tick=APP_REMOTE_SERVO_LINK_TIMEOUT_MS;
    app_nrf_remote_service();
    app_nrf_remote_get_snapshot(&s);
    assert(!s.link_up && s.link_loss_count==1);
    app_nrf_remote_service();
    app_nrf_remote_get_snapshot(&s);
    assert(s.link_loss_count==1);
    tick=0xFFFFFFF0U;
    send_frame(1,1,1);
    tick=0x120U;
    app_nrf_remote_service();
    app_nrf_remote_get_snapshot(&s);
    assert(!s.link_up && s.age_ms==304);
    /* 模块未就绪立即失联，恢复首帧建立基准，旧事件不重复执行。 */
    tick += 1U; send_frame(2,2,1);
    app_nrf_remote_get_snapshot(&s);
    unsigned events = s.event_batches;
    radio_ready = 0U; generation++;
    app_nrf_remote_service(); app_nrf_remote_get_snapshot(&s);
    assert(!s.link_up && !s.radio_ready && s.recovery_generation == generation);
    radio_ready = 1U; send_frame(3,2,1);
    app_nrf_remote_get_snapshot(&s);
    assert(s.link_up && s.event_batches == events);
    send_frame(4,2,1); app_nrf_remote_get_snapshot(&s);
    assert(s.event_batches == events);
    send_frame(5,3,1); app_nrf_remote_get_snapshot(&s);
    assert(s.event_batches == events + 1U);
    /* 两次服务间快速重建也要丢弃首帧历史事件。 */
    generation++; send_frame(6,3,1); app_nrf_remote_get_snapshot(&s);
    assert(s.link_up && s.event_batches == events + 1U);
    NRF24L01_RxPacket[23] = 0xA3U;
    NRF24L01_RxPacket[24] = 0xFFU; NRF24L01_RxPacket[25] = 0xFFU;
    NRF24L01_RxPacket[26] = 0x12U; NRF24L01_RxPacket[27] = 0x34U;
    { uint16_t crc = nrf_remote_crc16(NRF24L01_RxPacket,28);
      NRF24L01_RxPacket[28]=(uint8_t)(crc>>8); NRF24L01_RxPacket[29]=(uint8_t)crc; }
    assert(nrf_remote_parse(NRF24L01_RxPacket,&f)==NRF_REMOTE_PARSE_OK);
    assert(f.speed_keys_valid && f.speed_keys_held==3U && f.speed_steps[0]==65535U && f.speed_steps[1]==0x1234U);
    pending=1; app_nrf_remote_service(); app_nrf_remote_get_snapshot(&s);
    assert(s.speed_keys_valid && s.speed_steps[1]==0x1234U);
    NRF24L01_RxPacket[24]^=1U;
    assert(nrf_remote_parse(NRF24L01_RxPacket,&f)==NRF_REMOTE_PARSE_CRC);
    arm_active=true;send_frame(7,4,1);app_nrf_remote_get_snapshot(&s);
    assert(s.left_horizontal==1000U && s.left_vertical==1000U && s.right_vertical==1000U);
    assert((s.short_press&~(1U<<4U))==0U && !s.long_press && !s.double_click && !s.speed_keys_valid);
    arm_active=false;
    puts("NRF protocol/application tests passed");
    return 0;
}
