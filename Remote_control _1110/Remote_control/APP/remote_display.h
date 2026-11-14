#ifndef REMOTE_DISPLAY_H
#define REMOTE_DISPLAY_H
#include "remote_status_model.h"
#define REMOTE_DISPLAY_EN_PAGE_COUNT 9U
#define REMOTE_DISPLAY_ZH_PAGE_COUNT 14U
#define REMOTE_PREVIOUS_PAGE_KEY_MASK 0x0040U
#define REMOTE_NEXT_PAGE_KEY_MASK 0x0080U
#define REMOTE_LOCAL_KEY_MASK 0x00C0U
#define REMOTE_DISPLAY_ITEMS 20U
#define REMOTE_DISPLAY_TEXT_BYTES 80U
#define REMOTE_DISPLAY_REGIONS 12U
typedef enum { REMOTE_DISPLAY_ENGLISH = 0U, REMOTE_DISPLAY_CHINESE = 1U } remote_display_language_t;
typedef enum {
    REMOTE_EN_PAGE_HOME, REMOTE_EN_PAGE_OVERVIEW, REMOTE_EN_PAGE_SERVO,
    REMOTE_EN_PAGE_DRIVE, REMOTE_EN_PAGE_VELOCITY, REMOTE_EN_PAGE_POSITION,
    REMOTE_EN_PAGE_SYNC, REMOTE_EN_PAGE_DIAGNOSTIC, REMOTE_EN_PAGE_DEBUG
} remote_display_english_page_t;
typedef enum {
    REMOTE_PAGE_HOME, REMOTE_PAGE_FEEDBACK, REMOTE_PAGE_OVERVIEW,
    REMOTE_PAGE_COMMAND, REMOTE_PAGE_SPEED, REMOTE_PAGE_COMMUNICATION,
    REMOTE_PAGE_SERVO, REMOTE_PAGE_SERVO2, REMOTE_PAGE_DISTANCE,
    REMOTE_PAGE_SYNC, REMOTE_PAGE_SYNC_QUALITY, REMOTE_PAGE_EXTENSION,
    REMOTE_PAGE_DEBUG, REMOTE_PAGE_SNAPSHOT
} remote_display_page_t;
typedef struct {
    uint16_t joystick[4], event_sequence;
    uint8_t frame_sequence;
    char short_keys[13], long_keys[13], double_keys[13];
    const char *radio;
} remote_debug_view_t;
typedef struct { uint8_t x, y, width, height, underline; } remote_display_region_t;
typedef struct {
    char text[REMOTE_DISPLAY_TEXT_BYTES];
    uint8_t x, y, width, height, font;
} remote_display_item_t;
typedef struct {
    remote_display_item_t items[REMOTE_DISPLAY_ITEMS];
    remote_display_region_t regions[REMOTE_DISPLAY_REGIONS];
    char diagnostic_title[22];
    uint8_t page, requested_page, language, requested_language, item_count, region_count;
    uint8_t truncated, quality_any, quality_old, radio_live, invert;
} remote_display_t;
uint8_t remote_display_page_count(remote_display_language_t language);
void remote_display_init(remote_display_t *display);
void remote_display_keys(remote_display_t *display, uint16_t short_keys,
                         uint16_t *short_upload, uint16_t *long_upload, uint16_t *double_upload);
void remote_display_build(remote_display_t *display, const remote_status_model_t *model,
                          const remote_debug_view_t *debug, uint32_t now_ms,
                          uint8_t radio_ready, uint8_t ack_capable);
#endif
