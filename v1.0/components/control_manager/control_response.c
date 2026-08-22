#include "control_internal.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CONTROL_RESPONSE";

static const char *control_error_to_string(int code)
{
    switch (code) {
    case CONTROL_OK:
        return "success";
    case CONTROL_ERR_INVALID_JSON:
        return "invalid_json";
    case CONTROL_ERR_UNSUPPORTED_CMD:
        return "unsupported_command";
    case CONTROL_ERR_PARAM_OUT_OF_RANGE:
        return "param_out_of_range";
    case CONTROL_ERR_DEVICE_NOT_FOUND:
        return "device_not_found";
    case CONTROL_ERR_METRIC_NOT_FOUND:
        return "metric_not_found";
    case CONTROL_ERR_DEVICE_OFFLINE:
        return "device_offline";
    case CONTROL_ERR_WRITE_FAILED:
        return "write_failed";
    case CONTROL_ERR_TIMEOUT:
        return "timeout";
    case CONTROL_ERR_QUEUE_FULL:
        return "queue_full";
    default:
        return "unknown_error";
    }
}

esp_err_t control_response_format(
    const control_response_t *resp,
    char *out_json,
    size_t max_len)
{
    if (resp == NULL || out_json == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "创建 JSON 对象失败 (内存不足)");
        return ESP_ERR_NO_MEM;
    }

    // 1. 添加 msg_id
    cJSON_AddStringToObject(root, "msg_id", resp->msg_id);

    // 2. 添加 code 状态码
    cJSON_AddNumberToObject(root, "code", resp->code);

    // 3. 添加 message 描述
    const char *msg_str = (resp->message[0] != '\0') ? resp->message : control_error_to_string(resp->code);
    cJSON_AddStringToObject(root, "message", msg_str);

    // 4. 添加 timestamp (毫秒)
    cJSON_AddNumberToObject(root, "timestamp", (double)resp->timestamp_ms);

    // 5. 添加 cost_ms 耗时
    cJSON_AddNumberToObject(root, "cost_ms", resp->cost_ms);

    // 6. 纯预分配内存序列化 (零堆内存 malloc 开销)
    bool ok = cJSON_PrintPreallocated(root, out_json, (int)max_len, false);
    cJSON_Delete(root);

    if (!ok) {
        ESP_LOGE(TAG, "回执 JSON 序列化缓冲区溢出 (max_len=%u)", (unsigned int)max_len);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(TAG, "生成控制回执 JSON: %s", out_json);
    return ESP_OK;
}
