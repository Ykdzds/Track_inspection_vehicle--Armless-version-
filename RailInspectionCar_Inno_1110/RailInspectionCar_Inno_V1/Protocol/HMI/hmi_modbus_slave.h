#ifndef HMI_MODBUS_SLAVE_H
#define HMI_MODBUS_SLAVE_H

#include <stdint.h>

#define HMI_MODBUS_MAX_PDU (252U)

#define HMI_MODBUS_PARSE_IGNORE       (0U)
#define HMI_MODBUS_PARSE_REQUEST      (1U)
#define HMI_MODBUS_PARSE_EXCEPTION    (2U)
#define HMI_MODBUS_PARSE_CRC_ERROR    (3U)
#define HMI_MODBUS_PARSE_FRAME_ERROR  (4U)

typedef struct
{
    uint8_t function;
    uint16_t start;
    uint16_t quantity;
    uint16_t values[24];
    uint8_t value_count;
} hmi_modbus_request_t;

uint16_t hmi_modbus_crc16(const uint8_t *data, uint16_t length);
uint8_t hmi_modbus_parse(const uint8_t *frame, uint16_t length,
                         hmi_modbus_request_t *request,
                         uint8_t response[HMI_MODBUS_MAX_PDU + 3U],
                         uint16_t *response_length);
uint8_t hmi_modbus_exception(uint8_t address, uint8_t function,
                             uint8_t exception, uint8_t response[5U],
                             uint16_t *response_length);
uint8_t hmi_modbus_build_read_response(uint8_t address, uint8_t function,
                                        const uint16_t *values,
                                        uint16_t quantity,
                                        uint8_t response[HMI_MODBUS_MAX_PDU + 3U],
                                        uint16_t *response_length);
uint8_t hmi_modbus_build_write_response(uint8_t address, uint16_t start,
                                        uint16_t quantity,
                                        uint8_t response[8U],
                                        uint16_t *response_length);

#endif
