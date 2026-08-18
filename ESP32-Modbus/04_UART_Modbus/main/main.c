#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "modbus.h"
#include "WIFI_sta.h"
#include "MQTT_CLIENT_APP.h"
#include "cJSON.h"




// 声明队列句柄
static QueueHandle_t sensor1_queue = NULL;
//
static void on_mqtt_control_received(const char *topic, int topic_len, const char *data, int data_len)
{
    static const char *TW="CONTROL";
    // 判断是否是发往控制主题的消息
    if (strncmp(topic, MQTT_TOPIC_CONTROL, topic_len) == 0) 
    {
        ESP_LOGI(TW, "收到云端控制指令: %.*s", data_len, data);
        cJSON *root = cJSON_ParseWithLength(data, data_len);
        if (root == NULL) {
            ESP_LOGE(TW, "JSON 格式解析失败");
            return;
        }
        // 1. 如果包含 temp_limit 字段，写入 Modbus 温度上限
        cJSON *item_temp = cJSON_GetObjectItem(root, "temp_limit");
        if (cJSON_IsNumber(item_temp)) {
            modbus_master_write_temp_limit((float)item_temp->valuedouble);
        }
        // 2. 如果包含 humi_limit 字段，写入 Modbus 湿度下限
        cJSON *item_humi = cJSON_GetObjectItem(root, "humi_limit");
        if (cJSON_IsNumber(item_humi)) {
            modbus_master_write_humi_limit((float)item_humi->valuedouble);
        }
        // 3. 如果包含 status 字段，写入 Modbus 设备状态
        cJSON *item_status = cJSON_GetObjectItem(root, "status");
        if (cJSON_IsNumber(item_status)) {
            modbus_master_write_status((uint16_t)item_status->valueint);
        }
        cJSON_Delete(root);
    }
}


static void modbus_task(void *pvParameters)
{
    gateway_data_t data = {0};
    int consecutive_fail = 0;

    while (1)
    {
        if (modbus_master_read_all(&data) == ESP_OK) {
            consecutive_fail = 0;
            printf("\n--- 从站数据 ---\n");
            printf("温度: %.1f ℃\n", data.temperature);
            printf("湿度: %.1f %%\n", data.humidity);
            printf("状态: %d\n", data.status);
            printf("温度上限: %.1f ℃\n", data.temp_limit);
            printf("湿度下限: %.1f %%\n", data.humi_limit);


              BaseType_t status = xQueueSend(sensor1_queue, &data, 0);
            if (status == pdPASS) {
                ESP_LOGI("queue", "Queue ok");
            } else {
                ESP_LOGW("queue", "Queue full, failed to send data");
            }
        } 
        
        
        else {
            consecutive_fail++;
            if (consecutive_fail >= 3) {
                printf("⚠️ 从站可能已断线（连续 %d 轮失败）\n", consecutive_fail);
            } else {
                printf("读取从站失败 (%d/3)\n", consecutive_fail);
            }
        }
      

        vTaskDelay(pdMS_TO_TICKS(1000)); // 每秒发送一次


       
    }
}


static void MQTT_task(void *pvParameters)
{
     gateway_data_t data = {0};
    
   
    while(1)
    {
            if (xQueueReceive(sensor1_queue, &data, portMAX_DELAY) == pdPASS)
            {

                    if (mqtt_app_is_connected())
                {
                    char payload[128];

                    /* 发布DHT11传感器数据 */
                    snprintf(payload, sizeof(payload),
                            "{\"temperature\":%.1f,\"humidity\":%.1f,\"status\":%d,\"temp_limit\":%.2f,\"humi_limit\":%.2f}",
                            data.temperature, data.humidity,data.status,data.temp_limit,data.humi_limit);
                    mqtt_app_publish(MQTT_TOPIC_SENSOR, payload, 1);


                  

                  
                }


           
            }


    }




}

static void LED_task(void *pvParameters)
{
    while(1)
    {
        LED_TOGGLE();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    led_init();             /* 初始化LED */
    modbus_master_init();   /* 初始化 Modbus Master */

    sensor1_queue=xQueueCreate(1,sizeof(gateway_data_t));
    if(sensor1_queue==NULL)
    {
        ESP_LOGE("QUEUE", "Failed to create queue");
        return;

    }


    esp_err_t ret = wifi_sta_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE("WiFi", "WiFi 连接失败，系统停止");
        return;
    }
      /* 2. 启动MQTT客户端并连接到EMQX Broker */
      ret = mqtt_app_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE("MQTT", "MQTT 启动失败");
        return;
    }
    mqtt_app_set_data_callback(on_mqtt_control_received);

    xTaskCreate(LED_task, "LED_task", 4096, NULL, 1, NULL);
    xTaskCreate(modbus_task, "modbus_task", 4096, NULL, 4, NULL);

     xTaskCreate(MQTT_task, "MQTT_task", 4096, NULL, 3, NULL);


}
    