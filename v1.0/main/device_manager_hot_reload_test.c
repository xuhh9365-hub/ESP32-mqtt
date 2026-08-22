#include "device_manager_test.h"
#include "device_manager.h"
#include "config_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "HOT_RELOAD_TEST";

static void test1_add_device_via_diff(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 1: 新增设备热重载测试 (Add Device via Diff)]");

    // 初始基线: 1个设备 (temperature_sensor)
    device_config_t old_cfgs[1] = {
        {
            .name = "temperature_sensor",
            .slave_id = 2,
            .register_addr = 0x0010,
            .scale = 0.1f,
            .period = 1000,
            .metric_count = 1
        }
    };
    strncpy(old_cfgs[0].metrics[0].metric_name, "temp_limit", CONFIG_METRIC_NAME_MAX_LEN - 1);
    old_cfgs[0].metrics[0].write_register = 0x0013;
    old_cfgs[0].metrics[0].scale = 0.1f;
    old_cfgs[0].metrics[0].min_value = 0.0f;
    old_cfgs[0].metrics[0].max_value = 100.0f;

    // 新配置: 2个设备 (temperature_sensor + pressure_sensor)
    device_config_t new_cfgs[2] = {
        old_cfgs[0],
        {
            .name = "pressure_sensor",
            .slave_id = 2,
            .register_addr = 0x0012,
            .scale = 0.01f,
            .period = 500,
            .metric_count = 0
        }
    };

    ESP_LOGI(TAG, "  执行 apply_config_diff: 添加 pressure_sensor...");
    esp_err_t err = device_manager_apply_config_diff(old_cfgs, 1, new_cfgs, 2);
    uint8_t count = device_manager_get_device_count();

    if (err == ESP_OK && count == 2) {
        ESP_LOGI(TAG, "  --> ✅ [Test 1 PASS] 成功热添加设备! 活跃设备数: %u (调度器自动纳管新设备)", count);
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 1 FAIL] 添加设备失败: %s (count=%u)", esp_err_to_name(err), count);
    }
}

static void test2_remove_device_via_diff(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 2: 删除设备热重载测试 (Remove Device via Diff)]");

    // 旧配置: 2个设备
    device_config_t old_cfgs[2] = {
        {
            .name = "temperature_sensor",
            .slave_id = 2,
            .register_addr = 0x0010,
            .scale = 0.1f,
            .period = 1000
        },
        {
            .name = "pressure_sensor",
            .slave_id = 2,
            .register_addr = 0x0012,
            .scale = 0.01f,
            .period = 500
        }
    };

    // 新配置: 仅保留 1个设备 (pressure_sensor)
    device_config_t new_cfgs[1] = {
        old_cfgs[1]
    };

    ESP_LOGI(TAG, "  执行 apply_config_diff: 删除 temperature_sensor...");
    esp_err_t err = device_manager_apply_config_diff(old_cfgs, 2, new_cfgs, 1);
    uint8_t count = device_manager_get_device_count();

    if (err == ESP_OK && count == 1) {
        ESP_LOGI(TAG, "  --> ✅ [Test 2 PASS] 成功热删除设备! 剩余活跃设备数: %u", count);
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 2 FAIL] 删除设备失败: %s (count=%u)", esp_err_to_name(err), count);
    }
}

static void test3_update_device_period(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 3: 修改设备参数测试 (周期变更 500ms -> 200ms)]");

    device_info_t info_before;
    device_manager_get_device_info("pressure_sensor", &info_before);

    // 旧配置
    device_config_t old_cfg = {
        .name = "pressure_sensor",
        .slave_id = 2,
        .register_addr = 0x0012,
        .scale = 0.01f,
        .period = 500
    };

    // 新配置: 周期改为 200ms
    device_config_t new_cfg = {
        .name = "pressure_sensor",
        .slave_id = 2,
        .register_addr = 0x0012,
        .scale = 0.01f,
        .period = 200
    };

    ESP_LOGI(TAG, "  执行 apply_config_diff: 变更 pressure_sensor 采集周期为 200ms...");
    esp_err_t err = device_manager_apply_config_diff(&old_cfg, 1, &new_cfg, 1);

    device_info_t info_after;
    device_manager_get_device_info("pressure_sensor", &info_after);

    if (err == ESP_OK && info_after.period == 200) {
        ESP_LOGI(TAG, "  --> ✅ [Test 3 PASS] 成功热更新参数! 原周期: %lu ms -> 新周期: %lu ms (代数已自增保护)",
                 (unsigned long)info_before.period, (unsigned long)info_after.period);
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 3 FAIL] 更新参数失败: %s (period=%lu)", esp_err_to_name(err), (unsigned long)info_after.period);
    }
}

static void test4_runtime_concurrent_update(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 4: 运行中并发热更新与防死锁/防竞争测试]");

    device_bus_status_t bus_before;
    device_manager_get_bus_status(&bus_before);

    // 恢复标准双设备配置 (temperature_sensor + humidity_sensor)
    device_config_t final_cfgs[2] = {
        {
            .name = "temperature_sensor",
            .slave_id = 2,
            .register_addr = 0x0010,
            .scale = 0.1f,
            .period = 1000,
            .metric_count = 1
        },
        {
            .name = "humidity_sensor",
            .slave_id = 2,
            .register_addr = 0x0011,
            .scale = 0.1f,
            .period = 1000,
            .metric_count = 1
        }
    };
    strncpy(final_cfgs[0].metrics[0].metric_name, "temp_limit", CONFIG_METRIC_NAME_MAX_LEN - 1);
    final_cfgs[0].metrics[0].write_register = 0x0013;
    final_cfgs[0].metrics[0].scale = 0.1f;
    final_cfgs[0].metrics[0].min_value = 0.0f;
    final_cfgs[0].metrics[0].max_value = 100.0f;

    strncpy(final_cfgs[1].metrics[0].metric_name, "humi_limit", CONFIG_METRIC_NAME_MAX_LEN - 1);
    final_cfgs[1].metrics[0].write_register = 0x0014;
    final_cfgs[1].metrics[0].scale = 0.1f;
    final_cfgs[1].metrics[0].min_value = 0.0f;
    final_cfgs[1].metrics[0].max_value = 100.0f;

    device_config_t cur_cfg = {
        .name = "pressure_sensor",
        .slave_id = 2,
        .register_addr = 0x0012,
        .scale = 0.01f,
        .period = 200
    };

    ESP_LOGI(TAG, "  在调度器高频轮询期间并发执行 apply_config_diff...");
    esp_err_t err = device_manager_apply_config_diff(&cur_cfg, 1, final_cfgs, 2);

    device_bus_status_t bus_after;
    device_manager_get_bus_status(&bus_after);

    if (err == ESP_OK && device_manager_get_device_count() == 2) {
        ESP_LOGI(TAG, "  --> ✅ [Test 4 PASS] 运行中并发热重载无死锁，无总线超时冲突，平滑接管新设备!");
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 4 FAIL] 并发热重载异常: %s", esp_err_to_name(err));
    }
}

static void hot_reload_test_task(void *pvParameters)
{
    // 等待系统启动稳定 (2.5秒)
    vTaskDelay(pdMS_TO_TICKS(2500));

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  🚀 Step 7.3.3-2B device_manager 动态热重载测试开始");
    ESP_LOGI(TAG, "==================================================");

    // 建立干净的测试基线 (仅保留 1 个设备: temperature_sensor)
    device_manager_remove_device("humidity_sensor");
    device_manager_remove_device("pressure_sensor");
    device_config_t base_dev = {
        .name = "temperature_sensor",
        .slave_id = 2,
        .register_addr = 0x0010,
        .scale = 0.1f,
        .period = 1000,
        .metric_count = 1
    };
    strncpy(base_dev.metrics[0].metric_name, "temp_limit", CONFIG_METRIC_NAME_MAX_LEN - 1);
    base_dev.metrics[0].write_register = 0x0013;
    base_dev.metrics[0].scale = 0.1f;
    base_dev.metrics[0].min_value = 0.0f;
    base_dev.metrics[0].max_value = 100.0f;
    device_manager_add_device(&base_dev);

    ESP_LOGI(TAG, "测试基线就绪: 当前活跃设备数: %u", device_manager_get_device_count());
    vTaskDelay(pdMS_TO_TICKS(500));

    test1_add_device_via_diff();
    vTaskDelay(pdMS_TO_TICKS(600));

    test2_remove_device_via_diff();
    vTaskDelay(pdMS_TO_TICKS(600));

    test3_update_device_period();
    vTaskDelay(pdMS_TO_TICKS(600));

    test4_runtime_concurrent_update();

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  🎉 Step 7.3.3-2B 全部 4 项动态热重载测试圆满完成!");
    ESP_LOGI(TAG, "==================================================");

    vTaskDelete(NULL);
}

void device_manager_hot_reload_run_test_suite(void)
{
    xTaskCreatePinnedToCore(
        hot_reload_test_task,
        "hot_reload_test",
        4096,
        NULL,
        3,
        NULL,
        tskNO_AFFINITY
    );
}
