#ifndef APP_ARM3_PORT_H
#define APP_ARM3_PORT_H
#include <stdint.h>
#include <stdbool.h>
struct app_arm3_platform;
struct app_arm3_port_status;
/* Board integration must supply real approved safety samples. Default is unconfigured. */
#ifndef APP_ARM3_PROFILE_ENABLED
#define APP_ARM3_PROFILE_ENABLED 0U
#endif
#if APP_ARM3_PROFILE_ENABLED != 0U && APP_ARM3_PROFILE_ENABLED != 1U
#error "APP_ARM3_PROFILE_ENABLED must be 0 or 1"
#endif
void app_arm3_port_init(uint32_t boot);
void app_arm3_port_service(uint32_t now);
/* CAN task only. Bounded display steps, at most once per 20 ms. The optional
 * callback advances transport only; it must not re-enter the port/manager. */
void app_arm3_port_display_service(uint32_t now, void (*progress)(void));
bool app_arm3_port_remote(const uint8_t frame[32],uint32_t now);
bool app_arm3_port_hmi_write(uint16_t start,uint16_t count,const uint16_t *values,uint32_t now);
bool app_arm3_port_hmi_read(uint16_t start,uint16_t count,uint16_t *values,uint32_t now);
bool app_arm3_port_ack(uint8_t type,uint8_t frame[32],uint32_t now);
void app_arm3_port_stop(void);
/* Read-only diagnostic snapshot; it never grants ownership or motion. */
bool app_arm3_port_get_status(struct app_arm3_port_status *status, uint32_t now_ms);
/* At most 24 words from the published display snapshot; no manager service/read. */
uint8_t app_arm3_port_diag_words(uint32_t *words, uint8_t phase, uint32_t *sampled_ms);
/* Override in an approved board binding; return false for missing/stale wiring. */
bool app_arm3_board_sample(struct app_arm3_platform *sample);
#endif
