#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "modbus.h"   // 新增




static void modbus_task(void *pvParameters)
{
    static gateway_data_t data = {0};
    while (1)
    {
        if (modbus_master_read_all(&data) == ESP_OK) {
            printf("\n--- 从站数据 ---\n");
            printf("温度: %.1f ℃\n", data.temperature);
            printf("湿度: %.1f %%\n", data.humidity);
            printf("状态: %d\n", data.status);
            printf("温度上限: %.1f ℃\n", data.temp_limit);
            printf("湿度下限: %.1f %%\n", data.humi_limit);
        } else {
            printf("读取从站失败\n");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}



static void LED_task(void *pvParameters)
{
    while(1)
    {
        LED_TOGGLE();
        vTaskDelay(500/portTICK_PERIOD_MS);
    }
}



void app_main(void)
{


     led_init();             /* 初始化LED */
 
    
  
    modbus_master_init();   // 替代原来的 rs485_init()
    xTaskCreate(LED_task, "LED_task", 4096, NULL, 2, NULL);
    xTaskCreate(modbus_task, "modbus_task", 4096, NULL, 3, NULL);
       


}
