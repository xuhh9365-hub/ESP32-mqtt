#include "config_parser.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "CONFIG_PARSER";

/**
 * @brief 校验单个设备配置的合法性
 */
static esp_err_t validate_device_config(const device_config_t *dev)
{
    if (dev->name[0] == '\0') {
        ESP_LOGE(TAG, "设备名称不能为空");
        return ESP_ERR_INVALID_ARG;
    }
    if (dev->slave_id < 1 || dev->slave_id > 247) {
        ESP_LOGE(TAG, "设备 [%s] 从站地址非法: %d (必须在 1 ~ 247 范围内)", dev->name, dev->slave_id);
        return ESP_ERR_INVALID_ARG;
    }
    if (fabs((double)dev->scale) < 1e-6) {
        ESP_LOGE(TAG, "设备 [%s] 倍率 scale 不能为 0", dev->name);
        return ESP_ERR_INVALID_ARG;
    }
    if (dev->period < 100) {
        ESP_LOGE(TAG, "设备 [%s] 采集周期过小: %lu ms (至少需 >= 100ms)", dev->name, (unsigned long)dev->period);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

/**
 * @brief 解析单个设备 JSON 对象
 */
static esp_err_t parse_single_device(const cJSON *dev_item, device_config_t *out_dev)
{
    if (!cJSON_IsObject(dev_item)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_dev, 0, sizeof(device_config_t));

    // 默认值
    strncpy(out_dev->reg_type, "holding", sizeof(out_dev->reg_type) - 1);
    strncpy(out_dev->data_type, "uint16", sizeof(out_dev->data_type) - 1);
    out_dev->scale = 1.0f;
    out_dev->period = 1000;

    // 1. name
    cJSON *item_name = cJSON_GetObjectItem(dev_item, "name");
    if (cJSON_IsString(item_name) && item_name->valuestring != NULL) {
        strncpy(out_dev->name, item_name->valuestring, CONFIG_DEVICE_NAME_MAX_LEN - 1);
    } else {
        ESP_LOGE(TAG, "缺少设备名称 'name'");
        return ESP_ERR_INVALID_ARG;
    }

    // 2. slave_id
    cJSON *item_slave = cJSON_GetObjectItem(dev_item, "slave_id");
    if (cJSON_IsNumber(item_slave)) {
        out_dev->slave_id = (uint8_t)item_slave->valueint;
    } else {
        ESP_LOGE(TAG, "设备 [%s] 缺少 'slave_id'", out_dev->name);
        return ESP_ERR_INVALID_ARG;
    }

    // 3. register 嵌套对象或扁平字段
    cJSON *item_reg = cJSON_GetObjectItem(dev_item, "register");
    if (cJSON_IsObject(item_reg)) {
        cJSON *item_addr = cJSON_GetObjectItem(item_reg, "address");
        if (cJSON_IsNumber(item_addr)) {
            out_dev->register_addr = (uint16_t)item_addr->valueint;
        }
        cJSON *item_rtype = cJSON_GetObjectItem(item_reg, "type");
        if (cJSON_IsString(item_rtype) && item_rtype->valuestring != NULL) {
            strncpy(out_dev->reg_type, item_rtype->valuestring, sizeof(out_dev->reg_type) - 1);
        }
    } else {
        // 兼容扁平字段
        cJSON *item_addr = cJSON_GetObjectItem(dev_item, "address");
        if (cJSON_IsNumber(item_addr)) {
            out_dev->register_addr = (uint16_t)item_addr->valueint;
        }
    }

    // 4. data 嵌套对象或扁平字段
    cJSON *item_data = cJSON_GetObjectItem(dev_item, "data");
    if (cJSON_IsObject(item_data)) {
        cJSON *item_dtype = cJSON_GetObjectItem(item_data, "type");
        if (cJSON_IsString(item_dtype) && item_dtype->valuestring != NULL) {
            strncpy(out_dev->data_type, item_dtype->valuestring, sizeof(out_dev->data_type) - 1);
        }
        cJSON *item_scale = cJSON_GetObjectItem(item_data, "scale");
        if (cJSON_IsNumber(item_scale)) {
            out_dev->scale = (float)item_scale->valuedouble;
        }
    } else {
        // 兼容扁平字段
        cJSON *item_scale = cJSON_GetObjectItem(dev_item, "scale");
        if (cJSON_IsNumber(item_scale)) {
            out_dev->scale = (float)item_scale->valuedouble;
        }
    }

    // 5. period
    cJSON *item_period = cJSON_GetObjectItem(dev_item, "period");
    if (cJSON_IsNumber(item_period)) {
        out_dev->period = (uint32_t)item_period->valueint;
    }

    return validate_device_config(out_dev);
}

esp_err_t config_parser_parse_json(const char *json_str, 
                                  device_config_t *out_devices, 
                                  uint8_t max_devices, 
                                  uint8_t *out_count)
{
    if (json_str == NULL || out_devices == NULL || out_count == NULL || max_devices == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_count = 0;

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON 根节点解析失败");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *devices_arr = cJSON_GetObjectItem(root, "devices");
    if (!cJSON_IsArray(devices_arr)) {
        ESP_LOGE(TAG, "未找到 'devices' 数组节点");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    int dev_size = cJSON_GetArraySize(devices_arr);
    if (dev_size <= 0) {
        ESP_LOGW(TAG, "'devices' 数组为空");
        cJSON_Delete(root);
        return ESP_OK;
    }

    if (dev_size > max_devices) {
        ESP_LOGW(TAG, "设备数量 %d 超过最大容量 %d，将仅解析前 %d 个", dev_size, max_devices, max_devices);
        dev_size = max_devices;
    }

    uint8_t valid_cnt = 0;
    for (int i = 0; i < dev_size; i++) {
        cJSON *item = cJSON_GetArrayItem(devices_arr, i);
        device_config_t temp_dev;
        esp_err_t ret = parse_single_device(item, &temp_dev);
        if (ret == ESP_OK) {
            memcpy(&out_devices[valid_cnt], &temp_dev, sizeof(device_config_t));
            valid_cnt++;
        } else {
            ESP_LOGW(TAG, "跳过解析失败的第 %d 个设备项", i + 1);
        }
    }

    cJSON_Delete(root);
    *out_count = valid_cnt;

    ESP_LOGI(TAG, "成功解析 %d 个设备配置", valid_cnt);
    return ESP_OK;
}

esp_err_t config_parser_serialize_json(const device_config_t *devices, 
                                      uint8_t count, 
                                      char *out_buf, 
                                      size_t max_len)
{
    if (devices == NULL || out_buf == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return ESP_ERR_NO_MEM;

    cJSON *devices_arr = cJSON_CreateArray();
    if (devices_arr == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToObject(root, "devices", devices_arr);

    for (int i = 0; i < count; i++) {
        cJSON *dev_item = cJSON_CreateObject();
        cJSON_AddStringToObject(dev_item, "name", devices[i].name);
        cJSON_AddNumberToObject(dev_item, "slave_id", devices[i].slave_id);

        cJSON *reg_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(reg_obj, "address", devices[i].register_addr);
        cJSON_AddStringToObject(reg_obj, "type", devices[i].reg_type);
        cJSON_AddItemToObject(dev_item, "register", reg_obj);

        cJSON *data_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(data_obj, "type", devices[i].data_type);
        cJSON_AddNumberToObject(data_obj, "scale", devices[i].scale);
        cJSON_AddItemToObject(dev_item, "data", data_obj);

        cJSON_AddNumberToObject(dev_item, "period", devices[i].period);

        cJSON_AddItemToArray(devices_arr, dev_item);
    }

    char *rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (rendered == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (strlen(rendered) >= max_len) {
        free(rendered);
        return ESP_ERR_NO_MEM;
    }

    strncpy(out_buf, rendered, max_len - 1);
    out_buf[max_len - 1] = '\0';
    free(rendered);

    return ESP_OK;
}
