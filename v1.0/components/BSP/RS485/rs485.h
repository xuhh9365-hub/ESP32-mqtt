#ifndef __RS485_H__
#define __RS485_H__

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/*                   RS485 硬件物理引脚与 UART 端口定义                      */
/* ========================================================================= */

#define RS485_UART_NUM          UART_NUM_1

#define RS485_UART_TX_PIN       GPIO_NUM_4
#define RS485_UART_RX_PIN       GPIO_NUM_5
#define RS485_UART_RTS_PIN      GPIO_NUM_6

#define RS485_DEFAULT_BAUDRATE  115200
#define RS485_BUF_SIZE          (1024)

/**
 * @brief RS485 纯硬件辅助初始化 (保留接口)
 * @note  在当前统一架构中，UART1 驱动生命周期与半双工控制由官方 esp-modbus 统一管理。
 *        本函数保留作为硬件引脚/参数打印与辅助配置接口，不重复安装底层 UART 驱动。
 */
void rs485_init(uint32_t baudrate);

#ifdef __cplusplus
}
#endif

#endif /* __RS485_H__ */
