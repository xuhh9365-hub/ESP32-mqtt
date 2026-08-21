#include "device_manager.h"
#include "device_internal.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "DEVICE_MANAGER";

static device_manager_context_t s_ctx = {
    .device_count = 0,
    .is_initialized = false,
    .is_running = false,
    .scheduler_task_handle = NULL,
    .lock = NULL
};

device_manager_context_t *device_manager_get_context(void)
{
    return &s_ctx;
}

esp_err_t device_manager_init(void)
{
    if (s_ctx.is_initialized) {
        ESP_LOGW(TAG, "设备管理器已初始化，跳过重复初始化");
        return ESP_OK;
    }

    if (s_ctx.lock == NULL) {
        s_ctx.lock = xSemaphoreCreateMutex();
        if (s_ctx.lock == NULL) {
            ESP_LOGE(TAG, "创建互斥锁失败");
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "正在初始化设备管理器 (从 config_manager 读取配置)...");

    // 1. 获取配置管理器中的设备总数
    int cfg_num = config_get_device_num();
    if (cfg_num <= 0) {
        ESP_LOGW(TAG, "config_manager 中暂无有效设备配置 (num=%d)", cfg_num);
        s_ctx.device_count = 0;
        s_ctx.is_initialized = true;
        return ESP_OK;
    }

    if (cfg_num > DEVICE_MANAGER_MAX_DEVICES) {
        ESP_LOGW(TAG, "设备数 %d 超过最大容量 %d，将仅实例化前 %d 个设备", 
                 cfg_num, DEVICE_MANAGER_MAX_DEVICES, DEVICE_MANAGER_MAX_DEVICES);
        cfg_num = DEVICE_MANAGER_MAX_DEVICES;
    }

    // 2. 依次读取配置并实例化
    memset(s_ctx.instances, 0, sizeof(s_ctx.instances));
    uint8_t valid_inst_count = 0;
    TickType_t base_tick = xTaskGetTickCount();

    for (int i = 0; i < cfg_num; i++) {
        device_config_t cfg;
        esp_err_t ret = config_get_device(i, &cfg);
        if (ret == ESP_OK) {
            device_instance_t *inst = &s_ctx.instances[valid_inst_count];
            memcpy(&inst->config, &cfg, sizeof(device_config_t));
            inst->status = DEVICE_STATUS_UNKNOWN;
            memset(&inst->stats, 0, sizeof(device_statistics_t));
            
            // 错开每个设备的初始采集时刻 (以 100ms 间隔错峰)，防止启动瞬间总线拥堵
            inst->next_poll_tick = base_tick + pdMS_TO_TICKS(valid_inst_count * 100);
            inst->is_valid = true;

            ESP_LOGI(TAG, " -> 实例化设备[%d]: 名称=%s, 从站ID=%d, 寄存器=0x%04X, 周期=%lums",
                     valid_inst_count,
                     inst->config.name,
                     inst->config.slave_id,
                     inst->config.register_addr,
                     (unsigned long)inst->config.period);

            valid_inst_count++;
        } else {
            ESP_LOGE(TAG, "读取第 %d 个设备配置失败", i);
        }
    }

    s_ctx.device_count = valid_inst_count;
    s_ctx.is_initialized = true;

    ESP_LOGI(TAG, "设备管理器初始化成功，共实例化 %d 个运行态设备", s_ctx.device_count);
    return ESP_OK;
}

esp_err_t device_manager_start(void)
{
    if (!s_ctx.is_initialized) {
        ESP_LOGE(TAG, "无法启动: 设备管理器尚未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ctx.is_running) {
        ESP_LOGW(TAG, "设备管理器已处于运行状态");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "正在启动 Modbus Scheduler 集中调度器任务...");

    s_ctx.is_running = true;

    // 创建集中式采集调度任务 (优先级 4，堆栈 4096 字节)
    BaseType_t ret = xTaskCreate(
        device_scheduler_task,
        "mb_scheduler",
        4096,
        NULL,
        4,
        &s_ctx.scheduler_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建 modbus_scheduler_task 任务失败");
        s_ctx.is_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "设备管理器启动成功!");
    return ESP_OK;
}

int device_manager_get_device_count(void)
{
    if (!s_ctx.is_initialized) return -1;
    return (int)s_ctx.device_count;
}

esp_err_t device_manager_get_device_instance(int index, device_instance_t *out_inst)
{
    if (out_inst == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_ctx.is_initialized) return ESP_ERR_INVALID_STATE;

    if (index < 0 || index >= s_ctx.device_count) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(out_inst, &s_ctx.instances[index], sizeof(device_instance_t));
        xSemaphoreGive(s_ctx.lock);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t device_manager_get_instance_by_name(const char *name, device_instance_t *out_inst)
{
    if (name == NULL || out_inst == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_ctx.is_initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < s_ctx.device_count; i++) {
            if (strncmp(s_ctx.instances[i].config.name, name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
                memcpy(out_inst, &s_ctx.instances[i], sizeof(device_instance_t));
                ret = ESP_OK;
                break;
            }
        }
        xSemaphoreGive(s_ctx.lock);
    } else {
        ret = ESP_ERR_TIMEOUT;
    }

    return ret;
}
