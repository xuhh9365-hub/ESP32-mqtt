#include "led.h"


void led_init(void)
{

    gpio_config_t LED_Config={0} ;



    LED_Config.intr_type = GPIO_INTR_DISABLE;         /* 失能引脚中断 */

    LED_Config.pin_bit_mask = (1ULL << LED_PIN);
    LED_Config.mode = GPIO_MODE_INPUT_OUTPUT;//
    LED_Config.pull_up_en = GPIO_PULLUP_ENABLE;
    LED_Config.pull_down_en = GPIO_PULLDOWN_DISABLE;

    

    gpio_config( &LED_Config);

}