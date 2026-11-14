#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "app_link_diag.h"

typedef enum
{
    MODBUS_OK = 0,              /**< 操作成功，请求或响应处理正常。 */
    MODBUS_INVALID_ARGUMENT,    /**< 输入参数无效，例如空指针、地址或数量不合法。 */
    MODBUS_NOT_INITIALIZED,     /**< Modbus模块尚未完成初始化。 */
    MODBUS_QUEUE_FULL,          /**< Modbus事务请求队列已满，暂时无法提交新请求。 */
    MODBUS_TIMEOUT,             /**< 等待从站响应或内部操作完成超时。 */
    MODBUS_CRC_ERROR,           /**< 接收帧的CRC校验失败，数据可能损坏。 */
    MODBUS_ADDRESS_ERROR,       /**< 响应帧中的从站地址与请求地址不匹配。 */
    MODBUS_FUNCTION_ERROR,      /**< 响应功能码与请求功能码不匹配或不受支持。 */
    MODBUS_EXCEPTION,           /**< 从站返回Modbus异常响应。 */
    MODBUS_FRAME_ERROR,         /**< 响应帧长度、格式或数据结构不符合协议要求。 */
    MODBUS_TRANSPORT_ERROR      /**< RS485、UART或DMA等底层传输发生错误。 */
} modbus_status_t;

/** Modbus事务操作类型。 */
typedef enum
{
    MODBUS_OPERATION_NONE = 0,       /**< 无具体操作，用于初始化或未指定操作。 */
    MODBUS_OPERATION_READ_HOLDING,    /**< 读取保持寄存器，对应功能码03H。 */
    MODBUS_OPERATION_WRITE_SINGLE,    /**< 写单个保持寄存器，对应功能码06H。 */
    MODBUS_OPERATION_WRITE_MULTIPLE   /**< 写多个保持寄存器，对应功能码10H。 */
} modbus_operation_t;

/** 请求和响应之间使用的事务关联信息。 */
typedef struct
{
    uint32_t transaction_id; /**< 事务唯一编号，用于匹配请求和响应。 */
    uint16_t tag;             /**< 业务标签，用于区分具体操作或命令来源。 */
    uint8_t owner;             /**< 请求发起方标识，例如通信任务或设备适配层。 */
    uint8_t axis;              /**< 关联伺服轴编号，例如左轴或右轴。 */
} modbus_request_context_t;

/** Modbus主站待发送请求描述。 */
typedef struct
{
    modbus_request_context_t context; /**< 请求关联上下文。 */
    uint8_t slave;             /**< 目标从站地址。 */
    uint8_t function;          /**< Modbus功能码，例如03H、06H或10H。 */
    uint16_t address;           /**< 起始寄存器地址。 */
    uint16_t quantity;          /**< 访问寄存器数量。 */
    uint16_t value;             /**< 单寄存器写入值。 */
    uint8_t payload[246];       /**< 多寄存器写入数据或扩展请求载荷。 */
    uint16_t payload_length;    /**< 有效载荷长度，单位为字节。 */
    uint32_t transport_epoch;  /**< 入队时盖章，撤销后旧事务不得执行。 */
    uint8_t retries;            /**< 本请求允许的最大重试次数。 */
    uint16_t timeout_ms;        /**< 等待从站响应的超时时间，单位为毫秒。 */
} modbus_request_t;

/** Modbus从站响应及解析结果。 */
typedef struct
{
    modbus_request_context_t context; /**< 与原请求对应的上下文。 */
    modbus_operation_t operation;    /**< 实际解析出的操作类型。 */
    modbus_status_t status;           /**< 响应处理结果或错误类型。 */
    uint8_t slave;                   /**< 响应中的从站地址。 */
    uint8_t function;                /**< 响应中的功能码。 */
    uint8_t exception_code;          /**< Modbus异常码，无异常时通常为0。 */
    uint16_t address;                /**< 响应对应的起始寄存器地址。 */
    uint16_t quantity;               /**< 响应对应的寄存器数量。 */
    uint16_t value;                  /**< 单寄存器写入或回读的值。 */
    uint8_t data[252];               /**< 读取结果或响应数据。 */
    uint16_t data_length;            /**< 有效响应数据长度，单位为字节。 */
} modbus_response_t;

typedef struct
{
    uint32_t requests;       /**< 已发送或处理的Modbus请求总数。 */
    uint32_t responses;      /**< 成功接收并通过校验的响应总数。 */
    uint32_t retries;        /**< 请求失败或超时后执行的重试总次数。 */
    uint32_t timeouts;       /**< 等待从站响应超时的总次数。 */
    uint32_t crc_errors;     /**< 接收报文CRC校验失败的总次数。 */
    uint32_t frame_errors;   /**< 响应帧长度或格式错误的总次数。 */
    uint32_t exceptions;     /**< 从站返回Modbus异常响应的总次数。 */
} modbus_stats_t;

/* Read-only seqlock snapshot, written only by the bus task; no extra transactions. */
typedef struct {
    uint32_t ms,status,transaction,tag,owner,axis,slave,function,address,transport_epoch,attempt;
    uint32_t queue_overflows,trace_total,trace_overwritten;
} modbus_diagnostic_t;
void modbus_get_diagnostic(modbus_diagnostic_t *out);
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
uint32_t modbus_diag_millis(void);
/* Called before the consumer mutates a verified response or its speed cache.
 * accepted=0 explicitly records a late/mismatched/cancelled response. */
void modbus_diag_consumed(const modbus_response_t *response, uint8_t accepted);
void modbus_diag_speed(const modbus_response_t *response, int16_t previous,
                      int16_t command, uint8_t command_valid, uint16_t state);
#else
static inline void modbus_diag_consumed(const modbus_response_t *r, uint8_t accepted)
{ (void)r; (void)accepted; }
static inline void modbus_diag_speed(const modbus_response_t *r, int16_t previous,
    int16_t command, uint8_t valid, uint16_t state)
{ (void)r; (void)previous; (void)command; (void)valid; (void)state; }
#endif

/** 初始化Modbus主站模块、事务队列和响应通道。 */
modbus_status_t modbus_init(void);
/** 将通用Modbus请求提交到事务队列。 */
modbus_status_t modbus_submit(const modbus_request_t *request,
                              TickType_t wait_ticks);
/** 提交读取保持寄存器请求，使用功能码03H。 */
modbus_status_t modbus_read_holding(
    const modbus_request_context_t *context,
    uint8_t slave,
    uint16_t address,
    uint16_t count,
    TickType_t wait_ticks);
/** 提交写单个保持寄存器请求，使用功能码06H。 */
modbus_status_t modbus_write_single(
    const modbus_request_context_t *context,
    uint8_t slave,
    uint16_t address,
    uint16_t value,
    TickType_t wait_ticks);
/** 提交写多个保持寄存器请求，使用功能码10H。 */
modbus_status_t modbus_write_multiple(
    const modbus_request_context_t *context,
    uint8_t slave,
    uint16_t address,
    const uint16_t *values,
    uint16_t count,
    TickType_t wait_ticks);
/** 等待并获取一条已解析的Modbus响应。 */
bool modbus_get_response(modbus_response_t *response,
                         TickType_t wait_ticks);
/** 执行一次Modbus事务处理和响应解析。 */
void modbus_process(void);
/** 联停时撤销未执行事务并禁止新发送，仅通信任务可恢复发送。 */
void modbus_cancel_pending(void);
void modbus_resume_transport(void);
/* 独立安全入口：专用响应通道，普通业务不能消费安全响应。 */
void modbus_safety_takeover(void);
void modbus_safety_silence(void);
void modbus_safety_diagnostics(void);
void modbus_safety_release(void);
modbus_status_t modbus_safety_submit(const modbus_request_t *request);
bool modbus_safety_get_response(modbus_response_t *response);
bool modbus_business_allowed(void);
bool modbus_motion_permitted(void);
void modbus_set_motion_permitted(bool permitted);

/** 计算Modbus RTU报文的CRC-16校验值。 */
uint16_t modbus_crc16(const uint8_t *data, uint16_t length);
/** 获取当前Modbus通信统计信息。 */
void modbus_get_stats(modbus_stats_t *stats);

#endif
