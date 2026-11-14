#ifndef BSP_RS485_H
#define BSP_RS485_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "app_link_diag.h"

#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
/* Milliseconds from the same RTOS clock. Bits: start=1, actual UART TC=2,
 * DMA bytes captured=4. A DMA completion alone never sets the TC bit. */
typedef struct { uint32_t start_ms, tc_ms, rx_ms, valid; } rs485_diag_times_t;
void rs485_diag_begin(void);
void rs485_diag_times(rs485_diag_times_t *out);
#endif

typedef enum
{
    RS485_OK = 0,              /**< 操作成功。 */
    RS485_INVALID_ARGUMENT,    /**< 输入参数无效，例如空指针或长度为0。 */
    RS485_NOT_INITIALIZED,     /**< RS485模块尚未初始化。 */
    RS485_BUSY,                /**< 发送资源正忙，当前无法执行操作。 */
    RS485_TIMEOUT,             /**< 等待DMA发送完成超时。 */
    RS485_HAL_ERROR            /**< STM32 HAL底层操作失败。 */
} rs485_status_t;

typedef struct
{
    uint32_t rx_bytes;         /**< 成功复制到软件环形缓冲区的字节总数。 */
    uint32_t tx_bytes;         /**< 发送字节总数。 */
    uint32_t rx_overflows;     /**< 软件环形缓冲区溢出次数。 */
    uint32_t uart_errors;      /**< UART错误处理次数，包含IDLE入口保存的接收错误。 */
    uint32_t tx_timeouts;      /**< DMA发送完成等待超时次数。 */
    uint32_t rx_restarts;      /**< UART/DMA异常后的接收重启次数。 */
    uint32_t idle_interrupts;  /**< USART2空闲线中断次数。 */
    uint32_t dma_half_events;  /**< RX DMA半传输事件次数。 */
    uint32_t dma_full_events;  /**< RX DMA满传输事件次数。 */
    uint32_t irq_entries;      /**< USART2中断入口进入次数。 */
    uint32_t dma_start_failures; /**< RX DMA启动失败次数。 */
    uint32_t rx_abort_failures; /**< 恢复时终止RX失败次数。 */
    uint32_t uart_error_flags; /**< 按USART SR位定义累计的ORE、FE、NE、PE。 */
    uint32_t hal_error_flags;  /**< HAL错误码累计值，包含DMA错误。 */
    uint32_t uart_sr;          /**< USART2状态寄存器快照，不读取DR。 */
    uint32_t uart_cr1;         /**< USART2控制寄存器1快照。 */
    uint32_t uart_cr3;         /**< USART2控制寄存器3快照。 */
    uint32_t dma_control;      /**< RX DMA流控制寄存器快照。 */
    uint32_t rx_state;         /**< HAL接收状态快照。 */
    uint16_t dma_remaining;    /**< DMA本轮剩余接收字节数。 */
    uint16_t dma_position;     /**< RX DMA当前写入位置。 */
    uint16_t dma_last_position; /**< 上次搬运结束位置。 */
    uint8_t dma_running;       /**< RX DMA是否处于运行状态。 */
    uint8_t recovery_pending;  /**< 是否等待通信任务恢复接收。 */
    uint8_t pa3_mode;          /**< PA3模式，USART2接收应为复用模式2。 */
    uint8_t pa3_af;            /**< PA3复用编号，USART2应为7。 */
} rs485_stats_t;

rs485_status_t rs485_init(void);
rs485_status_t rs485_send(const uint8_t *data, uint16_t length);
uint16_t rs485_read(uint8_t *data, uint16_t capacity);
uint16_t rs485_available(void);
uint32_t rs485_last_rx_tick(void);
void rs485_bind_rx_task(TaskHandle_t task);
void rs485_service(void);
/** 禁止启动新的发送，已经在线路上的字节不能撤回。 */
void rs485_inhibit_transmit(uint8_t inhibit);
void rs485_get_stats(rs485_stats_t *stats);
void rs485_uart_irq_entry_isr(void);
void rs485_uart_idle_isr(void);
void rs485_uart_rx_half_isr(void);
void rs485_uart_rx_complete_isr(void);
void rs485_uart_tx_complete_isr(void);
void rs485_uart_error_isr(void);

#endif
