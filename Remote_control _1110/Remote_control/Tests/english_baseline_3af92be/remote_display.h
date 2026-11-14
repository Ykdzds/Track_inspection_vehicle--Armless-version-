#ifndef REMOTE_DISPLAY_H
#define REMOTE_DISPLAY_H

#include "remote_status_model.h"

#define REMOTE_DISPLAY_ROWS 8U
#define REMOTE_DISPLAY_COLUMNS 21U
#define REMOTE_DISPLAY_PAGE_COUNT 9U
#define REMOTE_PREVIOUS_PAGE_KEY_MASK 0x0040U
#define REMOTE_NEXT_PAGE_KEY_MASK 0x0080U
#define REMOTE_LOCAL_KEY_MASK 0x00C0U
#define REMOTE_DISPLAY_REGIONS 4U

typedef enum {
    REMOTE_PAGE_HOME, REMOTE_PAGE_OVERVIEW, REMOTE_PAGE_SERVO,
    REMOTE_PAGE_DRIVE, REMOTE_PAGE_VELOCITY, REMOTE_PAGE_POSITION,
    REMOTE_PAGE_SYNC, REMOTE_PAGE_DIAGNOSTIC, REMOTE_PAGE_DEBUG
} remote_display_page_t;

typedef struct {
    uint16_t joystick[4], event_sequence;
    uint8_t frame_sequence;
    char short_keys[13], long_keys[13], double_keys[13];
    const char *radio;
} remote_debug_view_t;

typedef struct {
    uint8_t row, x, width;
} remote_display_region_t;

typedef struct {
    char lines[REMOTE_DISPLAY_ROWS][REMOTE_DISPLAY_COLUMNS + 1U];
    uint8_t page, requested_page;
    uint8_t invert_row, invert_x, invert_width, invert;
    uint8_t truncated, region_count;
    uint8_t quality_any, quality_old, radio_live;
    remote_display_region_t regions[REMOTE_DISPLAY_REGIONS];
} remote_display_t;

void remote_display_init(remote_display_t *display);
void remote_display_keys(remote_display_t *display, uint16_t short_keys,
                         uint16_t *short_upload, uint16_t *long_upload,
                         uint16_t *double_upload);
void remote_display_build(remote_display_t *display,
                          const remote_status_model_t *model,
                          const remote_debug_view_t *debug, uint32_t now_ms,
                          uint8_t radio_ready, uint8_t ack_capable);

#endif
