#ifndef APP_CAN_H
#define APP_CAN_H

#include "bsp_can.h"
#include "can_identity.h"
#include "can_link.h"
#include "can_upper.h"
#include "can_arm.h"

typedef struct
{
    bsp_can_stats_t bus;
    uint32_t boot_id, upper_boot_id, revision, generation;
    uint32_t rejected, queue_dropped, record_failed, upper_invalid;
    uint32_t stale_rx, log_skipped, last_service_ms;
    uint16_t token;
    can_link_phase_t phase;
    can_identity_result_t identity_result;
    bool enabled, task_created;
} app_can_status_t;

/* 初始化只在任务创建前执行，Flash追加日志不进入运行期。 */
void app_can_init(void);
void app_can_task_create(void);
bool app_can_get_status(app_can_status_t *status);
bool app_can_get_upper(uint8_t type, can_upper_snapshot_t *snapshot);
/* 同一临界区读取链路及上层缓存，防止跨会话拼接旧记录。 */
bool app_can_get_snapshot(app_can_status_t *status,
                          can_upper_snapshot_t upper[CAN_UPPER_SLOT_COUNT]);
bool app_can_get_arm_summary(uint8_t arm_id, can_arm_summary_t *summary);
bool app_can_get_arm_joint(uint8_t arm_id, uint8_t joint_id,
                           can_arm_joint_t *joint);
bool app_can_get_arm_result(can_arm_result_t *result);
void app_can_get_arm_stats(can_arm_stats_t *stats);
bool app_can_cli_command(const char *line);
void app_can_cli_service(void);

#endif
