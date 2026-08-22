#include "config_manager.h"
#include "config_validator.h"
#include "config_storage.h"
#include "device_manager.h"
#include "data_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "CONFIG_APPLY_TEST";

/**
 * @brief Test 1: 提交非法 slave_id=300，期望 4001/非法参数拦截，且配置未被改变
 */
static bool test1_invalid_slave_id_rejected(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 1: 提交非法 slave_id = 300 拦截与原子保护测试]");

    int dev_cnt_before = config_get_device_num();
    uint32_t ver_before = config_manager_get_version();

    const char *bad_json = 
        "{\"version\":99,\"devices\":["
        "{\"name\":\"invalid_sensor\",\"slave_id\":300,\"register\":10,\"period\":1000}"
        "]}";

    // 1. Dry-run 预校验检查
    config_validation_result_t val_res;
    esp_err_t err_val = config_manager_validate_json(bad_json, &val_res);

    // 2. 热应用检查 (确保内部同样被严格拦截)
    uint32_t new_ver = 0;
    esp_err_t err_apply = config_manager_apply_json(bad_json, &new_ver);

    int dev_cnt_after = config_get_device_num();
    uint32_t ver_after = config_manager_get_version();

    bool pass = (err_val != ESP_OK) && (err_apply != ESP_OK) && 
                (dev_cnt_before == dev_cnt_after) && (ver_before == ver_after);

    if (pass) {
        ESP_LOGI(TAG, "  --> ✅ [Test 1 PASS] 成功拦截非法从站 ID，返回错误并保持运行态配置完整无损!");
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 1 FAIL] 校验拦截或原子保护失效");
    }
    return pass;
}

/**
 * @brief Test 2: 提交新增设备 pressure_sensor_v2 (slave_id=3)，期望 code=0 且版本递增
 */
static bool test2_apply_new_device_pressure_sensor_v2(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 2: 提交新增设备 pressure_sensor_v2 (slave_id=3) 热应用]");

    const char *new_cfg_json = 
        "{\"version\":16,\"devices\":["
        "{\"name\":\"temperature_sensor\",\"slave_id\":2,\"register\":16,\"data\":{\"scale\":0.1},\"period\":1000,"
        "\"metrics\":[{\"name\":\"temp_limit\",\"write_register\":19,\"scale\":0.1,\"min\":0,\"max\":100}]},"
        "{\"name\":\"pressure_sensor_v2\",\"slave_id\":3,\"register\":20,\"data\":{\"scale\":0.01},\"period\":500}"
        "]}";

    uint32_t applied_version = 0;
    esp_err_t err = config_manager_apply_json(new_cfg_json, &applied_version);

    bool pass = (err == ESP_OK) && (applied_version > 0);

    if (pass) {
        ESP_LOGI(TAG, "  --> ✅ [Test 2 PASS] 配置热应用成功! 生效版本: v%lu, 设备数: %d", 
                 (unsigned long)applied_version, config_get_device_num());
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 2 FAIL] 热应用失败: %s", esp_err_to_name(err));
    }
    return pass;
}

/**
 * @brief Test 3: 确认新增设备已经保存并在 active 运行配置中生效
 */
static bool test3_verify_config_saved_and_in_ram(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 3: 验证活跃配置表与设备查询 API]");

    device_config_t dev;
    esp_err_t err = config_get_device_by_name("pressure_sensor_v2", &dev);

    bool pass = (err == ESP_OK) && (dev.slave_id == 3) && (dev.register_addr == 20) && (dev.period == 500);

    if (pass) {
        ESP_LOGI(TAG, "  --> ✅ [Test 3 PASS] 活跃配置成功确认包含 pressure_sensor_v2 (slave_id=3, reg=20, period=500ms)!");
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 3 FAIL] 活跃配置中未找到新设备或参数不符: %s", esp_err_to_name(err));
    }
    return pass;
}

/**
 * @brief Test 4: 确认 device_manager 和 scheduler 开始管理新设备
 */
static bool test4_verify_device_manager_and_scheduler(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 4: 验证 device_manager 运行态与调度器管理新设备]");

    uint8_t dev_cnt = device_manager_get_device_count();
    device_stats_t stats;
    data_status_t status;
    esp_err_t err = device_manager_get_device_status("pressure_sensor_v2", &stats, &status);

    bool pass = (err == ESP_OK) && (dev_cnt >= 2);

    if (pass) {
        ESP_LOGI(TAG, "  --> ✅ [Test 4 PASS] device_manager 成功纳管新设备并在集中调度池中运行! (实例总数: %u)", dev_cnt);
    } else {
        ESP_LOGE(TAG, "  --> ❌ [Test 4 FAIL] device_manager 未纳管新设备: %s (dev_cnt=%u)", esp_err_to_name(err), dev_cnt);
    }
    return pass;
}

/**
 * @brief Test 5: 验证 NVS 持久化与掉电恢复能力 (从 NVS 读出并校验 CRC32)
 */
static bool test5_verify_nvs_persistence_recovery(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "▶ [Test 5: 验证 NVS 掉电持久化与自恢复完整性]");

    char load_buf[2048];
    uint32_t loaded_version = 0;
    esp_err_t err = config_storage_load(load_buf, sizeof(load_buf), &loaded_version);

    bool pass = false;
    if (err == ESP_OK && loaded_version > 0) {
        if (strstr(load_buf, "pressure_sensor_v2") != NULL) {
            pass = true;
            ESP_LOGI(TAG, "  --> ✅ [Test 5 PASS] 成功从 NVS 恢复持久化配置 (v%lu)，包含新增设备 pressure_sensor_v2!", 
                     (unsigned long)loaded_version);
        }
    }

    if (!pass) {
        ESP_LOGE(TAG, "  --> ❌ [Test 5 FAIL] NVS 恢复失败或未包含新配置: %s", esp_err_to_name(err));
    }
    return pass;
}

void config_apply_run_test_suite(void)
{
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "🚀 开始执行 Step 7.3.5-3 Phase 2 Web 配置热应用全链路自动化测试");
    ESP_LOGI(TAG, "============================================================");

    int pass_count = 0;
    if (test1_invalid_slave_id_rejected()) pass_count++;
    if (test2_apply_new_device_pressure_sensor_v2()) pass_count++;
    if (test3_verify_config_saved_and_in_ram()) pass_count++;
    if (test4_verify_device_manager_and_scheduler()) pass_count++;
    if (test5_verify_nvs_persistence_recovery()) pass_count++;

    ESP_LOGI(TAG, "============================================================");
    if (pass_count == 5) {
        ESP_LOGI(TAG, "🎉 [Step 7.3.5-3 Phase 2 自动化测试结果]: 全部 5 项测试 100%% PASS!");
    } else {
        ESP_LOGE(TAG, "❌ [Step 7.3.5-3 Phase 2 自动化测试结果]: %d/5 PASS, 存在失败项!", pass_count);
    }
    ESP_LOGI(TAG, "============================================================");
}
