#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hmi_modbus_slave.h"

static uint16_t append_crc(uint8_t *frame, uint16_t length)
{
    uint16_t crc = hmi_modbus_crc16(frame, length);
    frame[length] = (uint8_t)crc;
    frame[length + 1U] = (uint8_t)(crc >> 8U);
    return (uint16_t)(length + 2U);
}

static void test_read_request(void)
{
    uint8_t frame[] = { 1U, 3U, 0U, 0U, 0U, 2U, 0U, 0U };
    uint8_t response[HMI_MODBUS_MAX_PDU + 3U];
    hmi_modbus_request_t request;
    uint16_t length = append_crc(frame, 6U);

    assert(length == 8U);
    assert(hmi_modbus_parse(frame, length, &request, response, &length) == 1U);
    assert(request.function == 3U);
    assert(request.start == 0U);
    assert(request.quantity == 2U);
}

static void test_mailbox_request(void)
{
    uint8_t frame[57U];
    uint8_t response[HMI_MODBUS_MAX_PDU + 3U];
    hmi_modbus_request_t request;
    uint16_t length;
    uint16_t index;

    memset(frame, 0, sizeof(frame));
    frame[0] = 1U;
    frame[1] = 0x10U;
    frame[2] = 0x01U;
    frame[3] = 0x00U;
    frame[4] = 0x00U;
    frame[5] = 0x18U;
    frame[6] = 0x30U;
    for (index = 0U; index < 24U; index++)
    {
        frame[7U + index * 2U] = (uint8_t)(index >> 8U);
        frame[8U + index * 2U] = (uint8_t)index;
    }
    length = append_crc(frame, 55U);
    assert(length == sizeof(frame));
    assert(hmi_modbus_parse(frame, length, &request, response, &length) == 1U);
    assert(request.function == 0x10U);
    assert(request.start == 0x0100U);
    assert(request.quantity == 24U);
    assert(request.value_count == 24U);
    assert(request.values[23] == 23U);
}

static void test_rejection(void)
{
    uint8_t frame[] = { 1U, 3U, 0U, 0U, 0U, 2U, 0U, 0U };
    uint8_t response[HMI_MODBUS_MAX_PDU + 3U];
    hmi_modbus_request_t request;
    uint16_t length;

    length = 8U;
    frame[6] ^= 0x01U;
    assert(hmi_modbus_parse(frame, length, &request, response, &length) ==
           HMI_MODBUS_PARSE_CRC_ERROR);
    frame[6] ^= 0x01U;
    length = append_crc(frame, 6U);
    frame[1] = 0x06U;
    length = append_crc(frame, 6U);
    assert(hmi_modbus_parse(frame, length, &request, response, &length) ==
           HMI_MODBUS_PARSE_EXCEPTION);
    assert(response[1] == 0x86U);
    assert(response[2] == 1U);
}

int main(void)
{
    test_read_request();
    test_mailbox_request();
    test_rejection();
    puts("PASS HMI Modbus: CRC, 03H, complete 10H mailbox and rejection");
    return 0;
}
