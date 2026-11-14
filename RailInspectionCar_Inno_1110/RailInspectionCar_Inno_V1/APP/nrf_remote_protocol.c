#include "nrf_remote_protocol.h"

#include <stddef.h>

static uint16_t nrf_remote_read_u16_be(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

uint16_t nrf_remote_crc16(const uint8_t *data, uint8_t length)
{
    uint16_t crc;
    uint8_t index;
    uint8_t bit;

    if (data == NULL)
    {
        return 0U;
    }

    crc = 0xFFFFU;
    for (index = 0U; index < length; index++)
    {
        crc ^= (uint16_t)data[index] << 8U;
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            }
            else
            {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }
    return crc;
}

nrf_remote_parse_result_t nrf_remote_parse(const uint8_t *payload,
                                           nrf_remote_frame_t *frame)
{
    uint16_t received_crc;
    uint16_t calculated_crc;

    if ((payload == NULL) || (frame == NULL))
    {
        return NRF_REMOTE_PARSE_NULL;
    }
    if ((payload[0] != 0xA5U) || (payload[1] != 0x5AU))
    {
        return NRF_REMOTE_PARSE_HEADER;
    }
    if (payload[2] != 0x01U)
    {
        return NRF_REMOTE_PARSE_VERSION;
    }
    if (payload[3] != 0x01U)
    {
        return NRF_REMOTE_PARSE_TYPE;
    }
    if (payload[5] != 17U)
    {
        return NRF_REMOTE_PARSE_LENGTH;
    }
    if ((payload[30] != 0x0DU) || (payload[31] != 0x0AU))
    {
        return NRF_REMOTE_PARSE_TAIL;
    }

    calculated_crc = nrf_remote_crc16(payload, 28U);
    received_crc = nrf_remote_read_u16_be(&payload[28]);
    if (calculated_crc != received_crc)
    {
        return NRF_REMOTE_PARSE_CRC;
    }

    frame->frame_sequence = payload[4];
    frame->left_horizontal = nrf_remote_read_u16_be(&payload[6]);
    frame->left_vertical = nrf_remote_read_u16_be(&payload[8]);
    frame->right_horizontal = nrf_remote_read_u16_be(&payload[10]);
    frame->right_vertical = nrf_remote_read_u16_be(&payload[12]);
    frame->short_press = (uint16_t)(payload[14] |
                                    ((uint16_t)(payload[15] & 0x0FU) << 8U));
    frame->long_press = (uint16_t)(payload[16] |
                                   ((uint16_t)(payload[17] & 0x0FU) << 8U));
    frame->double_click = (uint16_t)(payload[18] |
                                     ((uint16_t)(payload[19] & 0x0FU) << 8U));
    frame->event_sequence = nrf_remote_read_u16_be(&payload[20]);
    frame->speed_keys_valid = (uint8_t)((payload[23] & 0xFCU) == 0xA0U);
    frame->speed_keys_held = frame->speed_keys_valid ? (uint8_t)(payload[23] & 3U) : 0U;
    frame->speed_steps[0] = frame->speed_keys_valid ? nrf_remote_read_u16_be(&payload[24]) : 0U;
    frame->speed_steps[1] = frame->speed_keys_valid ? nrf_remote_read_u16_be(&payload[26]) : 0U;
    frame->event_valid = (uint8_t)(payload[22] & 0x01U);
    if (frame->event_valid == 0U)
    {
        frame->short_press = 0U;
        frame->long_press = 0U;
        frame->double_click = 0U;
    }
    return NRF_REMOTE_PARSE_OK;
}
