#include "data_internal.h"
#include "esp_log.h"

static const char *TAG = "DATA_QUEUE";

esp_err_t data_queue_init(void)
{
    data_manager_context_t *ctx = data_manager_get_context();
    if (ctx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx->data_queue == NULL) {
        ctx->data_queue = xQueueCreate(DATA_QUEUE_LENGTH, sizeof(device_data_t));
        if (ctx->data_queue == NULL) {
            ESP_LOGE(TAG, "创建 data_queue 队列失败 (容量 %d)", DATA_QUEUE_LENGTH);
            return ESP_ERR_NO_MEM;
        }
    }

    ctx->dropped_packet_count = 0;
    ctx->processed_packet_count = 0;

    ESP_LOGI(TAG, "数据缓冲队列初始化成功 (容量: %d 帧)", DATA_QUEUE_LENGTH);
    return ESP_OK;
}

esp_err_t data_queue_push(const device_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    data_manager_context_t *ctx = data_manager_get_context();
    if (ctx == NULL || ctx->data_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // 尝试 0 阻塞推入
    BaseType_t ret = xQueueSend(ctx->data_queue, data, 0);
    if (ret != pdPASS) {
        // 队列已满，执行 Ring-Drop Oldest 防御策略，保证最新时效性
        device_data_t dummy;
        xQueueReceive(ctx->data_queue, &dummy, 0);
        xQueueSend(ctx->data_queue, data, 0);
        ctx->dropped_packet_count++;
    }

    return ESP_OK;
}

esp_err_t data_queue_receive(device_data_t *data, TickType_t wait_ticks)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    data_manager_context_t *ctx = data_manager_get_context();
    if (ctx == NULL || ctx->data_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ret = xQueueReceive(ctx->data_queue, data, wait_ticks);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_TIMEOUT;
}
