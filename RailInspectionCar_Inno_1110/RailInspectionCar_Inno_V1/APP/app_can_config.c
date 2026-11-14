#include "app_can_config.h"
#include "app_config.h"
#include "can_protocol.h"
#include "app_can_control.h"
#include "app_arm3.h"
#include <string.h>

/* 配置字段不能静默截断，否则上层距离换算与控制器参数将不一致。 */
#if APP_POSITION_LEFT_GEAR_NUM > 65534U || APP_POSITION_LEFT_GEAR_DEN > 65534U || \
    APP_POSITION_RIGHT_GEAR_NUM > 65534U || APP_POSITION_RIGHT_GEAR_DEN > 65534U || \
    APP_POSITION_LEFT_WHEEL_DIAMETER_MM > 4294967U || \
    APP_POSITION_RIGHT_WHEEL_DIAMETER_MM > 4294967U || \
    APP_CAN_LINK_TIMEOUT_MS > 65534U
#error "CAN configuration fields exceed protocol range"
#endif

static uint16_t scaled_u16(double value)
{
    /* 无法表达的参数显示未知，不将饱和值冒充完整配置。 */
    return value < 0.0 || value > 65534.0 ? 65535U : (uint16_t)(value + 0.5);
}

void app_can_config_encode(uint8_t p[80], uint32_t revision)
{
    uint16_t flags = (uint16_t)((APP_POSITION_MONITOR_ENABLED ? 1U : 0U) |
        (APP_REMOTE_SERVO_SYNC_ENABLED ? 2U : 0U) |
        (APP_SERVO_STARTUP_HOLD_ENABLED ? 4U : 0U) |
        (APP_SERVO_BOOT_ZERO_ENABLED ? 8U : 0U) |
        (APP_SERVO_READONLY_TEST ? 16U : 0U) |
        (APP_CAN_CONTROL_ALLOW_REVERSE ? 256U : 0U) | 512U | 1024U);
    memset(p, 0, 80U);
    can_protocol_write_u32(p, revision);
    p[4] = APP_SERVO_CONTROL_AXIS;
    p[5] = APP_SERVO_QUERY_MODE;
    p[6] = (uint8_t)APP_POSITION_LEFT_SIGN;
    p[7] = (uint8_t)APP_POSITION_RIGHT_SIGN;
    can_protocol_write_u32(p + 8U, APP_POSITION_LEFT_WHEEL_DIAMETER_MM * 1000UL);
    can_protocol_write_u32(p + 12U, APP_POSITION_RIGHT_WHEEL_DIAMETER_MM * 1000UL);
    can_protocol_write_u32(p + 16U, APP_POSITION_LEFT_COUNTS_PER_REV);
    can_protocol_write_u32(p + 20U, APP_POSITION_RIGHT_COUNTS_PER_REV);
    can_protocol_write_u16(p + 24U, APP_POSITION_LEFT_GEAR_NUM);
    can_protocol_write_u16(p + 26U, APP_POSITION_LEFT_GEAR_DEN);
    can_protocol_write_u16(p + 28U, APP_POSITION_RIGHT_GEAR_NUM);
    can_protocol_write_u16(p + 30U, APP_POSITION_RIGHT_GEAR_DEN);
    can_protocol_write_u16(p + 32U, scaled_u16(APP_DEBUG_MAX_RPM));
    can_protocol_write_u16(p + 34U, APP_CAN_CONTROL_ENABLED && app_can_control_config_valid() ? app_can_control_limit() : 0U);
    can_protocol_write_u16(p + 36U, scaled_u16(APP_POSITION_MAX_AGE_MS));
    can_protocol_write_u16(p + 38U, scaled_u16(APP_POSITION_MAX_SKEW_MS));
    can_protocol_write_u16(p + 40U, scaled_u16(APP_SERVO_BACKGROUND_POLL_MS));
    can_protocol_write_u16(p + 42U, scaled_u16((double)APP_REMOTE_SERVO_SYNC_KP * 10000.0));
    can_protocol_write_u16(p + 44U, (uint16_t)APP_REMOTE_SERVO_SYNC_DEADBAND_RPM);
    can_protocol_write_u16(p + 46U, scaled_u16(APP_REMOTE_SERVO_SYNC_LIMIT_RPM));
    can_protocol_write_u16(p + 48U, scaled_u16(APP_REMOTE_SERVO_SYNC_PERIOD_MS));
    can_protocol_write_u16(p + 50U, scaled_u16(APP_SERVO_SPEED_ACCELERATION_RAMP_MS));
    can_protocol_write_u16(p + 52U, scaled_u16(APP_WDG_RECOVERY_DECEL_MS));
    can_protocol_write_u16(p + 54U, APP_CAN_LINK_TIMEOUT_MS);
    /* 正式只读构建保留期限字段，运动能力由能力位及批准配置共同判定。 */
    can_protocol_write_u16(p + 56U, APP_CAN_SPEED_TIMEOUT_MS);
    can_protocol_write_u16(p + 58U, APP_CAN_CONTROL_ENABLED ? scaled_u16((double)APP_CAN_LINK_STOP_DECEL_MPS2 * 1000.0) : 65535U);
    can_protocol_write_u16(p + 60U, APP_CAN_CONTROL_ENABLED && APP_CAN_QUICK_STOP_ENABLED ?
        scaled_u16((double)APP_CAN_QUICK_STOP_DECEL_MPS2 * 1000.0) : 65535U);
    can_protocol_write_u16(p + 62U, scaled_u16((double)APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2 * 1000.0));
    can_protocol_write_u16(p + 64U, scaled_u16((double)APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 * 1000.0));
    can_protocol_write_u16(p + 66U, scaled_u16(APP_REMOTE_SERVO_LINK_TIMEOUT_MS));
    can_protocol_write_u32(p + 68U, APP_SERVO_CLI_DEBUG_TIMEOUT_MS);
    can_protocol_write_u16(p + 72U, scaled_u16(APP_SERVO_STATUS_MAX_AGE_MS));
    can_protocol_write_u16(p + 74U, scaled_u16(APP_REMOTE_SERVO_SYNC_MAX_AGE_MS));
    can_protocol_write_u16(p + 76U, scaled_u16(APP_REMOTE_SERVO_SYNC_MAX_SKEW_MS));
    can_protocol_write_u16(p + 78U, flags);
}

uint32_t app_can_config_fingerprint(void)
{
    uint8_t payload[152];
    static const uint32_t policy[] = {0x43414E32UL, APP_CAN_AUTH_MODE,
        APP_CAN_ACQUIRE_TIMEOUT_MS, APP_CAN_FIRST_SPEED_TIMEOUT_MS,
        APP_CAN_WRITE_CONFIRM_TIMEOUT_MS, APP_CAN_REVERSE_CONFIRM_TIMEOUT_MS,
        APP_CAN_STOP_CONFIRM_MARGIN_MS, APP_CAN_STOP_TIMEOUT_MAX_MS,
        APP_CAN_STOP_HOLD_ENABLED, APP_CAN_CONTROL_ALLOW_REVERSE,
        APP_CAN_QUICK_STOP_ENABLED, APP_CAN_SPEED_REFRESH_PERIOD_MS};
    unsigned i;
    memset(payload, 0, sizeof(payload));
    app_can_config_encode(payload, 0U);
    for (i = 0U; i < sizeof(policy) / sizeof(policy[0]); ++i)
        can_protocol_write_u32(payload + 80U + i * 4U, policy[i]);
    can_protocol_write_u32(payload + 128U, scaled_u16((double)APP_CAN_NORMAL_STOP_DECEL_MPS2 * 1000.0));
    can_protocol_write_u32(payload + 132U, scaled_u16((double)APP_CAN_LINK_STOP_DECEL_MPS2 * 1000.0));
    can_protocol_write_u32(payload + 136U, scaled_u16((double)APP_CAN_QUICK_STOP_DECEL_MPS2 * 1000.0));
    can_protocol_write_u32(payload + 140U, scaled_u16((double)APP_CAN_ACCELERATION_MPS2 * 1000.0));
    can_protocol_write_u32(payload + 144U, scaled_u16((double)APP_CAN_DECELERATION_MPS2 * 1000.0));
    can_protocol_write_u32(payload + 148U, scaled_u16((double)APP_CAN_REVERSE_DECEL_MPS2 * 1000.0));
    return can_protocol_crc32(payload, sizeof(payload));
}

uint32_t app_can_capabilities(void)
{
    return ((1UL<<26U)|(1UL<<27U)|(1UL<<31U)) | 7UL | (APP_POSITION_MONITOR_ENABLED ? 1UL << 3U : 0U) |
        (APP_REMOTE_SERVO_SYNC_ENABLED ? 1UL << 4U : 0U) |
        (1UL << 5U) | (1UL << 20U) | (1UL << 23U) | (1UL << 30U) |
        (1UL << 26U) | (APP_CAN_CONTROL_ENABLED && app_can_control_config_valid() ?
            (1UL << 6U) | (1UL << 21U) | (APP_REMOTE_SERVO_SYNC_ENABLED ? 1UL << 22U : 0U) | (1UL << 29U) : 0U);
}

uint32_t app_can_data_sources(void)
{
    /* 0x53当前只有无许可快照，真实硬件联锁尚未接入，数据源位保持0。 */
    return app_can_capabilities() & ~((1UL << 26U) | (1UL << 29U));
}
