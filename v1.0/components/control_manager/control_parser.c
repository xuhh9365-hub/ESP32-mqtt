#include "control_internal.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "CONTROL_PARSER";

esp_err_t control_parser_parse(
    const char *raw_json,
    size_t len,
    control_message_t *out_msg,
    int *out_err_code)
{
    if (out_err_code != NULL) {
        *out_err_code = CONTROL_ERR_INVALID_JSON;
    }

    if (raw_json == NULL || len == 0 || out_msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_msg, 0, sizeof(control_message_t));

    // 1. 解析 JSON 根对象
    cJSON *root = cJSON_ParseWithLength(raw_json, len);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON 根节点解析失败 (格式非法)");
        if (out_err_code != NULL) *out_err_code = CONTROL_ERR_INVALID_JSON;
        return ESP_FAIL;
    }

    // 2. 校验 msg_id (必填字符串，< 32 字节)
    cJSON *item_id = cJSON_GetObjectItem(root, "msg_id");
    if (!cJSON_IsString(item_id) || item_id->valuestring == NULL || item_id->valuestring[0] == '\0') {
        ESP_LOGE(TAG, "缺失或非法的 'msg_id' 字段");
        cJSON_Delete(root);
        if (out_err_code != NULL) *out_err_code = CONTROL_ERR_INVALID_JSON;
        return ESP_FAIL;
    }
    strncpy(out_msg->msg_id, item_id->valuestring, CONTROL_MSG_ID_MAX_LEN - 1);
    out_msg->msg_id[CONTROL_MSG_ID_MAX_LEN - 1] = '\0';

    // 3. 校验 device (必填字符串)
    cJSON *item_dev = cJSON_GetObjectItem(root, "device");
    if (!cJSON_IsString(item_dev) || item_dev->valuestring == NULL || item_dev->valuestring[0] == '\0') {
        ESP_LOGE(TAG, "[%s] 缺失或非法的 'device' 字段", out_msg->msg_id);
        cJSON_Delete(root);
        if (out_err_code != NULL) *out_err_code = CONTROL_ERR_DEVICE_NOT_FOUND;
        return ESP_FAIL;
    }
    strncpy(out_msg->device, item_dev->valuestring, CONTROL_DEVICE_NAME_MAX_LEN - 1);
    out_msg->device[CONTROL_DEVICE_NAME_MAX_LEN - 1] = '\0';

    // 4. 校验 command (当前仅支持 "write")
    cJSON *item_cmd = cJSON_GetObjectItem(root, "command");
    if (!cJSON_IsString(item_cmd) || item_cmd->valuestring == NULL) {
        ESP_LOGE(TAG, "[%s] 缺失 'command' 字段", out_msg->msg_id);
        cJSON_Delete(root);
        if (out_err_code != NULL) *out_err_code = CONTROL_ERR_INVALID_JSON;
        return ESP_FAIL;
    }
    strncpy(out_msg->command, item_cmd->valuestring, CONTROL_COMMAND_MAX_LEN - 1);
    out_msg->command[CONTROL_COMMAND_MAX_LEN - 1] = '\0';

    if (strcmp(item_cmd->valuestring, "write") == 0) {
        out_msg->cmd_type = CMD_TYPE_WRITE;
    } else {
        ESP_LOGW(TAG, "[%s] 不支持的命令类型: '%s'", out_msg->msg_id, item_cmd->valuestring);
        cJSON_Delete(root);
        if (out_err_code != NULL) *out_err_code = CONTROL_ERR_UNSUPPORTED_CMD;
        return ESP_FAIL;
    }

    // 5. 校验 metric (必填字符串)
    cJSON *item_metric = cJSON_GetObjectItem(root, "metric");
    if (!cJSON_IsString(item_metric) || item_metric->valuestring == NULL || item_metric->valuestring[0] == '\0') {
        ESP_LOGE(TAG, "[%s] 缺失或非法的 'metric' 字段", out_msg->msg_id);
        cJSON_Delete(root);
        if (out_err_code != NULL) *out_err_code = CONTROL_ERR_METRIC_NOT_FOUND;
        return ESP_FAIL;
    }
    strncpy(out_msg->metric, item_metric->valuestring, CONTROL_METRIC_NAME_MAX_LEN - 1);
    out_msg->metric[CONTROL_METRIC_NAME_MAX_LEN - 1] = '\0';

    // 6. 校验 value (数值或布尔量)
    cJSON *item_val = cJSON_GetObjectItem(root, "value");
    if (item_val == NULL || (!cJSON_IsNumber(item_val) && !cJSON_IsBool(item_val))) {
        ESP_LOGE(TAG, "[%s] 缺失或非法的 'value' 载荷", out_msg->msg_id);
        cJSON_Delete(root);
        if (out_err_code != NULL) *out_err_code = CONTROL_ERR_INVALID_JSON;
        return ESP_FAIL;
    }

    // 7. 解析 type 与物理量装载
    cJSON *item_type = cJSON_GetObjectItem(root, "type");
    out_msg->val_type = VAL_TYPE_FLOAT; // 默认 float

    if (cJSON_IsString(item_type) && item_type->valuestring != NULL) {
        if (strcmp(item_type->valuestring, "int") == 0 || strcmp(item_type->valuestring, "int32") == 0) {
            out_msg->val_type = VAL_TYPE_INT32;
            out_msg->value.i32 = (int32_t)item_val->valueint;
        } else if (strcmp(item_type->valuestring, "uint") == 0 || strcmp(item_type->valuestring, "uint32") == 0) {
            out_msg->val_type = VAL_TYPE_UINT32;
            out_msg->value.u32 = (uint32_t)item_val->valuedouble;
        } else if (strcmp(item_type->valuestring, "bool") == 0 || strcmp(item_type->valuestring, "boolean") == 0) {
            out_msg->val_type = VAL_TYPE_BOOL;
            out_msg->value.b_val = cJSON_IsTrue(item_val) || (item_val->valueint != 0);
        } else {
            out_msg->val_type = VAL_TYPE_FLOAT;
            out_msg->value.f32 = (float)item_val->valuedouble;
        }
    } else {
        out_msg->val_type = VAL_TYPE_FLOAT;
        out_msg->value.f32 = (float)item_val->valuedouble;
    }

    // 8. 可选字段 timeout_ms (缺省 1000ms)
    cJSON *item_tout = cJSON_GetObjectItem(root, "timeout_ms");
    if (cJSON_IsNumber(item_tout) && item_tout->valueint > 0) {
        out_msg->timeout_ms = (uint32_t)item_tout->valueint;
    } else {
        out_msg->timeout_ms = 1000;
    }

    out_msg->receive_timestamp_ms = esp_timer_get_time() / 1000;

    cJSON_Delete(root);

    if (out_err_code != NULL) {
        *out_err_code = CONTROL_OK;
    }

    ESP_LOGI(TAG, "成功解析指令 [%s] -> 设备: %s, 测点: %s, 动作: %s, 物理量: %.2f (type=%d, 超时=%lums)",
             out_msg->msg_id, out_msg->device, out_msg->metric, out_msg->command, 
             out_msg->value.f32, out_msg->val_type, (unsigned long)out_msg->timeout_ms);

    return ESP_OK;
}
