#include "data_formatter.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "DATA_FORMATTER";

esp_err_t data_format_json(
    const gateway_data_t *data,
    char *buffer,
    size_t len)
{
    if (data == NULL || buffer == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "创建 cJSON root 节点失败 (内存不足)");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "gateway_id", data->gateway_id);
    cJSON_AddNumberToObject(root, "version", (double)data->config_version);
    cJSON_AddNumberToObject(root, "timestamp", (double)data->snapshot_timestamp_ms);

    cJSON *dev_arr = cJSON_CreateArray();
    if (dev_arr == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToObject(root, "devices", dev_arr);

    for (uint8_t i = 0; i < data->device_count; i++) {
        const device_data_t *dev = &data->devices[i];
        cJSON *dev_obj = cJSON_CreateObject();
        if (dev_obj == NULL) continue;

        cJSON_AddStringToObject(dev_obj, "name", dev->device_name);
        cJSON_AddNumberToObject(dev_obj, "slave", (double)dev->slave_id);
        cJSON_AddNumberToObject(dev_obj, "status", (double)dev->status);

        cJSON *m_arr = cJSON_CreateArray();
        if (m_arr != NULL) {
            cJSON_AddItemToObject(dev_obj, "metrics", m_arr);
            for (uint8_t m = 0; m < dev->metric_count; m++) {
                const metric_data_t *metric = &dev->metrics[m];
                cJSON *m_obj = cJSON_CreateObject();
                if (m_obj == NULL) continue;

                cJSON_AddStringToObject(m_obj, "name", metric->name);

                switch (metric->val_type) {
                case VAL_TYPE_FLOAT:
                    cJSON_AddNumberToObject(m_obj, "value", (double)metric->value.f32);
                    break;
                case VAL_TYPE_DOUBLE:
                    cJSON_AddNumberToObject(m_obj, "value", metric->value.f64);
                    break;
                case VAL_TYPE_INT32:
                    cJSON_AddNumberToObject(m_obj, "value", (double)metric->value.i32);
                    break;
                case VAL_TYPE_UINT32:
                    cJSON_AddNumberToObject(m_obj, "value", (double)metric->value.u32);
                    break;
                case VAL_TYPE_BOOL:
                    cJSON_AddBoolToObject(m_obj, "value", metric->value.b_val);
                    break;
                default:
                    cJSON_AddNumberToObject(m_obj, "value", (double)metric->value.f32);
                    break;
                }

                cJSON_AddStringToObject(m_obj, "unit", metric->unit);
                cJSON_AddItemToArray(m_arr, m_obj);
            }
        }

        cJSON_AddItemToArray(dev_arr, dev_obj);
    }

    // 预分配内存打印 (禁止动态 malloc 堆碎片)
    cJSON_bool printed = cJSON_PrintPreallocated(root, buffer, (int)len, false);
    cJSON_Delete(root);

    if (!printed) {
        ESP_LOGW(TAG, "JSON 格式化缓冲区空间不足 (当前容量: %u 字节)", (unsigned int)len);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
