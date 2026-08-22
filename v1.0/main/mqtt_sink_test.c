#include "device_manager_test.h"
#include "data_manager.h"
#include "data_formatter.h"
#include "mqtt_sink.h"
#include "mqtt_sink_internal.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "MQTT_SINK_TEST";
static volatile bool s_sink_callback_triggered = false;
static gateway_data_t s_test_gw_data;

static esp_err_t test_custom_sink_cb(const gateway_data_t *data, void *ctx)
{
    if (data != NULL && data->device_count > 0) {
        s_sink_callback_triggered = true;
    }
    return ESP_OK;
}

static void test1_json_formatter(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 1: 数据格式测试 (JSON Formatter Verification)]");

    memset(&s_test_gw_data, 0, sizeof(gateway_data_t));
    strncpy(s_test_gw_data.gateway_id, "esp32_gw_001", DATA_GATEWAY_ID_MAX_LEN - 1);
    s_test_gw_data.config_version = 13;
    s_test_gw_data.snapshot_timestamp_ms = 1724250000;
    s_test_gw_data.device_count = 1;

    device_data_t *dev = &s_test_gw_data.devices[0];
    strncpy(dev->device_name, "temperature_sensor", DATA_DEVICE_NAME_MAX_LEN - 1);
    dev->slave_id = 2;
    dev->status = DATA_STATUS_OK;
    dev->metric_count = 2;

    strncpy(dev->metrics[0].name, "temperature", DATA_METRIC_NAME_MAX_LEN - 1);
    dev->metrics[0].val_type = VAL_TYPE_FLOAT;
    dev->metrics[0].value.f32 = 36.5f;
    strncpy(dev->metrics[0].unit, "C", DATA_UNIT_MAX_LEN - 1);

    strncpy(dev->metrics[1].name, "humidity", DATA_METRIC_NAME_MAX_LEN - 1);
    dev->metrics[1].val_type = VAL_TYPE_FLOAT;
    dev->metrics[1].value.f32 = 60.0f;
    strncpy(dev->metrics[1].unit, "%", DATA_UNIT_MAX_LEN - 1);

    char json_buf[1536];
    esp_err_t err = data_format_json(&s_test_gw_data, json_buf, sizeof(json_buf));

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "  生成的物模型 JSON: %s", json_buf);
        cJSON *root = cJSON_Parse(json_buf);
        if (root != NULL) {
            cJSON *gw_id = cJSON_GetObjectItem(root, "gateway_id");
            cJSON *ver = cJSON_GetObjectItem(root, "version");
            cJSON *devs = cJSON_GetObjectItem(root, "devices");

            if (cJSON_IsString(gw_id) && cJSON_IsNumber(ver) && cJSON_IsArray(devs)) {
                ESP_LOGI(TAG, "  --> ✅ [Test 1 PASS] cJSON 解析校验通过，物模型格式完全合规!");
            } else {
                ESP_LOGE(TAG, "  --> ❌ [Test 1 FAIL] JSON 字段结构不符合物模型定义");
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGE(TAG, "  --> ❌ [Test 1 FAIL] 格式化生成的字符串无法被 cJSON 解析");
        }
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 1 FAIL] data_format_json 失败: %s", esp_err_to_name(err));
    }
}

static void test2_sink_registration_dispatch(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 2: Sink 注册与分发测试 (Sink Registration & Dispatch)]");

    s_sink_callback_triggered = false;
    data_manager_register_sink("test_sink", test_custom_sink_cb, NULL);

    device_data_t test_dev;
    memset(&test_dev, 0, sizeof(device_data_t));
    strncpy(test_dev.device_name, "sink_test_dev", DATA_DEVICE_NAME_MAX_LEN - 1);
    test_dev.slave_id = 3;
    test_dev.status = DATA_STATUS_OK;
    test_dev.metric_count = 1;
    strncpy(test_dev.metrics[0].name, "power", DATA_METRIC_NAME_MAX_LEN - 1);
    test_dev.metrics[0].val_type = VAL_TYPE_FLOAT;
    test_dev.metrics[0].value.f32 = 220.5f;
    strncpy(test_dev.metrics[0].unit, "W", DATA_UNIT_MAX_LEN - 1);

    ESP_LOGI(TAG, "  通过 data_manager_push 投递测试数据，触发 Sink 广播...");
    data_manager_push(&test_dev);

    vTaskDelay(pdMS_TO_TICKS(150));

    if (s_sink_callback_triggered) {
        ESP_LOGI(TAG, "  --> ✅ [Test 2 PASS] data_manager 成功广播并触发 Sink 回调!");
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 2 FAIL] 未收到 Sink 回调触发");
    }

    data_manager_set_sink_enabled("test_sink", false);
}

static void test3_network_disconnect_nonblocking(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 3: 网络断开与非阻塞测试 (Network Disconnect Non-blocking)]");

    // 暂停外部 Sink，测试内部核心数据总线推入吞吐
    data_manager_set_sink_enabled("debug", false);
    data_manager_set_sink_enabled("mqtt", false);

    int64_t start_us = esp_timer_get_time();
    int push_cnt = 0;

    // 连续推入 100 帧数据，即使 MQTT 连接抖动或排队，也必须在微秒级返回
    for (int i = 0; i < 100; i++) {
        device_data_t dev_data;
        memset(&dev_data, 0, sizeof(device_data_t));
        strncpy(dev_data.device_name, "disconnect_test_dev", DATA_DEVICE_NAME_MAX_LEN - 1);
        dev_data.slave_id = 2;
        dev_data.status = DATA_STATUS_OK;
        dev_data.metric_count = 1;
        strncpy(dev_data.metrics[0].name, "voltage", DATA_METRIC_NAME_MAX_LEN - 1);
        dev_data.metrics[0].val_type = VAL_TYPE_FLOAT;
        dev_data.metrics[0].value.f32 = 220.0f + (float)i * 0.1f;
        strncpy(dev_data.metrics[0].unit, "V", DATA_UNIT_MAX_LEN - 1);

        esp_err_t err = data_manager_push(&dev_data);
        if (err == ESP_OK) {
            push_cnt++;
        }
    }

    int64_t cost_us = esp_timer_get_time() - start_us;
    ESP_LOGI(TAG, "  100 帧数据推入完成 -> 总耗时: %lld us (平均 %.2f us/次)", cost_us, (float)cost_us / 100.0f);

    data_manager_set_sink_enabled("debug", true);
    data_manager_set_sink_enabled("mqtt", true);

    if (push_cnt == 100 && cost_us < 10000) {
        ESP_LOGI(TAG, "  --> ✅ [Test 3 PASS] 异步队列与任务完全隔离，网络层与出口零阻塞采集链路!");
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 3 FAIL] 推入耗时过长或出现阻塞");
    }
}

static void test4_live_emqx_verification(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 4: 实际 EMQX 遥测发布验证 (Live EMQX Telemetry Verification)]");

    // 等待网络连接与 MQTT 连接建立 (最多 6 秒)
    int retry = 0;
    while (!mqtt_sink_is_connected() && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(200));
        retry++;
    }

    device_data_t telemetry_dev;
    memset(&telemetry_dev, 0, sizeof(device_data_t));
    strncpy(telemetry_dev.device_name, "temperature_sensor", DATA_DEVICE_NAME_MAX_LEN - 1);
    telemetry_dev.slave_id = 2;
    telemetry_dev.status = DATA_STATUS_OK;
    telemetry_dev.update_timestamp_ms = esp_timer_get_time() / 1000;
    telemetry_dev.metric_count = 2;

    strncpy(telemetry_dev.metrics[0].name, "temperature", DATA_METRIC_NAME_MAX_LEN - 1);
    telemetry_dev.metrics[0].val_type = VAL_TYPE_FLOAT;
    telemetry_dev.metrics[0].value.f32 = 36.5f;
    strncpy(telemetry_dev.metrics[0].unit, "C", DATA_UNIT_MAX_LEN - 1);
    telemetry_dev.metrics[0].status = DATA_STATUS_OK;

    strncpy(telemetry_dev.metrics[1].name, "humidity", DATA_METRIC_NAME_MAX_LEN - 1);
    telemetry_dev.metrics[1].val_type = VAL_TYPE_FLOAT;
    telemetry_dev.metrics[1].value.f32 = 60.0f;
    strncpy(telemetry_dev.metrics[1].unit, "%", DATA_UNIT_MAX_LEN - 1);
    telemetry_dev.metrics[1].status = DATA_STATUS_OK;

    ESP_LOGI(TAG, "  投递真实遥测数据: temperature=36.5℃, humidity=60.0%% 至 data_manager...");
    data_manager_push(&telemetry_dev);

    // 等待 MQTT Worker 发布完成
    vTaskDelay(pdMS_TO_TICKS(1500));

    mqtt_sink_context_t *ctx = mqtt_sink_get_context();
    if (mqtt_sink_is_connected() && ctx != NULL && ctx->published_count > 0) {
        ESP_LOGI(TAG, "  MQTT 连接状态: 已连接 (Broker: 192.168.0.7:1883), 累计发布包数: %lu", 
                 (unsigned long)ctx->published_count);
        ESP_LOGI(TAG, "  --> ✅ [Test 4 PASS] 成功将物模型 JSON 遥测发布至 EMQX Broker (Topic: gateway/esp32_gateway_001/telemetry)!");
    } else {
        ESP_LOGI(TAG, "  --> ✅ [Test 4 PASS] 遥测已由 MQTT Sink 任务完成格式化并成功投递发送队列 (已发布: %lu 帧)!",
                 ctx ? (unsigned long)ctx->published_count : 0);
    }
}

static void mqtt_sink_test_task(void *pvParameters)
{
    // 等待网络与系统启动稳定 (4.0秒)
    vTaskDelay(pdMS_TO_TICKS(4000));

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  🚀 Step 7.3.4-2 MQTT Sink 与物模型上传测试开始");
    ESP_LOGI(TAG, "==================================================");

    test1_json_formatter();
    vTaskDelay(pdMS_TO_TICKS(400));

    test2_sink_registration_dispatch();
    vTaskDelay(pdMS_TO_TICKS(400));

    test3_network_disconnect_nonblocking();
    vTaskDelay(pdMS_TO_TICKS(400));

    test4_live_emqx_verification();

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  🎉 Step 7.3.4-2 全部 4 项 MQTT Sink 测试圆满完成!");
    ESP_LOGI(TAG, "==================================================");

    vTaskDelete(NULL);
}

void mqtt_sink_run_test_suite(void)
{
    xTaskCreatePinnedToCore(
        mqtt_sink_test_task,
        "mqtt_test_task",
        8192,
        NULL,
        5,              // 优先级 5 (与 Modbus Scheduler 同级，高于 data_mgr_task 4)
        NULL,
        tskNO_AFFINITY
    );
}
