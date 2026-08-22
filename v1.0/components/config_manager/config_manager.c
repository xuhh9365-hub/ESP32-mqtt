#include "config_manager.h"
#include "config_parser.h"
#include "config_validator.h"
#include "config_storage.h"
#include "config_default.h"
#include "cJSON.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "CONFIG_MANAGER";

#define CONFIG_JSON_BUFFER_SIZE  (2048)

/**
 * @brief 静态内存缓冲区 (零堆内存 malloc 开销)
 */
static char s_json_load_buf[CONFIG_JSON_BUFFER_SIZE];
static device_config_t s_parsed_devices[CONFIG_MAX_DEVICES];

/**
 * @brief 内部配置管理上下文
 */
typedef struct {
    device_config_t     devices[CONFIG_MAX_DEVICES];
    uint8_t             device_count;
    uint32_t            version;
    bool                is_initialized;
    SemaphoreHandle_t   mutex;
} config_manager_ctx_t;

static config_manager_ctx_t s_ctx = {
    .device_count = 0,
    .version = 0,
    .is_initialized = false,
    .mutex = NULL
};

/**
 * @brief 内部静态函数：原子更新 config_manager 内存运行态配置
 */
static esp_err_t config_apply_runtime_config(
    const device_config_t *devices,
    uint8_t count,
    uint32_t version)
{
    if (devices == NULL || count > CONFIG_MAX_DEVICES) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ctx.mutex != NULL) {
        if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGE(TAG, "获取配置互斥锁超时");
            return ESP_ERR_TIMEOUT;
        }
    }

    memset(s_ctx.devices, 0, sizeof(s_ctx.devices));
    memcpy(s_ctx.devices, devices, sizeof(device_config_t) * count);
    s_ctx.device_count = count;
    s_ctx.version = version;

    if (s_ctx.mutex != NULL) {
        xSemaphoreGive(s_ctx.mutex);
    }

    return ESP_OK;
}

/**
 * @brief 内部静态函数：从 NVS 持久化存储加载并恢复配置
 */
static esp_err_t config_load_from_storage(void)
{
    uint32_t loaded_ver = 0;
    esp_err_t err = config_storage_load(s_json_load_buf, sizeof(s_json_load_buf), &loaded_ver);
    if (err != ESP_OK) {
        return err;
    }

    // 1. JSON 反序列化
    uint8_t parsed_count = 0;
    err = config_parser_parse_json(s_json_load_buf, s_parsed_devices, CONFIG_MAX_DEVICES, &parsed_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 配置 JSON 解析失败: %s", esp_err_to_name(err));
        return err;
    }

    // 2. 深度 Schema 与参数合法性校验
    config_validation_result_t v_res;
    err = config_validator_check(s_parsed_devices, parsed_count, &v_res);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 配置校验被拒绝 [%s]: %s (%s)", 
                 config_val_err_to_str(v_res.err_code), v_res.err_detail, v_res.err_target);
        return err;
    }

    // 3. 应用至运行态内存
    config_apply_runtime_config(s_parsed_devices, parsed_count, loaded_ver);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " Loading configuration from NVS");
    ESP_LOGI(TAG, " version : %lu", (unsigned long)loaded_ver);
    ESP_LOGI(TAG, " devices : %u", parsed_count);
    ESP_LOGI(TAG, " CRC check PASS");
    ESP_LOGI(TAG, " Configuration restored successfully");
    ESP_LOGI(TAG, "========================================");

    return ESP_OK;
}

/**
 * @brief 内部静态函数：加载出厂默认配置并写入 NVS 修复
 */
static esp_err_t config_load_factory_default(void)
{
    uint8_t count = 0;
    esp_err_t err = config_parser_parse_json(DEFAULT_DEVICE_CONFIG_JSON, s_parsed_devices, CONFIG_MAX_DEVICES, &count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "出厂默认配置 JSON 解析失败!");
        return err;
    }

    config_validation_result_t v_res;
    err = config_validator_check(s_parsed_devices, count, &v_res);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "出厂默认配置校验非法 [%s]: %s (%s)",
                 config_val_err_to_str(v_res.err_code), v_res.err_detail, v_res.err_target);
        return err;
    }

    // 保存出厂配置到 NVS，生成新的版本号
    uint32_t saved_ver = 0;
    err = config_storage_save(DEFAULT_DEVICE_CONFIG_JSON, strlen(DEFAULT_DEVICE_CONFIG_JSON), &saved_ver);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "写入默认配置到 NVS 警告 (可能为只读环境): %s", esp_err_to_name(err));
        saved_ver = 1;
    }

    config_apply_runtime_config(s_parsed_devices, count, saved_ver);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " Factory configuration loaded");
    ESP_LOGI(TAG, " devices: %u", count);
    ESP_LOGI(TAG, " Saved to NVS, new version: %lu", (unsigned long)saved_ver);
    ESP_LOGI(TAG, "========================================");

    return ESP_OK;
}

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

    // 1. 初始化 NVS Storage 存储层
    config_storage_init();

    // 2. 尝试从 NVS 恢复持久化配置
    esp_err_t ret = config_load_from_storage();
    if (ret == ESP_OK) {
        s_ctx.is_initialized = true;
        ESP_LOGI(TAG, "加载 NVS 配置成功, 当前设备数: %d, 版本: %lu", 
                 s_ctx.device_count, (unsigned long)s_ctx.version);
        return ESP_OK;
    }

    // 3. NVS 恢复失败或首次上电 -> 回退加载出厂默认配置并自愈写入 NVS
    ESP_LOGW(TAG, "Persistent configuration unavailable (ret: %s), Fallback to factory configuration", 
             esp_err_to_name(ret));

    ret = config_load_factory_default();
    if (ret == ESP_OK) {
        s_ctx.is_initialized = true;
        ESP_LOGI(TAG, "已回退并成功初始化默认配置, 当前设备数: %d, 版本: %lu", 
                 s_ctx.device_count, (unsigned long)s_ctx.version);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "配置管理器初始化严重错误: 出厂默认配置加载失败 (%s)", esp_err_to_name(ret));
    return ret;
}

esp_err_t config_manager_apply_update(
    const char *json_str,
    uint32_t *out_version)
{
    // 1. 输入参数检查
    if (json_str == NULL) {
        ESP_LOGE(TAG, "配置更新失败: json_str 指针为 NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 2. 检查 JSON 根对象与版本信息 (防止回滚至陈旧版本)
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "Configuration update rejected\n reason:\n JSON_SYNTAX_ERROR\n target:\n root");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *ver_item = cJSON_GetObjectItem(root, "version");
    uint32_t incoming_version = 0;
    if (cJSON_IsNumber(ver_item)) {
        incoming_version = (uint32_t)ver_item->valueint;
    }
    cJSON_Delete(root);

    if (incoming_version > 0 && incoming_version < s_ctx.version) {
        ESP_LOGW(TAG, "Reject stale configuration\n incoming version: %lu\n current version: %lu",
                 (unsigned long)incoming_version, (unsigned long)s_ctx.version);
        return ESP_ERR_INVALID_STATE;
    }

    // 3. JSON 解析为设备配置结构体数组
    uint8_t new_count = 0;
    esp_err_t ret = config_parser_parse_json(json_str, s_parsed_devices, CONFIG_MAX_DEVICES, &new_count);
    if (ret != ESP_OK || new_count == 0) {
        ESP_LOGE(TAG, "Configuration update rejected\n reason:\n parse_devices_failed\n target:\n devices");
        return (ret != ESP_OK) ? ret : ESP_ERR_INVALID_ARG;
    }

    // 4. 深度参数合法性与量程校验 (不通过则坚决拒绝，绝不破坏现有 NVS)
    config_validation_result_t v_res;
    ret = config_validator_check(s_parsed_devices, new_count, &v_res);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Configuration update rejected\n reason:\n validator failed [%s: %s]\n target:\n %s",
                 config_val_err_to_str(v_res.err_code), v_res.err_detail, v_res.err_target);
        return ret;
    }

    // 5. 持久化写入 NVS 并获取自增版本号
    uint32_t old_version = s_ctx.version;
    uint32_t new_version = 0;
    ret = config_storage_save(json_str, strlen(json_str), &new_version);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Configuration update rejected\n reason:\n NVS_SAVE_FAILED\n target:\n storage");
        return ret;
    }

    // 6. 原子更新内存中运行态配置
    config_apply_runtime_config(s_parsed_devices, new_count, new_version);

    if (out_version != NULL) {
        *out_version = new_version;
    }

    // 7. 打印配置更新成功日志
    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "Applying new configuration");
    ESP_LOGI(TAG, "old version: %lu", (unsigned long)old_version);
    ESP_LOGI(TAG, "new version: %lu", (unsigned long)new_version);
    ESP_LOGI(TAG, "validation PASS");
    ESP_LOGI(TAG, "devices: %u", (unsigned int)new_count);
    ESP_LOGI(TAG, "Saved to NVS");
    ESP_LOGI(TAG, "Configuration update success");
    ESP_LOGI(TAG, "================================");

    return ESP_OK;
}

esp_err_t config_manager_validate_json(const char *json_str, void *out_result)
{
    if (json_str == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_validation_result_t *res = (config_validation_result_t *)out_result;
    if (res != NULL) {
        memset(res, 0, sizeof(config_validation_result_t));
    }

    // 1. JSON 反序列化解析
    static device_config_t s_temp_validate_devices[CONFIG_MAX_DEVICES];
    uint8_t parsed_count = 0;
    esp_err_t err = config_parser_parse_json(json_str, s_temp_validate_devices, CONFIG_MAX_DEVICES, &parsed_count);
    if (err != ESP_OK || parsed_count == 0) {
        if (res != NULL) {
            res->err_code = CONFIG_VAL_ERR_DEVICE_COUNT;
            strncpy(res->err_target, "json_root", sizeof(res->err_target) - 1);
            strncpy(res->err_detail, "JSON 语法解析失败或未包含有效 devices 数组", sizeof(res->err_detail) - 1);
        }
        return (err != ESP_OK) ? err : ESP_ERR_INVALID_ARG;
    }

    // 2. 深度 Schema 与业务参数合法性校验
    config_validation_result_t val_diag;
    err = config_validator_check(s_temp_validate_devices, parsed_count, &val_diag);
    if (err != ESP_OK) {
        if (res != NULL) {
            *res = val_diag;
        }
        return err;
    }

    return ESP_OK;
}

static config_reload_hook_t s_reload_hook = NULL;

esp_err_t config_manager_register_reload_hook(config_reload_hook_t hook)
{
    s_reload_hook = hook;
    ESP_LOGI(TAG, "已成功注册设备热重载通知钩子 (device reload hook)");
    return ESP_OK;
}

esp_err_t config_manager_apply_json(const char *json, uint32_t *new_version)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 阶段 1: JSON 反序列化解析
    static device_config_t s_new_devices[CONFIG_MAX_DEVICES];
    uint8_t new_count = 0;
    esp_err_t err = config_parser_parse_json(json, s_new_devices, CONFIG_MAX_DEVICES, &new_count);
    if (err != ESP_OK || new_count == 0) {
        ESP_LOGE(TAG, "config_manager_apply_json: JSON 语法或结构解析失败");
        return (err != ESP_OK) ? err : ESP_ERR_INVALID_ARG;
    }

    // 阶段 2: 配置深度合法性校验
    config_validation_result_t v_res;
    err = config_validator_check(s_new_devices, new_count, &v_res);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_manager_apply_json 校验被拒绝 [%s]: %s (target: %s)",
                 config_val_err_to_str(v_res.err_code), v_res.err_detail, v_res.err_target);
        return err;
    }

    // 阶段 3: 设备热加载 (更新运行态 Modbus 调度槽位)
    if (s_reload_hook != NULL) {
        ESP_LOGI(TAG, "正在调用 device_manager 执行设备差异热重载 (Diff Apply)...");
        err = s_reload_hook(s_new_devices, new_count);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "设备热重载返回警告: %s", esp_err_to_name(err));
        }
    }

    // 阶段 4: 保存到 NVS 持久化存储
    uint32_t saved_ver = 0;
    err = config_storage_save(json, strlen(json), &saved_ver);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_manager_apply_json: 写入 NVS 持久化存储失败: %s", esp_err_to_name(err));
        return err;
    }

    // 阶段 5: 原子更新 config_manager RAM 中的全局运行配置
    config_apply_runtime_config(s_new_devices, new_count, saved_ver);

    if (new_version != NULL) {
        *new_version = saved_ver;
    }

    ESP_LOGI(TAG, "✅ [config_manager_apply_json] 配置已成功落盘 NVS 并热重载至设备管理! 新版本: v%lu, 设备数: %u",
             (unsigned long)saved_ver, (unsigned int)new_count);

    return ESP_OK;
}

uint32_t config_manager_get_version(void)
{
    return s_ctx.version;
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
    uint32_t out_ver = 0;
    return config_manager_apply_update(json_str, &out_ver);
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
        cJSON *root = cJSON_CreateObject();
        if (root == NULL) {
            xSemaphoreGive(s_ctx.mutex);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddNumberToObject(root, "version", (double)s_ctx.version);
        cJSON *devices_arr = cJSON_CreateArray();
        for (int i = 0; i < s_ctx.device_count; i++) {
            cJSON *dev_item = cJSON_CreateObject();
            cJSON_AddStringToObject(dev_item, "name", s_ctx.devices[i].name);
            cJSON_AddNumberToObject(dev_item, "slave_id", s_ctx.devices[i].slave_id);
            cJSON_AddNumberToObject(dev_item, "period", s_ctx.devices[i].period);

            cJSON *reg_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(reg_obj, "address", s_ctx.devices[i].register_addr);
            cJSON_AddStringToObject(reg_obj, "type", s_ctx.devices[i].reg_type);
            cJSON_AddItemToObject(dev_item, "register", reg_obj);

            cJSON *data_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(data_obj, "type", s_ctx.devices[i].data_type);
            cJSON_AddNumberToObject(data_obj, "scale", (double)s_ctx.devices[i].scale);
            cJSON_AddItemToObject(dev_item, "data", data_obj);

            if (s_ctx.devices[i].metric_count > 0) {
                cJSON *m_arr = cJSON_CreateArray();
                for (int m = 0; m < s_ctx.devices[i].metric_count; m++) {
                    cJSON *m_obj = cJSON_CreateObject();
                    cJSON_AddStringToObject(m_obj, "name", s_ctx.devices[i].metrics[m].metric_name);
                    cJSON_AddNumberToObject(m_obj, "write_register", s_ctx.devices[i].metrics[m].write_register);
                    cJSON_AddNumberToObject(m_obj, "scale", (double)s_ctx.devices[i].metrics[m].scale);
                    cJSON_AddNumberToObject(m_obj, "min", (double)s_ctx.devices[i].metrics[m].min_value);
                    cJSON_AddNumberToObject(m_obj, "max", (double)s_ctx.devices[i].metrics[m].max_value);
                    cJSON_AddItemToArray(m_arr, m_obj);
                }
                cJSON_AddItemToObject(dev_item, "metrics", m_arr);
            }
            cJSON_AddItemToArray(devices_arr, dev_item);
        }
        cJSON_AddItemToObject(root, "devices", devices_arr);
        char *rendered = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        xSemaphoreGive(s_ctx.mutex);

        if (rendered == NULL) {
            return ESP_ERR_NO_MEM;
        }
        if (strlen(rendered) >= buf_len) {
            free(rendered);
            return ESP_ERR_NO_MEM;
        }
        strncpy(json_buf, rendered, buf_len - 1);
        json_buf[buf_len - 1] = '\0';
        free(rendered);
        ret = ESP_OK;
    } else {
        ret = ESP_ERR_TIMEOUT;
    }

    return ret;
}

esp_err_t config_manager_restore_factory(void)
{
    ESP_LOGW(TAG, "正在恢复出厂默认配置 (擦除 NVS)...");
    config_storage_clear();
    return config_load_factory_default();
}

int config_save(void)
{
    if (!s_ctx.is_initialized) {
        return -1;
    }

    char json_buf[CONFIG_JSON_BUFFER_SIZE];
    esp_err_t ret = config_export_to_json(json_buf, sizeof(json_buf));
    if (ret != ESP_OK) {
        return -1;
    }

    uint32_t new_ver = 0;
    ret = config_storage_save(json_buf, strlen(json_buf), &new_ver);
    if (ret == ESP_OK) {
        s_ctx.version = new_ver;
        return 0;
    }
    return -1;
}
