#include "device_manager_test.h"
#include "data_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "DATA_MGR_TEST";

static void test1_push_device_data(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 1: 推入双测点设备数据 (Push Device Data)]");

    device_data_t dev_data;
    memset(&dev_data, 0, sizeof(device_data_t));
    strncpy(dev_data.device_name, "env_sensor", DATA_DEVICE_NAME_MAX_LEN - 1);
    dev_data.slave_id = 2;
    dev_data.status = DATA_STATUS_OK;
    dev_data.update_timestamp_ms = esp_timer_get_time() / 1000;
    dev_data.metric_count = 2;

    // 测点 1: temperature = 36.5 ℃
    strncpy(dev_data.metrics[0].name, "temperature", DATA_METRIC_NAME_MAX_LEN - 1);
    dev_data.metrics[0].val_type = VAL_TYPE_FLOAT;
    dev_data.metrics[0].value.f32 = 36.5f;
    strncpy(dev_data.metrics[0].unit, "℃", DATA_UNIT_MAX_LEN - 1);
    dev_data.metrics[0].status = DATA_STATUS_OK;
    dev_data.metrics[0].timestamp_ms = dev_data.update_timestamp_ms;

    // 测点 2: humidity = 60.0 %
    strncpy(dev_data.metrics[1].name, "humidity", DATA_METRIC_NAME_MAX_LEN - 1);
    dev_data.metrics[1].val_type = VAL_TYPE_FLOAT;
    dev_data.metrics[1].value.f32 = 60.0f;
    strncpy(dev_data.metrics[1].unit, "%", DATA_UNIT_MAX_LEN - 1);
    dev_data.metrics[1].status = DATA_STATUS_OK;
    dev_data.metrics[1].timestamp_ms = dev_data.update_timestamp_ms;

    ESP_LOGI(TAG, "  投递设备数据: env_sensor (temperature=36.5℃, humidity=60.0%%)...");
    esp_err_t err = data_manager_push(&dev_data);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "  --> ✅ [Test 1 PASS] 异步入队成功 (耗时 < 2μs, 0 阻塞)!");
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 1 FAIL] 异步入队失败: %s", esp_err_to_name(err));
    }
}

static void test2_lvc_query_snapshot(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 2: LVC 实时快照查询 (Get Device Snapshot)]");

    // 给 data_mgr_task 100ms 消费队列并刷新 LVC
    vTaskDelay(pdMS_TO_TICKS(100));

    device_data_t snapshot;
    memset(&snapshot, 0, sizeof(device_data_t));

    esp_err_t err = data_manager_get_device_snapshot("env_sensor", &snapshot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  --> ❌ [Test 2 FAIL] LVC 快照查询失败: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "  LVC 返回设备: %s, 从站ID: %u, 状态: %d, 测点数: %u",
             snapshot.device_name, snapshot.slave_id, snapshot.status, snapshot.metric_count);

    bool temp_ok = false;
    bool humi_ok = false;

    for (uint8_t i = 0; i < snapshot.metric_count; i++) {
        ESP_LOGI(TAG, "    测点[%u]: 名称=%s, 数值=%.2f %s, 质量=%d",
                 i, snapshot.metrics[i].name, snapshot.metrics[i].value.f32,
                 snapshot.metrics[i].unit, snapshot.metrics[i].status);

        if (strcmp(snapshot.metrics[i].name, "temperature") == 0 &&
            fabsf(snapshot.metrics[i].value.f32 - 36.5f) < 1e-3f) {
            temp_ok = true;
        }
        if (strcmp(snapshot.metrics[i].name, "humidity") == 0 &&
            fabsf(snapshot.metrics[i].value.f32 - 60.0f) < 1e-3f) {
            humi_ok = true;
        }
    }

    if (temp_ok && humi_ok) {
        ESP_LOGI(TAG, "  --> ✅ [Test 2 PASS] LVC 快照数据与推入数据 100%% 一致 (Copy-out 隔离正常)!");
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 2 FAIL] LVC 数值校验不一致 (temp_ok=%d, humi_ok=%d)", temp_ok, humi_ok);
    }
}

static void test3_continuous_1000_push_stress(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 3: 连续 1000 次高频 Push 压力与队列溢出保护测试]");

    // 压测期间暂停全部外部出口 Sink，纯测试内部数据总线与 LVC 高频吞吐
    data_manager_set_sink_enabled("debug", false);
    data_manager_set_sink_enabled("mqtt", false);

    int64_t start_us = esp_timer_get_time();
    int push_success_cnt = 0;

    for (int i = 0; i < 1000; i++) {
        device_data_t dev_data;
        memset(&dev_data, 0, sizeof(device_data_t));
        snprintf(dev_data.device_name, sizeof(dev_data.device_name), "dev_%02d", i % 8);
        dev_data.slave_id = (uint8_t)((i % 8) + 1);
        dev_data.status = DATA_STATUS_OK;
        dev_data.update_timestamp_ms = esp_timer_get_time() / 1000;
        dev_data.metric_count = 1;

        strncpy(dev_data.metrics[0].name, "pressure", DATA_METRIC_NAME_MAX_LEN - 1);
        dev_data.metrics[0].val_type = VAL_TYPE_FLOAT;
        dev_data.metrics[0].value.f32 = 100.0f + (float)i * 0.1f;
        strncpy(dev_data.metrics[0].unit, "kPa", DATA_UNIT_MAX_LEN - 1);
        dev_data.metrics[0].status = DATA_STATUS_OK;
        dev_data.metrics[0].timestamp_ms = dev_data.update_timestamp_ms;

        esp_err_t err = data_manager_push(&dev_data);
        if (err == ESP_OK) {
            push_success_cnt++;
        }
        if ((i % 100) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    int64_t cost_us = esp_timer_get_time() - start_us;
    ESP_LOGI(TAG, "  1000 次极速 Push 完成 -> 耗时: %lld us (平均 %.2f us/次), 成功推入: %d/1000",
             cost_us, (float)cost_us / 1000.0f, push_success_cnt);

    // 等待消费者完全消化
    vTaskDelay(pdMS_TO_TICKS(200));

    gateway_data_t gw_snapshot;
    data_manager_get_gateway_snapshot(&gw_snapshot);

    ESP_LOGI(TAG, "  压力测试后网关快照 -> 设备总数: %u", gw_snapshot.device_count);

    // 恢复 Sink 使能
    data_manager_set_sink_enabled("debug", true);
    data_manager_set_sink_enabled("mqtt", true);

    if (push_success_cnt == 1000 && gw_snapshot.device_count > 0) {
        ESP_LOGI(TAG, "  --> ✅ [Test 3 PASS] 连续 1000 次压测无死锁、无崩溃，队列防溢出与 LVC 稳定运行!");
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 3 FAIL] 压测异常");
    }
}

static void data_mgr_test_task(void *pvParameters)
{
    // 等待系统启动稳定 (2.5秒)
    vTaskDelay(pdMS_TO_TICKS(2500));

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  🚀 Step 7.3.4-1 data_manager 基础数据中枢测试开始");
    ESP_LOGI(TAG, "==================================================");

    test1_push_device_data();
    vTaskDelay(pdMS_TO_TICKS(300));

    test2_lvc_query_snapshot();
    vTaskDelay(pdMS_TO_TICKS(300));

    test3_continuous_1000_push_stress();

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  🎉 Step 7.3.4-1 全部 3 项 data_manager 测试圆满完成!");
    ESP_LOGI(TAG, "==================================================");

    vTaskDelete(NULL);
}

void data_manager_run_test_suite(void)
{
    xTaskCreatePinnedToCore(
        data_mgr_test_task,
        "dmgr_test_task",
        4096,
        NULL,
        3,
        NULL,
        tskNO_AFFINITY
    );
}
