#ifndef __UART_H__
#define __UART_H__

#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>


#define UART_NUM UART_NUM_0

#define UART_TX_PIN GPIO_NUM_43
#define UART_RX_PIN GPIO_NUM_44


#define BUF_SIZE (1024)





void usart_init(uint32_t baudrate);


#endif

