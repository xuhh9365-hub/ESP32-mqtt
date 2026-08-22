#include "device_internal.h"
#include "data_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "DEVICE_HOT_RELOAD";

uint8_t device_manager_get_device_count(void)
{
    device_manager_context_t *ctx = device_manager_get_context();
    if (ctx == NULL || ctx->instance_lock == NULL) {
        return 0;
    }

    uint8_t cnt = 0;
    xSemaphoreTake(ctx->instance_lock, portMAX_DELAY);
    cnt = ctx->device_count;
    xSemaphoreGive(ctx->instance_lock);

    return cnt;
}

esp_err_t device_manager_update_device(const device_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    device_manager_context_t *ctx = device_manager_get_context();
    if (ctx == NULL || ctx->instance_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(ctx->instance_lock, portMAX_DELAY);

    int target_slot = -1;
    for (int i = 0; i < DEVICE_MANAGER_MAX_DEVICES; i++) {
        if (ctx->instances[i].is_valid && 
            strncmp(ctx->instances[i].config.name, config->name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
            target_slot = i;
            break;
        }
    }

    if (target_slot < 0) {
        xSemaphoreGive(ctx->instance_lock);
        ESP_LOGW(TAG, "更新失败: 未找到运行态设备 [%s]", config->name);
        return ESP_ERR_NOT_FOUND;
    }

    device_instance_t *inst = &ctx->instances[target_slot];

    // 核心生命周期保护：代数自增，使旧任务正在进行的读写操作自动安全作废
    inst->generation++;
    inst->config = *config;
    inst->is_enabled = true;
    inst->status = DATA_STATUS_INVALID;
    memset(&inst->stats, 0, sizeof(inst->stats));

    // 立即重置下次轮询 Tick
    inst->next_poll_tick = xTaskGetTickCount() + pdMS_TO_TICKS(50);

    uint32_t new_gen = inst->generation;
    xSemaphoreGive(ctx->instance_lock);

    ESP_LOGI(TAG, "✅ 已热更新设备 [%s] -> 槽位: %d, 新代数: %lu, 从站ID: %u, 周期: %lu ms",
             config->name, target_slot, (unsigned long)new_gen, 
             (unsigned int)config->slave_id, (unsigned long)config->period);

    // 同步生命周期事件至 data_manager
    data_manager_handle_device_event(DEVICE_EVENT_UPDATE, config->name);

    return ESP_OK;
}

/**
 * @brief 判断两个设备配置是否完全一致
 */
static bool is_device_config_equal(const device_config_t *a, const device_config_t *b)
{
    if (a->slave_id != b->slave_id) return false;
    if (a->register_addr != b->register_addr) return false;
    if (a->period != b->period) return false;
    if (fabsf(a->scale - b->scale) > 1e-6f) return false;
    if (strncmp(a->reg_type, b->reg_type, sizeof(a->reg_type)) != 0) return false;
    if (strncmp(a->data_type, b->data_type, sizeof(a->data_type)) != 0) return false;
    if (a->metric_count != b->metric_count) return false;

    for (uint8_t m = 0; m < a->metric_count; m++) {
        if (strncmp(a->metrics[m].metric_name, b->metrics[m].metric_name, CONFIG_METRIC_NAME_MAX_LEN) != 0) return false;
        if (a->metrics[m].write_register != b->metrics[m].write_register) return false;
        if (fabsf(a->metrics[m].scale - b->metrics[m].scale) > 1e-6f) return false;
        if (fabsf(a->metrics[m].min_value - b->metrics[m].min_value) > 1e-6f) return false;
        if (fabsf(a->metrics[m].max_value - b->metrics[m].max_value) > 1e-6f) return false;
    }

    return true;
}

esp_err_t device_manager_apply_config_diff(
    const device_config_t *old_cfg,
    uint8_t old_count,
    const device_config_t *new_cfg,
    uint8_t new_count)
{
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "▶ 开始执行设备运行态差异化热重载 (Diff Apply)");
    ESP_LOGI(TAG, "  原设备数: %u, 新设备数: %u", old_count, new_count);

    // 1. 检查被删除的设备 (在 old_cfg 中存在，但不在 new_cfg 中)
    if (old_cfg != NULL) {
        for (uint8_t i = 0; i < old_count; i++) {
            bool still_exists = false;
            if (new_cfg != NULL) {
                for (uint8_t j = 0; j < new_count; j++) {
                    if (strncmp(old_cfg[i].name, new_cfg[j].name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
                        still_exists = true;
                        break;
                    }
                }
            }
            if (!still_exists) {
                ESP_LOGI(TAG, "[-] 检测到设备移除: %s", old_cfg[i].name);
                device_manager_remove_device(old_cfg[i].name);
            }
        }
    }

    // 2. 检查新增设备与修改的设备
    if (new_cfg != NULL) {
        for (uint8_t j = 0; j < new_count; j++) {
            int old_idx = -1;
            if (old_cfg != NULL) {
                for (uint8_t i = 0; i < old_count; i++) {
                    if (strncmp(new_cfg[j].name, old_cfg[i].name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
                        old_idx = i;
                        break;
                    }
                }
            }

            if (old_idx < 0) {
                // 新增设备
                ESP_LOGI(TAG, "[+] 检测到新设备添加: %s (从站ID: %u, 周期: %lums)", 
                         new_cfg[j].name, (unsigned int)new_cfg[j].slave_id, (unsigned long)new_cfg[j].period);
                device_manager_add_device(&new_cfg[j]);
            } else {
                // 已存在设备：检查参数是否变更
                if (!is_device_config_equal(&old_cfg[old_idx], &new_cfg[j])) {
                    ESP_LOGI(TAG, "[*] 检测到设备参数变更: %s (触发热更新并递增代数)", new_cfg[j].name);
                    device_manager_update_device(&new_cfg[j]);
                } else {
                    ESP_LOGD(TAG, "[=] 设备 [%s] 配置无变化，平滑维持运行", new_cfg[j].name);
                }
            }
        }
    }

    uint8_t final_count = device_manager_get_device_count();
    ESP_LOGI(TAG, "✅ 设备运行态差异化热重载完成! 当前活跃运行设备数: %u", final_count);
    ESP_LOGI(TAG, "==================================================");

    return ESP_OK;
}

esp_err_t device_manager_apply_config(const device_config_t *devices, uint8_t count)
{
    device_manager_context_t *ctx = device_manager_get_context();
    if (ctx == NULL || ctx->instance_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    device_config_t old_cfg[DEVICE_MANAGER_MAX_DEVICES];
    uint8_t old_count = 0;

    xSemaphoreTake(ctx->instance_lock, portMAX_DELAY);
    for (int i = 0; i < DEVICE_MANAGER_MAX_DEVICES; i++) {
        if (ctx->instances[i].is_valid) {
            old_cfg[old_count++] = ctx->instances[i].config;
        }
    }
    xSemaphoreGive(ctx->instance_lock);

    return device_manager_apply_config_diff(old_cfg, old_count, devices, count);
}
