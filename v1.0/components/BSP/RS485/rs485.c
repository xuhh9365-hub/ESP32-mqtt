#include "rs485.h"
#include "esp_log.h"

static const char *TAG = "RS485_DRIVER";

void rs485_init(uint32_t baudrate)
{
    // 在当前统一架构中，UART1 的驱动安装、引脚绑定与半双工模式均由 modbus_master (esp-modbus) 统一接管。
    // 此处保留此函数用于硬件参数校验或兼容性预留，避免重复调用 uart_driver_install 导致驱动冲突。
    ESP_LOGI(TAG, "RS485 硬件引脚配置: TX=GPIO%d, RX=GPIO%d, RTS=GPIO%d (UART%d, 波特率: %lu)",
             RS485_UART_TX_PIN, RS485_UART_RX_PIN, RS485_UART_RTS_PIN, RS485_UART_NUM, (unsigned long)baudrate);
}