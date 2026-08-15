#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "key.h"
#include "rs485.h"






static void LED_task(void *pvParameters)
{
    while(1)
    {
        LED_TOGGLE();
        vTaskDelay(500/portTICK_PERIOD_MS);
    }
}


static void uart_task(void *pvParameters)
{
   

   uint16_t registers[10] = {0};
   while (1)
   {
    bool success = modbus_read_holding_registers(0x02, 0x0010, 5, registers);

        if (success)
        {
            
            
               printf("温度：%.1f ℃\n", registers[0] / 10.0);
               printf("湿度：%.1f %%\n", registers[1] / 10.0);
               printf("状态：%d\n", registers[2]);
               printf("温度上限：%.1f ℃\n", registers[3] / 10.0);
               printf("湿度下限：%.1f %%\n", registers[4] / 10.0);
            
        }
        else
        {
            printf("Modbus读取失败\n");
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // 延时1秒
   }
   

 
}


void app_main(void)
{


     led_init();             /* 初始化LED */
    rs485_init(115200); 
    
    unsigned char data[BUF_SIZE] = {0};
    xTaskCreate(LED_task, "LED_task", 4096, NULL, 2, NULL);

    xTaskCreate(uart_task, "uart_task", 4096, NULL, 3, NULL);

       


}
