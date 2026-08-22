#include "debug_sink.h"
#include "esp_log.h"

static const char *TAG = "DEBUG_SINK";

/**
 * @brief Debug Sink 回调函数：接收 data_manager 广播的网关全局快照并打印验证
 */
static esp_err_t debug_sink_callback(const gateway_data_t *data, void *ctx)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "================ [Debug Sink: 网关快照数据] ================");
    ESP_LOGI(TAG, "网关ID: %s, 版本: %lu, 设备数: %u, 时戳: %lld ms", 
             data->gateway_id, (unsigned long)data->config_version, 
             data->device_count, (long long)data->snapshot_timestamp_ms);

    for (uint8_t d = 0; d < data->device_count; d++) {
        const device_data_t *dev = &data->devices[d];
        ESP_LOGI(TAG, "  -> 设备 [%s] (ID=%u, 状态=%d, 测点数=%u):", 
                 dev->device_name, dev->slave_id, dev->status, dev->metric_count);
        for (uint8_t m = 0; m < dev->metric_count; m++) {
            const metric_data_t *metric = &dev->metrics[m];
            if (metric->val_type == VAL_TYPE_FLOAT) {
                ESP_LOGI(TAG, "     * 测点 [%s]: %.2f %s", metric->name, metric->value.f32, metric->unit);
            } else if (metric->val_type == VAL_TYPE_INT32) {
                ESP_LOGI(TAG, "     * 测点 [%s]: %ld %s", metric->name, (long)metric->value.i32, metric->unit);
            } else if (metric->val_type == VAL_TYPE_BOOL) {
                ESP_LOGI(TAG, "     * 测点 [%s]: %s %s", metric->name, metric->value.b_val ? "true" : "false", metric->unit);
            } else {
                ESP_LOGI(TAG, "     * 测点 [%s]: %.2f %s", metric->name, metric->value.f32, metric->unit);
            }
        }
    }
    ESP_LOGI(TAG, "============================================================");

    return ESP_OK;
}

esp_err_t debug_sink_init(void)
{
    ESP_LOGI(TAG, "正在注册 Debug Sink 调试出口插件...");
    return data_manager_register_sink("debug", debug_sink_callback, NULL);
}
