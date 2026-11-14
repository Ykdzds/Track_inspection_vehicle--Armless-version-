#include "hmi_modbus_slave.h"

#include <string.h>

#include "app_config.h"

uint16_t hmi_modbus_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;

    for (i = 0U; i < length; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
            crc = (crc & 1U) ? (uint16_t)((crc >> 1U) ^ 0xA001U) :
                               (uint16_t)(crc >> 1U);
    }
    return crc;
}

static void put_crc(uint8_t *frame, uint16_t length)
{
    uint16_t crc = hmi_modbus_crc16(frame, length);
    frame[length] = (uint8_t)crc;
    frame[length + 1U] = (uint8_t)(crc >> 8U);
}

uint8_t hmi_modbus_exception(uint8_t address, uint8_t function,
                             uint8_t exception, uint8_t response[5U],
                             uint16_t *response_length)
{
    if (response == NULL || response_length == NULL) return 0U;
    response[0] = address;
    response[1] = (uint8_t)(function | 0x80U);
    response[2] = exception;
    put_crc(response, 3U);
    *response_length = 5U;
    return 1U;
}

uint8_t hmi_modbus_parse(const uint8_t *frame, uint16_t length,
                         hmi_modbus_request_t *request,
                         uint8_t response[HMI_MODBUS_MAX_PDU + 3U],
                         uint16_t *response_length)
{
    uint16_t crc;
    uint16_t i;
    uint16_t byte_count;

    if (frame == NULL || request == NULL || response == NULL ||
        response_length == NULL || length < 4U) return 0U;
    *response_length = 0U;
    if (frame[0] != APP_HMI_ADDRESS) return 0U;
    crc = hmi_modbus_crc16(frame, (uint16_t)(length - 2U));
    if (frame[length - 2U] != (uint8_t)crc ||
        frame[length - 1U] != (uint8_t)(crc >> 8U))
        return HMI_MODBUS_PARSE_CRC_ERROR;
    memset(request, 0, sizeof(*request));
    request->function = frame[1];
    request->start = (uint16_t)((uint16_t)frame[2] << 8U | frame[3]);
    if (request->function == 0x03U)
    {
        if (length != 8U)
        {
            (void)hmi_modbus_exception(frame[0], frame[1], 3U, response, response_length);
            return HMI_MODBUS_PARSE_FRAME_ERROR;
        }
        request->quantity = (uint16_t)((uint16_t)frame[4] << 8U | frame[5]);
        if (request->quantity == 0U || request->quantity > 125U)
        {
            (void)hmi_modbus_exception(frame[0], frame[1], 3U, response, response_length);
            return HMI_MODBUS_PARSE_FRAME_ERROR;
        }
        return 1U;
    }
    if (request->function == 0x10U)
    {
        if (length < 9U)
        {
            (void)hmi_modbus_exception(frame[0], frame[1], 3U, response, response_length);
            return HMI_MODBUS_PARSE_FRAME_ERROR;
        }
        request->quantity = (uint16_t)((uint16_t)frame[4] << 8U | frame[5]);
        byte_count = frame[6];
        if (request->quantity != 24U || byte_count != 48U || length != 57U)
        {
            (void)hmi_modbus_exception(frame[0], frame[1], 3U, response, response_length);
            return 2U;
        }
        for (i = 0U; i < 24U; i++)
            request->values[i] = (uint16_t)((uint16_t)frame[7U + i * 2U] << 8U |
                                            frame[8U + i * 2U]);
        request->value_count = 24U;
        return 1U;
    }
    (void)hmi_modbus_exception(frame[0], frame[1], 1U, response, response_length);
    return HMI_MODBUS_PARSE_EXCEPTION;
}

uint8_t hmi_modbus_build_read_response(uint8_t address, uint8_t function,
                                       const uint16_t *values,
                                       uint16_t quantity,
                                       uint8_t response[HMI_MODBUS_MAX_PDU + 3U],
                                       uint16_t *response_length)
{
    uint16_t i;
    uint16_t body;
    if (values == NULL || response == NULL || response_length == NULL ||
        quantity > 125U) return 0U;
    response[0] = address;
    response[1] = function;
    response[2] = (uint8_t)(quantity * 2U);
    for (i = 0U; i < quantity; i++)
    {
        response[3U + i * 2U] = (uint8_t)(values[i] >> 8U);
        response[4U + i * 2U] = (uint8_t)values[i];
    }
    body = (uint16_t)(3U + quantity * 2U);
    put_crc(response, body);
    *response_length = (uint16_t)(body + 2U);
    return 1U;
}

uint8_t hmi_modbus_build_write_response(uint8_t address, uint16_t start,
                                        uint16_t quantity, uint8_t response[8U],
                                        uint16_t *response_length)
{
    if (response == NULL || response_length == NULL) return 0U;
    response[0] = address;
    response[1] = 0x10U;
    response[2] = (uint8_t)(start >> 8U);
    response[3] = (uint8_t)start;
    response[4] = (uint8_t)(quantity >> 8U);
    response[5] = (uint8_t)quantity;
    put_crc(response, 6U);
    *response_length = 8U;
    return 1U;
}
