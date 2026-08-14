#ifndef __LED_H__
#define __LED_H__

#include "driver/gpio.h"

#define LED_PIN 1

#define LED_ON()  gpio_set_level(LED_PIN, 0)
#define LED_OFF() gpio_set_level(LED_PIN, 1)


#define LED_TOGGLE() do{gpio_set_level(LED_PIN,!gpio_get_level(LED_PIN));}while(0)



void led_init(void);




#endif

