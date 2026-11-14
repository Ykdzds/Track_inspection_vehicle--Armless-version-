#include "app_can_status.h"
#include "app_can_config.h"
#include "app_config.h"
#include "can_link.h"
#include "can_upper.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 向量使用固定身份及无硬件反馈的测试场景；编码、CRC、握手均执行生产代码。 */
static can_link_t link;
static can_upper_cache_t upper;
static app_can_status_input_t input;
static uint32_t now, cookie;
static bool pending;

static void print_frame(const char *direction, uint16_t id, const uint8_t data[8])
{
    unsigned i;
    printf("| %lu | %s | `%03X` | `", (unsigned long)now, direction, id);
    for (i = 0U; i < 8U; ++i) printf("%s%02X", i == 0U ? "" : " ", data[i]);
    puts("` |");
}

static bool send_frame(void *context, uint16_t id, const uint8_t data[8],
                       uint32_t value, uint32_t deadline)
{
    (void)context;
    assert(!pending && (int32_t)(deadline - now) > 0);
    cookie = value;
    pending = true;
    print_frame("STM32->RK", id, data);
    return true;
}

static bool build_record(void *context, uint8_t type,
                         can_protocol_record_t *record, uint32_t tick)
{
    (void)context;
    input.boot_id = link.lower_boot_id;
    input.upper_boot_id = link.upper_boot_id;
    input.token = link.token;
    input.config_revision = link.config_revision;
    input.selected = APP_SERVO_CONTROL_AXIS;
    input.now_ms = input.snapshot_ms = input.sample_tick = tick;
    return app_can_status_record(&input, type, record);
}

static bool publish_upper(void *context, const can_protocol_record_t *record,
                          uint32_t tick)
{
    (void)context;
    return can_upper_publish(&upper, record, tick, link.lower_boot_id,
                             link.upper_boot_id);
}

static void receive(uint16_t id, const uint8_t data[8])
{
    print_frame("RK->STM32", id, data);
    can_link_receive(&link, id, data, now++);
}

static void step(void)
{
    can_link_service(&link, now);
    if (pending)
    {
        pending = false;
        /* 模拟每帧成功完成，不能将此向量当作CAN硬件发送证据。 */
        can_link_tx_result(&link, cookie, true, now);
    }
    ++now;
}

static void drain(unsigned count)
{
    unsigned i;
    for (i = 0U; i < count; ++i) step();
}

static void upper_record(uint8_t type, uint16_t sequence)
{
    can_protocol_record_t record;
    uint16_t i, id, count;
    uint8_t data[8];
    memset(&record, 0, sizeof(record));
    record.type = type;
    record.sequence = sequence;
    record.token = link.token;
    record.length = 32U;
    if (type == 0x02U)
    {
        can_protocol_write_u32(record.payload, link.upper_boot_id);
        can_protocol_write_u32(record.payload + 4U, link.lower_boot_id);
        can_protocol_write_u32(record.payload + 8U, 1U);
        can_protocol_write_u32(record.payload + 12U, 1U);
        record.payload[20] = record.payload[22] = 1U;
    }
    /* 任务状态0x40全零表示测试场景中的空闲，不代表实际RK状态。 */
    count = (uint16_t)((record.length + 3U) / 4U);
    for (i = 0U; i < count + 2U; ++i)
    {
        assert(can_protocol_encode_record_frame(&record, i, &id, data));
        receive((uint16_t)(id + 0x10U), data);
    }
}

int main(void)
{
    can_link_callbacks_t callbacks;
    can_upper_snapshot_t snapshot;
    uint8_t data[8] = {0U};
    unsigned i;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.send = send_frame;
    callbacks.build_record = build_record;
    callbacks.publish_upper = publish_upper;
    can_upper_init(&upper);
    can_link_init(&link, &callbacks, 123U, 100U, 131U, 7U,
                   app_can_capabilities(), 500U, 0U);
    puts("# CAN V1.0生产代码编码测试向量\n");
    puts("本文件由`Tests/generate_can_vectors.ps1`生成，不是CAN分析仪实测记录。");
    puts("固定测试身份：STM32 boot=123、RK boot=456、通信令牌=100、配置版本=7。");
    puts("伺服反馈未提供，因此状态数据为未知。配置字段来自生成时工程宏；重新生成后可能变化。");
    printf("配置指纹：`%08lX`；轴配置：`%u`；CRC32自检：`%08lX`。\n\n",
        (unsigned long)app_can_config_fingerprint(), APP_SERVO_CONTROL_AXIS,
        (unsigned long)can_protocol_crc32((const uint8_t *)"123456789", 9U));
    puts("每帧标准数据帧、DLC=8、500 kbit/s；数据列为十六进制，小端字段。");
    puts("时间只用于此单元测试步进，RK必须使用收到的真实boot、令牌和邀请序号，禁止硬编码。\n");
    puts("生成器未运行完整APP周期调度；本表不能作为周期帧时序或CAN分析仪实测证据。\n");
    puts("## 完整握手与初始能力配置\n");
    puts("| t/ms | 方向 | ID | DATA |");
    puts("|---:|---|---|---|");
    step();
    can_protocol_write_u32(data, 456U);
    data[4] = 1U;
    receive(0x102U, data);
    can_protocol_write_u32(data, 123U);
    can_protocol_write_u32(data + 4U, 456U);
    receive(0x104U, data);
    for (i = 0U; i < 60U && !link.invite_delivered; ++i) step();
    assert(link.invite_delivered && link.token == 100U);
    can_protocol_write_u16(data, link.token);
    can_protocol_write_u16(data + 2U, link.invite_sequence);
    can_protocol_write_u32(data + 4U, 456U);
    receive(0x105U, data);
    for (i = 0U; i < 30U && !link.confirm_delivered; ++i) step();
    assert(link.confirm_delivered);
    memset(data, 0, sizeof(data));
    data[0] = data[2] = 1U;
    can_protocol_write_u16(data + 4U, link.token);
    for (i = 0U; i < 3U; ++i)
    {
        data[1] = (uint8_t)(i + 1U);
        receive(0x100U, data);
        if (i < 2U) assert(link.phase != CAN_LINK_READY);
    }
    assert(link.phase == CAN_LINK_READY);
    drain(60U);
    puts("\n## 查询通信统计与拒绝控制请求\n");
    puts("| t/ms | 方向 | ID | DATA |");
    puts("|---:|---|---|---|");
    memset(data, 0, sizeof(data));
    data[0] = 1U;
    data[1] = 0x08U;
    data[2] = 0x24U;
    can_protocol_write_u16(data + 6U, link.token);
    receive(0x110U, data);
    /* Status records may precede diagnostics; wait within the query deadline. */
    for (i = 0U; i < 300U && link.requests_completed == 0U; ++i) step();
    assert(link.requests_completed == 1U);
    data[0] = 2U;
    data[1] = 0x01U;
    data[2] = 0U;
    receive(0x110U, data);
    drain(5U);
    memset(data, 0, sizeof(data));
    can_protocol_write_u16(data, 1U);
    can_protocol_write_u16(data + 2U, link.token);
    receive(0x080U, data);
    drain(20U);
    assert(link.rejected == 2U);
    puts("\n## RK能力和任务状态上传\n");
    puts("| t/ms | 方向 | ID | DATA |");
    puts("|---:|---|---|---|");
    upper_record(0x02U, 1U);
    upper_record(0x40U, 2U);
    assert(can_upper_get(&upper, 0x02U, now, &snapshot) && snapshot.valid);
    assert(can_upper_get(&upper, 0x40U, now, &snapshot) && snapshot.valid);
    assert(link.upper_invalid == 0U);
    puts("\n预期：会话READY；查询先接受后完成；模式与停车请求返回不支持，未执行伺服操作；RK能力和任务记录完整CRC校验后发布。");
    puts("缺段、CRC错误、旧会话、心跳超时及Bus-off见专项测试和联调说明。");
    return 0;
}
