#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led.h"
#include "config_manager.h"
#include "data_manager.h"
#include "debug_sink.h"
#include "mqtt_sink.h"
#include "modbus.h"
#include "device_manager.h"
#include "control_manager.h"
#include "web_server.h"
#include "wifi_sta.h"
#include "device_manager_test.h"

static const char *TAG = "APP_MAIN";

static void LED_task(void *pvParameters)
{
    while (1)
    {
        LED_TOGGLE();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "================ ESP32-S3 工业网关启动 (全链路云边端协同) ================");

    // 1. 基础硬件指示灯初始化
    led_init();
    xTaskCreate(LED_task, "LED_task", 2048, NULL, 1, NULL);

    // 2. 配置管理系统初始化 (加载内置默认配置、测点映射与设备列表)
    config_manager_init();

    // 3. 数据出口管理中枢初始化并启动
    data_manager_init();
    data_manager_start();

    // 4. 注册本地调试出口 Debug Sink
    debug_sink_init();

    // 5. 通用 Modbus Master 协议栈初始化 (波特率 115200)
    modbus_master_init();

    // 6. 设备管理器初始化并启动集中式采集调度 (modbus_sched_task)
    device_manager_init();
    device_manager_start();

    // 7. 下行控制中枢初始化并启动异步工作者任务 (control_task)
    control_manager_init();
    control_manager_start();

    // 8. 启动网络连接 (WiFi STA 模式)
    esp_err_t ret = wifi_sta_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi 连接失败，系统将在离线模式下继续运行设备数据采集与本地缓存");
    }

    // 9. 启动本地 HTTP Web 配置服务器 (端口 80, 提供监控看板与 REST API)
    web_server_init();

    // 10. 启动 MQTT Sink 适配器 (向 data_manager 注册 Sink 并启动后台发布任务)
    mqtt_sink_config_t mqtt_cfg = {
        .broker_uri = "mqtt://192.168.0.7:1883",
        .topic      = "gateway/esp32_gateway_001/telemetry",
        .client_id  = "esp32_gateway_001"
    };
    mqtt_sink_init(&mqtt_cfg);
    mqtt_sink_start();

    ESP_LOGI(TAG, "================ 工业网关核心服务全链路已就绪 ================");
}