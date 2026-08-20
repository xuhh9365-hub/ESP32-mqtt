#ifndef __RS485_H__
#define __RS485_H__

#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>


#define RS485_UART_NUM UART_NUM_1

#define RS485_UART_TX_PIN GPIO_NUM_4
#define RS485_UART_RX_PIN GPIO_NUM_5
#define RS485_UART_RTS_PIN GPIO_NUM_6


#define BUF_SIZE (1024)


void rs485_init(uint32_t baudrate);




bool modbus_read_holding_registers( uint8_t slave_addr, uint16_t start_addr, uint16_t quantity, uint16_t *registers);

#endif

