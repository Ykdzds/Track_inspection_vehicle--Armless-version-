#include "remote_display.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static void send_frame(remote_status_model_t *model, uint8_t type,
                       uint8_t sequence, uint8_t session, uint8_t axes,
                       uint8_t valid, uint8_t stale, uint32_t now)
{
    uint8_t frame[32] = {0};
    uint8_t axis, field;
    const int16_t values[4] = {-123, 456, -123, 3250};
    frame[0] = 0x5AU; frame[1] = 0xA5U; frame[2] = 1U;
    frame[3] = type; frame[4] = sequence; frame[5] = 22U;
    frame[6] = session; frame[7] = axes;
    frame[30] = 13U; frame[31] = 10U;
    if (type == 0x89U) {
        for (axis = 0U; axis < 2U; axis++) {
            frame[10U + axis] = stale;
            for (field = 0U; field < 4U; field++) {
                uint16_t encoded = (uint16_t)((uint16_t)values[field] & 0x7FFFU);
                if (valid & (1U << field)) encoded |= 0x8000U;
                put16(frame + 12U + axis * 8U + field * 2U, encoded);
            }
        }
    }
    put16(frame + 28U, remote_status_crc16(frame, 28U));
    assert(remote_status_accept(model, frame, sizeof(frame), now));
}

static void build(remote_display_t *display, remote_status_model_t *model,
                  uint32_t now)
{
    remote_debug_view_t debug = {{0}, 0U, 0U, "", "", "", "OK"};
    display->requested_language = REMOTE_DISPLAY_CHINESE;
    display->requested_page = REMOTE_PAGE_HOME;
    remote_display_build(display, model, &debug, now, 1U, 1U);
    assert(!display->truncated);

}


int main(void)
{
    remote_status_model_t model;remote_display_t display;
    remote_status_init(&model);remote_display_init(&display);build(&display,&model,0U);
    for(uint8_t axes=1U;axes<=3U;axes++) {
        remote_status_init(&model);
        send_frame(&model,0x89U,255U,1U,axes,15U,0U,100U);build(&display,&model,100U);assert(display.quality_any);
        send_frame(&model,0x81U,0U,1U,axes,0U,0U,200U);assert(model.diagnostic_cache.received);
        send_frame(&model,0x89U,1U,1U,axes,0U,0U,300U);build(&display,&model,300U);assert(display.quality_old);
        assert(model.diagnostic_cache.frame.data.diagnostic.torque[axes==2U?1U:0U]==-123);
        build(&display,&model,700U);assert(!display.radio_live);
        send_frame(&model,0x81U,2U,1U,axes,0U,0U,4000U);assert(model.diagnostic_cache.received);
        send_frame(&model,0x89U,3U,1U,axes,15U,4U,4100U);build(&display,&model,4100U);assert(model.history[RH_CURRENT1+ (axes==2U)].stale);
        send_frame(&model,0x89U,4U,1U,axes,15U,0U,4200U);build(&display,&model,4200U);
        send_frame(&model,0x81U,1U,2U,axes,0U,0U,4300U);build(&display,&model,4300U);assert(!model.diagnostic_cache.received);
    }
    puts("PASS servo diagnostics: fixed point, selected axes, retained history, stale, loss and session");return 0;
}
