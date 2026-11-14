#ifndef APP_NRF_REMOTE_H
#define APP_NRF_REMOTE_H

#include <stdbool.h>
#include <stdint.h>
#include "app_link_diag.h"

/* Remote diagnostic event kinds; values and field layouts are stable log ABI. */
enum {
    APP_REMOTE_DIAG_RX = 0x01, APP_REMOTE_DIAG_REJECT = 0x02,
    APP_REMOTE_DIAG_LINK_DOWN = 0x03, APP_REMOTE_DIAG_RECOVERY = 0x04,
    APP_REMOTE_DIAG_PUBLISH = 0x10, APP_REMOTE_DIAG_CONSUME = 0x11,
    APP_REMOTE_DIAG_STATE = 0x12, APP_REMOTE_DIAG_BATCH = 0x20,
    APP_REMOTE_DIAG_AXIS_ACK = 0x21, APP_REMOTE_DIAG_BATCH_DONE = 0x22,
    APP_REMOTE_DIAG_SUBMIT_FAIL = 0x23, APP_REMOTE_DIAG_STOP = 0x30,
    APP_REMOTE_DIAG_STOP_UPGRADE = 0x31, APP_REMOTE_DIAG_STOP_DONE = 0x32,
    APP_REMOTE_DIAG_ESTOP = 0x33, APP_REMOTE_DIAG_FIRST_STOP = 0x40,
    APP_REMOTE_DIAG_FIRST_ESTOP = 0x41
};

typedef struct
{
    uint16_t left_horizontal;       /* 左摇杆水平值，范围0～2000，中位1000 */
    uint16_t left_vertical;         /* 左摇杆垂直值，范围0～2000，中位1000 */
    uint16_t right_horizontal;      /* 右摇杆水平值，范围0～2000，中位1000 */
    uint16_t right_vertical;        /* 右摇杆垂直值，范围0～2000，中位1000 */

    /* 最近一次新事件批次的位图：bit0～bit11对应K1～K12，1表示发生该事件。
     * 无新事件时保留原位图，不代表按键当前仍处于按下状态。
     */
    uint16_t short_press;           /* 短按事件位图 */
    uint16_t long_press;            /* 长按事件位图 */
    uint16_t double_click;          /* 双击事件位图 */

    uint8_t frame_sequence;         /* 最近合法帧的帧序号，255后回绕到0 */
    uint16_t event_sequence;        /* 最近接受的新事件批次号，65535后回绕到0 */
    uint32_t age_ms;                /* 距最近合法帧的时间，单位ms；未收帧时从初始化计时 */

    uint32_t valid_frames;          /* 累计合法帧数，包含携带重复事件的合法帧 */
    uint32_t dropped_frames;        /* 根据帧序号间隔估算的累计丢帧数 */
    uint32_t crc_errors;            /* 累计协议CRC16校验失败帧数 */
    uint32_t format_errors;         /* 累计格式错误帧数，如帧头、版本、类型、长度或帧尾错误 */
    uint32_t duplicate_events;      /* 累计被去重或因批次号落后而忽略的有效事件帧数 */
    uint32_t event_batches;         /* 累计接受的新事件批次数，不等于按键事件总数 */
    uint32_t missed_event_batches;  /* 根据事件批次号间隔估算的累计缺失批次数 */

    /* 数组下标0～11对应K1～K12；相同批次重复接收不重复累计。 */
    uint32_t short_counts[12];      /* 各按键累计短按次数 */
    uint32_t long_counts[12];       /* 各按键累计长按次数 */
    uint32_t double_counts[12];     /* 各按键累计双击次数 */

    uint32_t link_loss_count;       /* 累计从在线转为失联的次数，连续失联只计一次 */
    uint32_t nrf_spi_errors;        /* NRF驱动累计SPI传输失败次数 */
    uint32_t nrf_fifo_errors;       /* NRF驱动累计FIFO异常或初始化校验失败次数 */
    uint32_t nrf_recoveries;         /* NRF驱动累计初始化尝试次数 */
    uint32_t recovery_generation;   /* 无线重建代次，快速恢复也必须撤销旧授权 */
    /* 最近两帧合法遥控数据之间的最大间隔，单位为ms。 */
    uint32_t max_frame_gap_ms;
    /* NRF接收服务函数的累计调用次数。 */
    uint32_t service_count;
    /* NRF接收服务两次调用间隔异常的累计次数。 */
    uint32_t service_overrun_count;
    /* 接收服务最近一次执行距当前的时间，单位为ms。 */
    uint32_t task_age_ms;             /* 接收服务最近一次执行距当前的时间 */

    uint8_t speed_keys_valid;
    uint8_t speed_keys_held;
    uint16_t speed_steps[2];
    uint8_t link_up;                /* 通信状态：1=在线，0=未收到合法帧或达到统一失联期限（默认150ms） */
    uint8_t initialized;            /* NRF初始化状态：1=初始化校验通过，0=未初始化或异常待恢复 */
    uint8_t radio_ready;            /* 接收就绪；配置完成但首ACK未装载时为0 */
} nrf_remote_snapshot_t;

void app_nrf_remote_init(void);
void app_nrf_remote_service(void);
bool app_nrf_remote_get_snapshot(nrf_remote_snapshot_t *snapshot);
#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
void app_nrf_remote_diag_snapshot(app_link_diag_snapshot_t *out, uint8_t phase);
#endif

#endif
