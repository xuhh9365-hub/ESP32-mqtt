#include "config_validator.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

static const char *TAG = "CONFIG_VALIDATOR";

const char *config_val_err_to_str(config_val_err_t err)
{
    switch (err) {
    case CONFIG_VAL_OK:
        return "OK";
    case CONFIG_VAL_ERR_DEVICE_COUNT:
        return "DEVICE_COUNT_INVALID";
    case CONFIG_VAL_ERR_DEVICE_NAME_EMPTY:
        return "DEVICE_NAME_EMPTY";
    case CONFIG_VAL_ERR_DEVICE_NAME_INVALID:
        return "DEVICE_NAME_INVALID";
    case CONFIG_VAL_ERR_DEVICE_NAME_DUP:
        return "DEVICE_NAME_DUPLICATE";
    case CONFIG_VAL_ERR_SLAVE_ID_INVALID:
        return "SLAVE_ID_OUT_OF_RANGE";
    case CONFIG_VAL_ERR_PERIOD_INVALID:
        return "PERIOD_OUT_OF_RANGE";
    case CONFIG_VAL_ERR_SCALE_ZERO:
        return "SCALE_IS_ZERO";
    case CONFIG_VAL_ERR_METRIC_COUNT:
        return "METRIC_COUNT_EXCEEDED";
    case CONFIG_VAL_ERR_METRIC_NAME_EMPTY:
        return "METRIC_NAME_EMPTY";
    case CONFIG_VAL_ERR_METRIC_NAME_INVALID:
        return "METRIC_NAME_INVALID";
    case CONFIG_VAL_ERR_METRIC_NAME_DUP:
        return "METRIC_NAME_DUPLICATE";
    case CONFIG_VAL_ERR_METRIC_RANGE_INVALID:
        return "METRIC_RANGE_INVALID";
    default:
        return "UNKNOWN_ERROR";
    }
}

/**
 * @brief 检查字符串是否为合法的标识符 (仅包含字母、数字、下划线、短横线)
 */
static bool is_valid_identifier(const char *str, size_t max_len)
{
    if (str == NULL || str[0] == '\0') {
        return false;
    }
    size_t len = 0;
    while (str[len] != '\0') {
        if (len >= max_len) {
            return false;
        }
        char c = str[len];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') {
            return false;
        }
        len++;
    }
    return len > 0;
}

static void fill_result(
    config_validation_result_t *out_result,
    config_val_err_t err_code,
    const char *target,
    const char *detail)
{
    if (out_result != NULL) {
        out_result->err_code = err_code;
        strncpy(out_result->err_target, (target != NULL) ? target : "", sizeof(out_result->err_target) - 1);
        out_result->err_target[sizeof(out_result->err_target) - 1] = '\0';
        strncpy(out_result->err_detail, (detail != NULL) ? detail : "", sizeof(out_result->err_detail) - 1);
        out_result->err_detail[sizeof(out_result->err_detail) - 1] = '\0';
    }
}

esp_err_t config_validator_check(
    const device_config_t *devices,
    uint8_t count,
    config_validation_result_t *out_result)
{
    if (out_result != NULL) {
        memset(out_result, 0, sizeof(config_validation_result_t));
        out_result->err_code = CONFIG_VAL_OK;
    }

    if (devices == NULL) {
        fill_result(out_result, CONFIG_VAL_ERR_DEVICE_COUNT, "NULL", "设备配置数组指针为 NULL");
        ESP_LOGE(TAG, "配置校验失败: 设备数组指针为 NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 1. 校验设备总数量 (最少 1 个，最大 CONFIG_MAX_DEVICES = 16)
    if (count == 0 || count > CONFIG_MAX_DEVICES) {
        char detail[128];
        snprintf(detail, sizeof(detail), "设备数量超出范围 1~%d (当前: %u)", CONFIG_MAX_DEVICES, count);
        fill_result(out_result, CONFIG_VAL_ERR_DEVICE_COUNT, "global", detail);
        ESP_LOGE(TAG, "配置校验失败: %s", detail);
        return ESP_ERR_INVALID_ARG;
    }

    // 2. 逐一遍历每个设备进行深度参数与测点校验
    for (uint8_t i = 0; i < count; i++) {
        const device_config_t *dev = &devices[i];

        // 2.1 校验设备名称 (非空与字符合法性)
        if (dev->name[0] == '\0') {
            fill_result(out_result, CONFIG_VAL_ERR_DEVICE_NAME_EMPTY, "device[]", "设备名称不能为空");
            ESP_LOGE(TAG, "配置校验失败: 第 %u 个设备名称为空", i);
            return ESP_ERR_INVALID_ARG;
        }

        if (!is_valid_identifier(dev->name, CONFIG_DEVICE_NAME_MAX_LEN)) {
            char detail[128];
            snprintf(detail, sizeof(detail), "设备名称非法或超长: %s", dev->name);
            fill_result(out_result, CONFIG_VAL_ERR_DEVICE_NAME_INVALID, dev->name, detail);
            ESP_LOGE(TAG, "配置校验失败: %s", detail);
            return ESP_ERR_INVALID_ARG;
        }

        // 2.2 检查设备名称全局唯一性 (不允许同名设备)
        for (uint8_t j = 0; j < i; j++) {
            if (strncmp(dev->name, devices[j].name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
                char detail[128];
                snprintf(detail, sizeof(detail), "发现重复的设备名称: %s", dev->name);
                fill_result(out_result, CONFIG_VAL_ERR_DEVICE_NAME_DUP, dev->name, detail);
                ESP_LOGE(TAG, "配置校验失败: %s", detail);
                return ESP_ERR_INVALID_ARG;
            }
        }

        // 2.3 校验 Modbus 从站 ID (1 ~ 247)
        if (dev->slave_id < 1 || dev->slave_id > 247) {
            char detail[128];
            snprintf(detail, sizeof(detail), "从站ID超出范围 1~247 (当前: %u)", (unsigned int)dev->slave_id);
            fill_result(out_result, CONFIG_VAL_ERR_SLAVE_ID_INVALID, dev->name, detail);
            ESP_LOGE(TAG, "设备 [%s] 配置校验失败: %s", dev->name, detail);
            return ESP_ERR_INVALID_ARG;
        }

        // 2.4 校验采集周期 (50ms ~ 60000ms)
        if (dev->period < 50 || dev->period > 60000) {
            char detail[128];
            snprintf(detail, sizeof(detail), "采集周期超出范围 50~60000ms (当前: %lu)", (unsigned long)dev->period);
            fill_result(out_result, CONFIG_VAL_ERR_PERIOD_INVALID, dev->name, detail);
            ESP_LOGE(TAG, "设备 [%s] 配置校验失败: %s", dev->name, detail);
            return ESP_ERR_INVALID_ARG;
        }

        // 2.5 校验采集换算倍率 scale (不能为 0)
        if (fabsf(dev->scale) < 1e-6f) {
            fill_result(out_result, CONFIG_VAL_ERR_SCALE_ZERO, dev->name, "采集换算倍率 (scale) 不能为 0");
            ESP_LOGE(TAG, "设备 [%s] 配置校验失败: scale 不能为 0", dev->name);
            return ESP_ERR_INVALID_ARG;
        }

        // 2.6 校验控制测点 (Metrics) 数量
        if (dev->metric_count > CONFIG_MAX_METRICS_PER_DEVICE) {
            char detail[128];
            snprintf(detail, sizeof(detail), "测点数超过上限 %d (当前: %u)", 
                     CONFIG_MAX_METRICS_PER_DEVICE, dev->metric_count);
            fill_result(out_result, CONFIG_VAL_ERR_METRIC_COUNT, dev->name, detail);
            ESP_LOGE(TAG, "设备 [%s] 配置校验失败: %s", dev->name, detail);
            return ESP_ERR_INVALID_ARG;
        }

        // 2.7 逐项校验控制测点详情
        for (uint8_t m = 0; m < dev->metric_count; m++) {
            const device_metric_config_t *metric = &dev->metrics[m];

            // 测点名称非空与合法性
            if (metric->metric_name[0] == '\0') {
                fill_result(out_result, CONFIG_VAL_ERR_METRIC_NAME_EMPTY, dev->name, "控制测点名称不能为空");
                ESP_LOGE(TAG, "设备 [%s] 测点[%u] 名称为空", dev->name, m);
                return ESP_ERR_INVALID_ARG;
            }

            if (!is_valid_identifier(metric->metric_name, CONFIG_METRIC_NAME_MAX_LEN)) {
                char detail[128];
                snprintf(detail, sizeof(detail), "测点名称非法: %s", metric->metric_name);
                fill_result(out_result, CONFIG_VAL_ERR_METRIC_NAME_INVALID, metric->metric_name, detail);
                ESP_LOGE(TAG, "设备 [%s] %s", dev->name, detail);
                return ESP_ERR_INVALID_ARG;
            }

            // 同设备内测点名唯一性
            for (uint8_t k = 0; k < m; k++) {
                if (strncmp(metric->metric_name, dev->metrics[k].metric_name, CONFIG_METRIC_NAME_MAX_LEN) == 0) {
                    char detail[128];
                    snprintf(detail, sizeof(detail), "同一设备内测点名称重复: %s", metric->metric_name);
                    fill_result(out_result, CONFIG_VAL_ERR_METRIC_NAME_DUP, metric->metric_name, detail);
                    ESP_LOGE(TAG, "设备 [%s] %s", dev->name, detail);
                    return ESP_ERR_INVALID_ARG;
                }
            }

            // 测点量程校验 (min < max)
            if (metric->min_value >= metric->max_value) {
                char detail[128];
                snprintf(detail, sizeof(detail), "测点量程下限必须小于上限");
                fill_result(out_result, CONFIG_VAL_ERR_METRIC_RANGE_INVALID, metric->metric_name, detail);
                ESP_LOGE(TAG, "设备 [%s] 测点 [%s] 量程下限 (%.2f) >= 上限 (%.2f)", 
                         dev->name, metric->metric_name, metric->min_value, metric->max_value);
                return ESP_ERR_INVALID_ARG;
            }

            // 测点换算倍率 scale (不能为 0)
            if (fabsf(metric->scale) < 1e-6f) {
                char detail[128];
                snprintf(detail, sizeof(detail), "测点换算倍率不能为 0");
                fill_result(out_result, CONFIG_VAL_ERR_SCALE_ZERO, metric->metric_name, detail);
                ESP_LOGE(TAG, "设备 [%s] 测点 [%s] scale 为 0", dev->name, metric->metric_name);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    ESP_LOGI(TAG, "配置深度校验通过: 共 %u 个设备配置，所有 Modbus 参数与测点均合法!", count);
    return ESP_OK;
}
