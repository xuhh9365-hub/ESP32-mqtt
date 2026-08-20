#include "config_manager.h"
#include "config_parser.h"
#include "config_default.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "CONFIG_MANAGER";

/**
 * @brief 内部配置管理上下文
 */
typedef struct {
    device_config_t     devices[CONFIG_MAX_DEVICES];
    uint8_t             device_count;
    bool                is_initialized;
    SemaphoreHandle_t   mutex;
} config_manager_ctx_t;

static config_manager_ctx_t s_ctx = {
    .device_count = 0,
    .is_initialized = false,
    .mutex = NULL
};

esp_err_t config_manager_init(void)
{
    if (s_ctx.is_initialized) {
        ESP_LOGW(TAG, "配置管理器已初始化，跳过重复初始化");
        return ESP_OK;
    }

    if (s_ctx.mutex == NULL) {
        s_ctx.mutex = xSemaphoreCreateMutex();
        if (s_ctx.mutex == NULL) {
            ESP_LOGE(TAG, "创建互斥锁失败");
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "正在初始化配置管理器 (Phase 1: 加载内置默认配置)...");

    // Phase 1: 加载默认 JSON 配置
    esp_err_t ret = config_load_from_json(DEFAULT_DEVICE_CONFIG_JSON);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "默认配置解析失败: %s", esp_err_to_name(ret));
        return ret;
    }

    s_ctx.is_initialized = true;
    ESP_LOGI(TAG, "配置管理器初始化成功，当前加载设备数: %d", s_ctx.device_count);

    // 打印加载的设备摘要
    for (int i = 0; i < s_ctx.device_count; i++) {
        ESP_LOGI(TAG, " -> 设备[%d]: 名称=%s, 从站ID=%d, 寄存器=0x%04X, 类型=%s, 周期=%lums, 倍率=%.2f",
                 i,
                 s_ctx.devices[i].name,
                 s_ctx.devices[i].slave_id,
                 s_ctx.devices[i].register_addr,
                 s_ctx.devices[i].reg_type,
                 (unsigned long)s_ctx.devices[i].period,
                 s_ctx.devices[i].scale);
    }

    return ESP_OK;
}

int config_get_device_num(void)
{
    if (!s_ctx.is_initialized) {
        ESP_LOGE(TAG, "配置管理器尚未初始化");
        return -1;
    }

    int count = 0;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        count = (int)s_ctx.device_count;
        xSemaphoreGive(s_ctx.mutex);
    } else {
        ESP_LOGE(TAG, "获取互斥锁超时");
        return -1;
    }
    return count;
}

esp_err_t config_get_device(int index, device_config_t *device)
{
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ctx.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (index < 0 || index >= s_ctx.device_count) {
            ret = ESP_ERR_INVALID_ARG;
        } else {
            memcpy(device, &s_ctx.devices[index], sizeof(device_config_t));
        }
        xSemaphoreGive(s_ctx.mutex);
    } else {
        ret = ESP_ERR_TIMEOUT;
    }

    return ret;
}

esp_err_t config_get_device_by_name(const char *name, device_config_t *device)
{
    if (name == NULL || device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ctx.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < s_ctx.device_count; i++) {
            if (strncmp(s_ctx.devices[i].name, name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
                memcpy(device, &s_ctx.devices[i], sizeof(device_config_t));
                ret = ESP_OK;
                break;
            }
        }
        xSemaphoreGive(s_ctx.mutex);
    } else {
        ret = ESP_ERR_TIMEOUT;
    }

    return ret;
}

esp_err_t config_load_from_json(const char *json_str)
{
    if (json_str == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    device_config_t temp_devices[CONFIG_MAX_DEVICES];
    uint8_t temp_count = 0;

    // 先解析到临时数组进行完整校验，若出错不破坏当前内存配置
    esp_err_t ret = config_parser_parse_json(json_str, temp_devices, CONFIG_MAX_DEVICES, &temp_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "解析配置 JSON 失败，保持原有配置");
        return ret;
    }

    if (s_ctx.mutex != NULL) {
        if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGE(TAG, "更新配置获取互斥锁超时");
            return ESP_ERR_TIMEOUT;
        }
    }

    // 原子更新内存表
    memset(s_ctx.devices, 0, sizeof(s_ctx.devices));
    memcpy(s_ctx.devices, temp_devices, sizeof(device_config_t) * temp_count);
    s_ctx.device_count = temp_count;

    if (s_ctx.mutex != NULL) {
        xSemaphoreGive(s_ctx.mutex);
    }

    ESP_LOGI(TAG, "配置更新完成，当前有效设备数: %d", temp_count);
    return ESP_OK;
}

esp_err_t config_export_to_json(char *json_buf, size_t buf_len)
{
    if (json_buf == NULL || buf_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ctx.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        ret = config_parser_serialize_json(s_ctx.devices, s_ctx.device_count, json_buf, buf_len);
        xSemaphoreGive(s_ctx.mutex);
    } else {
        ret = ESP_ERR_TIMEOUT;
    }

    return ret;
}

int config_save(void)
{
    // Phase 1 预留接口（Phase 2 将调用 NVS 存储）
    ESP_LOGI(TAG, "config_save() 触发 (Phase 1 暂未启用 NVS 持久化)");
    return 0;
}
