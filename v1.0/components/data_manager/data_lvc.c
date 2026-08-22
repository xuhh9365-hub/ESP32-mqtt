#include "data_internal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "DATA_LVC";

esp_err_t data_lvc_init(void)
{
    data_manager_context_t *ctx = data_manager_get_context();
    if (ctx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx->lvc_mutex == NULL) {
        ctx->lvc_mutex = xSemaphoreCreateMutex();
        if (ctx->lvc_mutex == NULL) {
            ESP_LOGE(TAG, "创建 lvc_mutex 互斥锁失败");
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(ctx->lvc_mutex, portMAX_DELAY);
    memset(&ctx->latest_gateway_data, 0, sizeof(gateway_data_t));
    strncpy(ctx->latest_gateway_data.gateway_id, "esp32_gateway_001", DATA_GATEWAY_ID_MAX_LEN - 1);
    ctx->latest_gateway_data.config_version = 1;
    ctx->latest_gateway_data.device_count = 0;
    ctx->latest_gateway_data.snapshot_timestamp_ms = 0;
    xSemaphoreGive(ctx->lvc_mutex);

    ESP_LOGI(TAG, "LVC (Latest Value Cache) 实时数据快照引擎初始化成功");
    return ESP_OK;
}

esp_err_t data_lvc_update(const device_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    data_manager_context_t *ctx = data_manager_get_context();
    if (ctx == NULL || ctx->lvc_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(ctx->lvc_mutex, portMAX_DELAY);

    int target_idx = -1;
    for (uint8_t i = 0; i < ctx->latest_gateway_data.device_count; i++) {
        if (strncmp(ctx->latest_gateway_data.devices[i].device_name, data->device_name, DATA_DEVICE_NAME_MAX_LEN) == 0) {
            target_idx = i;
            break;
        }
    }

    if (target_idx >= 0) {
        // 覆盖更新已有设备的全部最新测点
        ctx->latest_gateway_data.devices[target_idx] = *data;
    } else {
        // 新设备插入槽位
        if (ctx->latest_gateway_data.device_count < DATA_MAX_DEVICES) {
            uint8_t slot = ctx->latest_gateway_data.device_count;
            ctx->latest_gateway_data.devices[slot] = *data;
            ctx->latest_gateway_data.device_count++;
            target_idx = slot;
        } else {
            ESP_LOGW(TAG, "LVC 设备快照池已满 (最大 %d)", DATA_MAX_DEVICES);
        }
    }

    ctx->latest_gateway_data.snapshot_timestamp_ms = esp_timer_get_time() / 1000;

    xSemaphoreGive(ctx->lvc_mutex);
    return (target_idx >= 0) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t data_manager_get_device_snapshot(const char *name, device_data_t *out)
{
    if (name == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    data_manager_context_t *ctx = data_manager_get_context();
    if (ctx == NULL || ctx->lvc_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(ctx->lvc_mutex, portMAX_DELAY);

    for (uint8_t i = 0; i < ctx->latest_gateway_data.device_count; i++) {
        if (strncmp(ctx->latest_gateway_data.devices[i].device_name, name, DATA_DEVICE_NAME_MAX_LEN) == 0) {
            *out = ctx->latest_gateway_data.devices[i]; // Copy-out 机制
            xSemaphoreGive(ctx->lvc_mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(ctx->lvc_mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t data_manager_get_gateway_snapshot(gateway_data_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    data_manager_context_t *ctx = data_manager_get_context();
    if (ctx == NULL || ctx->lvc_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(ctx->lvc_mutex, portMAX_DELAY);
    *out = ctx->latest_gateway_data; // Copy-out 全局快照
    xSemaphoreGive(ctx->lvc_mutex);

    return ESP_OK;
}

esp_err_t data_lvc_remove_device(const char *device_name)
{
    if (device_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    data_manager_context_t *ctx = data_manager_get_context();
    if (ctx == NULL || ctx->lvc_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(ctx->lvc_mutex, portMAX_DELAY);

    int target_idx = -1;
    for (uint8_t i = 0; i < ctx->latest_gateway_data.device_count; i++) {
        if (strncmp(ctx->latest_gateway_data.devices[i].device_name, device_name, DATA_DEVICE_NAME_MAX_LEN) == 0) {
            target_idx = i;
            break;
        }
    }

    if (target_idx >= 0) {
        // 将后续设备向前平移，保持紧凑数组
        for (uint8_t i = target_idx; i < ctx->latest_gateway_data.device_count - 1; i++) {
            ctx->latest_gateway_data.devices[i] = ctx->latest_gateway_data.devices[i + 1];
        }
        memset(&ctx->latest_gateway_data.devices[ctx->latest_gateway_data.device_count - 1], 0, sizeof(device_data_t));
        ctx->latest_gateway_data.device_count--;
        ctx->latest_gateway_data.snapshot_timestamp_ms = esp_timer_get_time() / 1000;
        ESP_LOGI(TAG, "LVC 快照已同步清理删除设备: %s (剩余快照设备数: %u)", 
                 device_name, (unsigned int)ctx->latest_gateway_data.device_count);
    }

    xSemaphoreGive(ctx->lvc_mutex);
    return (target_idx >= 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t data_manager_handle_device_event(device_event_type_t event, const char *device_name)
{
    if (device_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    data_manager_context_t *ctx = data_manager_get_context();
    if (ctx == NULL || ctx->lvc_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    switch (event) {
        case DEVICE_EVENT_REMOVE:
            return data_lvc_remove_device(device_name);

        case DEVICE_EVENT_ADD: {
            xSemaphoreTake(ctx->lvc_mutex, portMAX_DELAY);
            // 检查是否已有该设备
            bool exists = false;
            for (uint8_t i = 0; i < ctx->latest_gateway_data.device_count; i++) {
                if (strncmp(ctx->latest_gateway_data.devices[i].device_name, device_name, DATA_DEVICE_NAME_MAX_LEN) == 0) {
                    exists = true;
                    break;
                }
            }
            if (!exists && ctx->latest_gateway_data.device_count < DATA_MAX_DEVICES) {
                uint8_t slot = ctx->latest_gateway_data.device_count;
                memset(&ctx->latest_gateway_data.devices[slot], 0, sizeof(device_data_t));
                strncpy(ctx->latest_gateway_data.devices[slot].device_name, device_name, DATA_DEVICE_NAME_MAX_LEN - 1);
                ctx->latest_gateway_data.devices[slot].status = DATA_STATUS_INVALID;
                ctx->latest_gateway_data.devices[slot].update_timestamp_ms = esp_timer_get_time() / 1000;
                ctx->latest_gateway_data.device_count++;
                ctx->latest_gateway_data.snapshot_timestamp_ms = esp_timer_get_time() / 1000;
                ESP_LOGI(TAG, "LVC 已即时注册新设备槽位: %s (当前快照设备数: %u)", 
                         device_name, (unsigned int)ctx->latest_gateway_data.device_count);
            }
            xSemaphoreGive(ctx->lvc_mutex);
            return ESP_OK;
        }

        case DEVICE_EVENT_UPDATE:
            ESP_LOGI(TAG, "LVC 收到更新设备事件: %s", device_name);
            return ESP_OK;

        default:
            return ESP_ERR_INVALID_ARG;
    }
}
