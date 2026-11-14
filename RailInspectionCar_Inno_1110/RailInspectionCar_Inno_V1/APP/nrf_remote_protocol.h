#ifndef NRF_REMOTE_PROTOCOL_H
#define NRF_REMOTE_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define NRF_REMOTE_PAYLOAD_SIZE (32U)

typedef enum
{
    NRF_REMOTE_PARSE_OK = 0,
    NRF_REMOTE_PARSE_NULL,
    NRF_REMOTE_PARSE_HEADER,
    NRF_REMOTE_PARSE_VERSION,
    NRF_REMOTE_PARSE_TYPE,
    NRF_REMOTE_PARSE_LENGTH,
    NRF_REMOTE_PARSE_TAIL,
    NRF_REMOTE_PARSE_CRC
} nrf_remote_parse_result_t;

typedef struct
{
    uint8_t frame_sequence;
    uint16_t left_horizontal;
    uint16_t left_vertical;
    uint16_t right_horizontal;
    uint16_t right_vertical;
    uint16_t short_press;
    uint16_t long_press;
    uint16_t double_click;
    uint16_t event_sequence;
    uint8_t speed_keys_valid;
    uint8_t speed_keys_held;
    uint16_t speed_steps[2];
    uint8_t event_valid;
} nrf_remote_frame_t;

uint16_t nrf_remote_crc16(const uint8_t *data, uint8_t length);
nrf_remote_parse_result_t nrf_remote_parse(const uint8_t *payload,
                                           nrf_remote_frame_t *frame);

#endif
