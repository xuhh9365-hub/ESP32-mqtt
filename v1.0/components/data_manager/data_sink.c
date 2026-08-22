#include "data_sink.h"
#include "data_internal.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "DATA_SINK";
static data_sink_t s_sinks[MAX_DATA_SINK];

esp_err_t data_sink_init(void)
{
    data_manager_context_t *ctx = data_manager_get_context();
    if (ctx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx->sink_lock == NULL) {
        ctx->sink_lock = xSemaphoreCreateMutex();
        if (ctx->sink_lock == NULL) {
            ESP_LOGE(TAG, "创建 sink_lock 互斥锁失败");
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(ctx->sink_lock, portMAX_DELAY);
    memset(s_sinks, 0, sizeof(s_sinks));
    xSemaphoreGive(ctx->sink_lock);

    return ESP_OK;
}

esp_err_t data_manager_register_sink(
    const char *name,
    data_sink_callback_t callback,
    void *ctx)
{
    if (name == NULL || callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    data_manager_context_t *dm_ctx = data_manager_get_context();
    if (dm_ctx == NULL || dm_ctx->sink_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(dm_ctx->sink_lock, portMAX_DELAY);

    // 1. 检查是否已经存在同名 Sink (支持更新)
    int target_slot = -1;
    for (int i = 0; i < MAX_DATA_SINK; i++) {
        if (s_sinks[i].callback != NULL && 
            strncmp(s_sinks[i].name, name, SINK_NAME_MAX_LEN) == 0) {
            target_slot = i;
            break;
        }
    }

    // 2. 寻找空闲槽位
    if (target_slot < 0) {
        for (int i = 0; i < MAX_DATA_SINK; i++) {
            if (s_sinks[i].callback == NULL) {
                target_slot = i;
                break;
            }
        }
    }

    if (target_slot < 0) {
        xSemaphoreGive(dm_ctx->sink_lock);
        ESP_LOGE(TAG, "Sink 插件槽位已满 (最大 %d 个)", MAX_DATA_SINK);
        return ESP_ERR_NO_MEM;
    }

    strncpy(s_sinks[target_slot].name, name, SINK_NAME_MAX_LEN - 1);
    s_sinks[target_slot].name[SINK_NAME_MAX_LEN - 1] = '\0';
    s_sinks[target_slot].enabled = true;
    s_sinks[target_slot].callback = callback;
    s_sinks[target_slot].ctx = ctx;

    xSemaphoreGive(dm_ctx->sink_lock);

    ESP_LOGI(TAG, "成功注册 Sink 插件 [槽位 %d]: 名称=%s, 状态=使能", target_slot, name);
    return ESP_OK;
}

esp_err_t data_manager_set_sink_enabled(const char *name, bool enable)
{
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    data_manager_context_t *dm_ctx = data_manager_get_context();
    if (dm_ctx == NULL || dm_ctx->sink_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(dm_ctx->sink_lock, portMAX_DELAY);

    for (int i = 0; i < MAX_DATA_SINK; i++) {
        if (s_sinks[i].callback != NULL && 
            strncmp(s_sinks[i].name, name, SINK_NAME_MAX_LEN) == 0) {
            s_sinks[i].enabled = enable;
            xSemaphoreGive(dm_ctx->sink_lock);
            ESP_LOGI(TAG, "已设置 Sink [%s] 状态: %s", name, enable ? "使能" : "禁用");
            return ESP_OK;
        }
    }

    xSemaphoreGive(dm_ctx->sink_lock);
    return ESP_ERR_NOT_FOUND;
}

void data_sink_dispatch(const gateway_data_t *snapshot)
{
    if (snapshot == NULL) return;

    data_manager_context_t *dm_ctx = data_manager_get_context();
    if (dm_ctx == NULL || dm_ctx->sink_lock == NULL) return;

    xSemaphoreTake(dm_ctx->sink_lock, portMAX_DELAY);

    for (int i = 0; i < MAX_DATA_SINK; i++) {
        if (s_sinks[i].enabled && s_sinks[i].callback != NULL) {
            // 快速非阻塞回调广播
            s_sinks[i].callback(snapshot, s_sinks[i].ctx);
        }
    }

    xSemaphoreGive(dm_ctx->sink_lock);
}
