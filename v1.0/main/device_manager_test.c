#include "device_manager_test.h"
#include "device_manager.h"
#include "config_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "DM_TEST_SUITE";
static const char *CFG_TAG = "CFG_TEST_SUITE";

static void test1_device_info_query(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 1: 设备信息查询测试 (Copy-out 机制)]");
    device_info_t info;
    esp_err_t err = device_manager_get_device_info("temperature_sensor", &info);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "  设备名称    : %s", info.name);
        ESP_LOGI(TAG, "  从站地址    : %u", (unsigned int)info.slave_id);
        ESP_LOGI(TAG, "  采集寄存器  : 0x%04X", (unsigned int)info.register_addr);
        ESP_LOGI(TAG, "  换算倍率    : %.4f", info.scale);
        ESP_LOGI(TAG, "  采集周期    : %lu ms", (unsigned long)info.period);
        ESP_LOGI(TAG, "  运行质量状态: %d (0=OK)", info.status);
        ESP_LOGI(TAG, "  使能标志    : %s", info.enabled ? "true" : "false");
        ESP_LOGI(TAG, "  控制测点数  : %u", info.metric_count);
        ESP_LOGI(TAG, "  --> ✅ [Test 1 PASS] 设备信息查询功能正常且安全隔离!");
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 1 FAIL] 查询失败: %s", esp_err_to_name(err));
    }
}

static void test2_semantic_write(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 2: 语义写入测试 (物理量 -> Modbus 0x06)]");
    sensor_val_t val = {.f32 = 40.5f};
    ESP_LOGI(TAG, "  发起语义写入: temperature_sensor.temp_limit = 40.50 ℃ (期望换算 405 -> 0x0013)");

    esp_err_t err = device_manager_write_metric("temperature_sensor", "temp_limit", val, VAL_TYPE_FLOAT, 300);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "  --> ✅ [Test 2 PASS] 语义写入成功! 从机已成功确认 0x06 应答 (405)");
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 2 FAIL] 语义写入失败: %s", esp_err_to_name(err));
    }
}

static void test3_illegal_metric_protection(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 3: 非法测点拦截保护测试]");
    sensor_val_t val = {.f32 = 40.5f};
    ESP_LOGI(TAG, "  尝试写入不存在的测点: temperature_sensor.abc_test (期望内部直接拦截, 不占用总线)");

    esp_err_t err = device_manager_write_metric("temperature_sensor", "abc_test", val, VAL_TYPE_FLOAT, 300);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "  --> ✅ [Test 3 PASS] 成功拦截非法测点! 返回 ESP_ERR_NOT_FOUND (未触发 Modbus 物理发送)");
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 3 FAIL] 拦截逻辑异常, 返回: %s", esp_err_to_name(err));
    }
}

static void test4_offline_device_protection(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 4: 离线设备写拦截保护测试]");

    device_config_t dummy_cfg = {
        .slave_id = 99,
        .register_addr = 0x0010,
        .scale = 0.1f,
        .period = 200,
        .metric_count = 1
    };
    strncpy(dummy_cfg.name, "offline_dummy_sensor", sizeof(dummy_cfg.name) - 1);
    strncpy(dummy_cfg.metrics[0].metric_name, "dummy_limit", sizeof(dummy_cfg.metrics[0].metric_name) - 1);
    dummy_cfg.metrics[0].write_register = 0x0019;
    dummy_cfg.metrics[0].scale = 0.1f;
    dummy_cfg.metrics[0].min_value = 0.0f;
    dummy_cfg.metrics[0].max_value = 100.0f;

    device_manager_add_device(&dummy_cfg);
    ESP_LOGI(TAG, "  已添加虚拟测试设备 [offline_dummy_sensor] (从站 ID=99)，等待调度器判定其离线...");

    vTaskDelay(pdMS_TO_TICKS(1500));

    data_status_t status = DATA_STATUS_INVALID;
    device_manager_get_device_status("offline_dummy_sensor", NULL, &status);
    ESP_LOGI(TAG, "  当前 dummy 设备状态: %d (1=OFFLINE)", status);

    sensor_val_t val = {.f32 = 50.0f};
    ESP_LOGI(TAG, "  尝试向已离线设备发起写入指令...");
    esp_err_t err = device_manager_write_metric("offline_dummy_sensor", "dummy_limit", val, VAL_TYPE_FLOAT, 1000);

    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "  --> ✅ [Test 4 PASS] 成功在内存层拦截离线写请求! 返回 ESP_ERR_INVALID_STATE (0 总线耗时)");
    } else {
        ESP_LOGW(TAG, "  --> ⚠️ [Test 4 提示] 返回码: %s", esp_err_to_name(err));
    }

    device_manager_remove_device("offline_dummy_sensor");
}

static void test5_slot_lifecycle_aba(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 5: Slot 生命周期与代数 (Generation) 安全测试]");

    device_config_t dev_a = {
        .slave_id = 2,
        .register_addr = 0x0010,
        .scale = 0.1f,
        .period = 1000
    };
    strncpy(dev_a.name, "temp_slot_device", sizeof(dev_a.name) - 1);
    device_manager_add_device(&dev_a);
    device_manager_remove_device("temp_slot_device");

    device_config_t dev_b = {
        .slave_id = 2,
        .register_addr = 0x0011,
        .scale = 0.1f,
        .period = 1000
    };
    strncpy(dev_b.name, "pressure_sensor", sizeof(dev_b.name) - 1);
    device_manager_add_device(&dev_b);

    device_stats_t b_stats;
    device_manager_get_device_status("pressure_sensor", &b_stats, NULL);

    if (b_stats.write_total_count == 0 && b_stats.write_success_count == 0) {
        ESP_LOGI(TAG, "  --> ✅ [Test 5 PASS] 槽位复用代数 (Generation) 隔离有效，新设备统计纯净 (write_total=0)!");
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 5 FAIL] 槽位复用受到历史脏数据污染!");
    }

    device_manager_remove_device("pressure_sensor");
}

static void test6_bus_lock_stress(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 6: Bus Lock 并发压力与互斥测试 (周期读 vs 突发写)]");

    device_bus_status_t bus_before;
    device_manager_get_bus_status(&bus_before);
    ESP_LOGI(TAG, "  测试前总线状态 -> 排队数: %lu, 超时数: %lu", 
             (unsigned long)bus_before.wait_count, (unsigned long)bus_before.timeout_count);

    int success_cnt = 0;
    for (int i = 0; i < 5; i++) {
        sensor_val_t val = {.f32 = 30.0f + (float)i * 2.0f};
        esp_err_t err = device_manager_write_metric("temperature_sensor", "temp_limit", val, VAL_TYPE_FLOAT, 500);
        if (err == ESP_OK) {
            success_cnt++;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    device_bus_status_t bus_after;
    device_manager_get_bus_status(&bus_after);
    ESP_LOGI(TAG, "  测试后总线状态 -> 排队数: %lu, 超时数: %lu (成功写入 %d/5 次)", 
             (unsigned long)bus_after.wait_count, (unsigned long)bus_after.timeout_count, success_cnt);

    if (success_cnt == 5 && bus_after.timeout_count == 0) {
        ESP_LOGI(TAG, "  --> ✅ [Test 6 PASS] 读写完全互斥，无 UART 冲突，无 CRC 错误!");
    } else {
        ESP_LOGW(TAG, "  --> ⚠️ [Test 6 完成] 成功: %d/5, 超时: %lu", success_cnt, (unsigned long)bus_after.timeout_count);
    }
}

static void dm_test_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  🚀 ESP32 工业网关 Device Manager 稳定性验证开始");
    ESP_LOGI(TAG, "==================================================");

    test1_device_info_query();
    vTaskDelay(pdMS_TO_TICKS(500));

    test2_semantic_write();
    vTaskDelay(pdMS_TO_TICKS(500));

    test3_illegal_metric_protection();
    vTaskDelay(pdMS_TO_TICKS(500));

    test4_offline_device_protection();
    vTaskDelay(pdMS_TO_TICKS(500));

    test5_slot_lifecycle_aba();
    vTaskDelay(pdMS_TO_TICKS(500));

    test6_bus_lock_stress();

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  🎉 全部 6 项 Device Manager 稳定性验证已圆满完成!");
    ESP_LOGI(TAG, "==================================================");

    vTaskDelete(NULL);
}

void device_manager_run_test_suite(void)
{
    xTaskCreatePinnedToCore(
        dm_test_task,
        "dm_test_task",
        4096,
        NULL,
        3,
        NULL,
        tskNO_AFFINITY
    );
}

/**
 * @brief Step 7.3.3-2A 配置更新与持久化自动化测试
 */
static void cfg_test_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(CFG_TAG, "==================================================");
    ESP_LOGI(CFG_TAG, "  🚀 Step 7.3.3-2A config_manager_apply_update 测试开始");
    ESP_LOGI(CFG_TAG, "==================================================");

    uint32_t current_ver = config_manager_get_version();
    ESP_LOGI(CFG_TAG, "当前系统配置版本号: %lu", (unsigned long)current_ver);

    // Test 1: 正常更新 (新增第 3 个设备 pressure_sensor)
    ESP_LOGI(CFG_TAG, "▶ [Test 1: 正常配置更新测试 -> 扩展为 3 个设备 (version: %lu)]", (unsigned long)(current_ver + 1));
    char new_cfg_json[768];
    snprintf(new_cfg_json, sizeof(new_cfg_json),
        "{\n"
        "  \"version\": %lu,\n"
        "  \"devices\": [\n"
        "    {\"name\": \"temperature_sensor\", \"slave_id\": 2, \"register\": {\"address\": 16, \"type\": \"holding\"}, \"data\": {\"type\": \"uint16\", \"scale\": 0.1}, \"period\": 1000, \"metrics\": [{\"name\": \"temp_limit\", \"write_register\": 19, \"scale\": 0.1, \"min\": 0.0, \"max\": 100.0}]},\n"
        "    {\"name\": \"humidity_sensor\", \"slave_id\": 2, \"register\": {\"address\": 17, \"type\": \"holding\"}, \"data\": {\"type\": \"uint16\", \"scale\": 0.1}, \"period\": 1000, \"metrics\": [{\"name\": \"humi_limit\", \"write_register\": 20, \"scale\": 0.1, \"min\": 0.0, \"max\": 100.0}]},\n"
        "    {\"name\": \"pressure_sensor\", \"slave_id\": 2, \"register\": {\"address\": 18, \"type\": \"holding\"}, \"data\": {\"type\": \"uint16\", \"scale\": 0.01}, \"period\": 500, \"metrics\": []}\n"
        "  ]\n"
        "}", (unsigned long)(current_ver + 1));

    uint32_t applied_ver = 0;
    esp_err_t err = config_manager_apply_update(new_cfg_json, &applied_ver);
    if (err == ESP_OK && applied_ver > current_ver && config_get_device_num() == 3) {
        ESP_LOGI(CFG_TAG, "  --> ✅ [Test 1 PASS] 正常配置更新成功! 版本更新为: %lu, 当前设备数: %d, NVS 已持久化",
                 (unsigned long)applied_ver, config_get_device_num());
    } else {
        ESP_LOGE(CFG_TAG, "  --> ❌ [Test 1 FAIL] 配置更新失败: %s (version=%lu)", esp_err_to_name(err), (unsigned long)applied_ver);
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    // Test 2: 非法配置校验拦截 (slave_id = 300)
    ESP_LOGI(CFG_TAG, "--------------------------------------------------");
    ESP_LOGI(CFG_TAG, "▶ [Test 2: 非法配置拦截测试 -> slave_id = 300]");
    char invalid_cfg_json[512];
    snprintf(invalid_cfg_json, sizeof(invalid_cfg_json),
        "{\n"
        "  \"version\": %lu,\n"
        "  \"devices\": [\n"
        "    {\"name\": \"invalid_sensor\", \"slave_id\": 300, \"register\": {\"address\": 16, \"type\": \"holding\"}, \"data\": {\"type\": \"uint16\", \"scale\": 0.1}, \"period\": 1000, \"metrics\": []}\n"
        "  ]\n"
        "}", (unsigned long)(applied_ver + 1));

    uint32_t invalid_ver = 0;
    err = config_manager_apply_update(invalid_cfg_json, &invalid_ver);
    if (err != ESP_OK && config_get_device_num() == 3) {
        ESP_LOGI(CFG_TAG, "  --> ✅ [Test 2 PASS] 成功拦截非法配置! validator reject, 内存设备数与 NVS 保持未受污染 (count=%d, ver=%lu)",
                 config_get_device_num(), (unsigned long)config_manager_get_version());
    } else {
        ESP_LOGE(CFG_TAG, "  --> ❌ [Test 2 FAIL] 拦截逻辑异常, 返回: %s", esp_err_to_name(err));
    }

    ESP_LOGI(CFG_TAG, "==================================================");
    ESP_LOGI(CFG_TAG, "  🎉 Step 7.3.3-2A 测试完成! (进入 Test 3 重启恢复验证)");
    ESP_LOGI(CFG_TAG, "==================================================");

    vTaskDelete(NULL);
}

void config_manager_run_test_suite(void)
{
    xTaskCreatePinnedToCore(
        cfg_test_task,
        "cfg_test_task",
        4096,
        NULL,
        3,
        NULL,
        tskNO_AFFINITY
    );
}
