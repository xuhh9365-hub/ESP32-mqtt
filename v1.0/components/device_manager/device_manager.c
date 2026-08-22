#include "device_internal.h"
#include "modbus.h"
#include "data_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "DEVICE_MANAGER";
static device_manager_context_t s_device_ctx = {0};

device_manager_context_t *device_manager_get_context(void)
{
    return &s_device_ctx;
}

esp_err_t device_manager_init(void)
{
    ESP_LOGI(TAG, "正在初始化设备管理器 (双锁隔离 & 实例生命周期保护)...");

    // 1. 初始化实例表互斥锁
    if (s_device_ctx.instance_lock == NULL) {
        s_device_ctx.instance_lock = xSemaphoreCreateMutex();
        if (s_device_ctx.instance_lock == NULL) {
            ESP_LOGE(TAG, "创建 instance_lock 互斥锁失败");
            return ESP_ERR_NO_MEM;
        }
    }

    // 2. 初始化物理总线互斥锁
    if (s_device_ctx.bus_lock == NULL) {
        s_device_ctx.bus_lock = xSemaphoreCreateMutex();
        if (s_device_ctx.bus_lock == NULL) {
            ESP_LOGE(TAG, "创建 bus_lock 互斥锁失败");
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_device_ctx.instance_lock, portMAX_DELAY);
    s_device_ctx.device_count = 0;
    s_device_ctx.is_running = false;
    s_device_ctx.scheduler_task_handle = NULL;
    s_device_ctx.bus_wait_count = 0;
    s_device_ctx.bus_timeout_count = 0;
    s_device_ctx.is_bus_busy = false;
    memset(s_device_ctx.instances, 0, sizeof(s_device_ctx.instances));
    xSemaphoreGive(s_device_ctx.instance_lock);

    // 从 config_manager 中装载静态设备配置
    int dev_num = config_get_device_num();
    if (dev_num > 0) {
        for (int i = 0; i < dev_num; i++) {
            device_config_t dev_cfg;
            if (config_get_device(i, &dev_cfg) == ESP_OK) {
                device_manager_add_device(&dev_cfg);
            }
        }
    }

    // 注册运行时重载钩子到 config_manager
    config_manager_register_reload_hook(device_manager_apply_config);

    ESP_LOGI(TAG, "设备管理器初始化成功，共实例化 %u 个运行态设备", s_device_ctx.device_count);
    return ESP_OK;
}

esp_err_t device_manager_add_device(const device_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_device_ctx.instance_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_device_ctx.instance_lock, portMAX_DELAY);

    // 1. 检查是否已存在同名设备 (支持更新)
    int target_slot = -1;
    for (int i = 0; i < DEVICE_MANAGER_MAX_DEVICES; i++) {
        if (s_device_ctx.instances[i].is_valid && 
            strncmp(s_device_ctx.instances[i].config.name, config->name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
            target_slot = i;
            break;
        }
    }

    // 2. 若不存在，寻找空闲槽位
    if (target_slot < 0) {
        for (int i = 0; i < DEVICE_MANAGER_MAX_DEVICES; i++) {
            if (!s_device_ctx.instances[i].is_valid) {
                target_slot = i;
                break;
            }
        }
    }

    if (target_slot < 0) {
        xSemaphoreGive(s_device_ctx.instance_lock);
        ESP_LOGE(TAG, "设备实例池已满 (最大 %d)", DEVICE_MANAGER_MAX_DEVICES);
        return ESP_ERR_NO_MEM;
    }

    device_instance_t *inst = &s_device_ctx.instances[target_slot];
    if (!inst->is_valid) {
        s_device_ctx.device_count++;
    }

    // 槽位代数自增，防止旧的异步写操作污染重新分配的实例
    inst->generation++;
    inst->is_valid = true;
    inst->is_enabled = true;
    inst->config = *config;
    inst->status = DATA_STATUS_INVALID;
    memset(&inst->stats, 0, sizeof(inst->stats));

    TickType_t now = xTaskGetTickCount();
    inst->next_poll_tick = now + pdMS_TO_TICKS(target_slot * 50);

    xSemaphoreGive(s_device_ctx.instance_lock);

    ESP_LOGI(TAG, " -> 实例化设备[%d, gen=%lu]: 名称=%s, 从站ID=%u, 采集寄存器=0x%04X, 周期=%lums, 测点数=%u",
             target_slot, (unsigned long)inst->generation, config->name, (unsigned int)config->slave_id, 
             (unsigned int)config->register_addr, (unsigned long)config->period, config->metric_count);

    // 同步生命周期事件至 data_manager
    data_manager_handle_device_event(DEVICE_EVENT_ADD, config->name);

    return ESP_OK;
}

esp_err_t device_manager_remove_device(const char *device_name)
{
    if (device_name == NULL || s_device_ctx.instance_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_device_ctx.instance_lock, portMAX_DELAY);
    for (int i = 0; i < DEVICE_MANAGER_MAX_DEVICES; i++) {
        if (s_device_ctx.instances[i].is_valid && 
            strncmp(s_device_ctx.instances[i].config.name, device_name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
            s_device_ctx.instances[i].is_valid = false;
            s_device_ctx.instances[i].is_enabled = false;
            s_device_ctx.instances[i].generation++; // 销毁时代数递增
            if (s_device_ctx.device_count > 0) {
                s_device_ctx.device_count--;
            }
            xSemaphoreGive(s_device_ctx.instance_lock);
            ESP_LOGI(TAG, "已安全移除设备: %s (槽位 %d 已失效)", device_name, i);

            // 同步生命周期事件至 data_manager，即时清理 LVC 快照
            data_manager_handle_device_event(DEVICE_EVENT_REMOVE, device_name);
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_device_ctx.instance_lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t device_manager_set_device_enabled(const char *device_name, bool enable)
{
    if (device_name == NULL || s_device_ctx.instance_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_device_ctx.instance_lock, portMAX_DELAY);
    for (int i = 0; i < DEVICE_MANAGER_MAX_DEVICES; i++) {
        if (s_device_ctx.instances[i].is_valid && 
            strncmp(s_device_ctx.instances[i].config.name, device_name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
            s_device_ctx.instances[i].is_enabled = enable;
            xSemaphoreGive(s_device_ctx.instance_lock);
            ESP_LOGI(TAG, "设备 [%s] 采集使能状态已修改为: %s", device_name, enable ? "使能" : "暂停");
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_device_ctx.instance_lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t device_manager_get_device_status(
    const char *device_name,
    device_stats_t *stats,
    data_status_t *status)
{
    if (device_name == NULL || s_device_ctx.instance_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_device_ctx.instance_lock, portMAX_DELAY);
    for (int i = 0; i < DEVICE_MANAGER_MAX_DEVICES; i++) {
        if (s_device_ctx.instances[i].is_valid && 
            strncmp(s_device_ctx.instances[i].config.name, device_name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
            if (stats != NULL) {
                *stats = s_device_ctx.instances[i].stats;
            }
            if (status != NULL) {
                *status = s_device_ctx.instances[i].status;
            }
            xSemaphoreGive(s_device_ctx.instance_lock);
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_device_ctx.instance_lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t device_manager_get_device_info(
    const char *device_name,
    device_info_t *info)
{
    if (device_name == NULL || info == NULL || s_device_ctx.instance_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_device_ctx.instance_lock, portMAX_DELAY);
    for (int i = 0; i < DEVICE_MANAGER_MAX_DEVICES; i++) {
        if (s_device_ctx.instances[i].is_valid && 
            strncmp(s_device_ctx.instances[i].config.name, device_name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
            
            // 安全 Copy-out 复制，不向外部泄漏内部指针
            strncpy(info->name, s_device_ctx.instances[i].config.name, CONFIG_DEVICE_NAME_MAX_LEN - 1);
            info->name[CONFIG_DEVICE_NAME_MAX_LEN - 1] = '\0';
            info->slave_id = s_device_ctx.instances[i].config.slave_id;
            info->register_addr = s_device_ctx.instances[i].config.register_addr;
            info->scale = s_device_ctx.instances[i].config.scale;
            info->period = s_device_ctx.instances[i].config.period;
            info->status = s_device_ctx.instances[i].status;
            info->enabled = s_device_ctx.instances[i].is_enabled;
            info->metric_count = s_device_ctx.instances[i].config.metric_count;

            xSemaphoreGive(s_device_ctx.instance_lock);
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_device_ctx.instance_lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t device_manager_get_bus_status(device_bus_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    status->busy = s_device_ctx.is_bus_busy;
    status->wait_count = s_device_ctx.bus_wait_count;
    status->timeout_count = s_device_ctx.bus_timeout_count;
    return ESP_OK;
}

esp_err_t device_manager_write_holding_register_ex(
    const char *device_name,
    uint16_t reg_addr,
    uint16_t value,
    uint32_t timeout_ms,
    bool high_priority)
{
    // 1. 参数有效性检查
    if (device_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_device_ctx.instance_lock == NULL || s_device_ctx.bus_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // 2. instance_lock 保护下查找设备并获取当前代数 (generation) 与在线状态
    xSemaphoreTake(s_device_ctx.instance_lock, portMAX_DELAY);
    int target_slot = -1;
    for (int i = 0; i < DEVICE_MANAGER_MAX_DEVICES; i++) {
        if (s_device_ctx.instances[i].is_valid && 
            strncmp(s_device_ctx.instances[i].config.name, device_name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
            target_slot = i;
            break;
        }
    }

    if (target_slot < 0) {
        xSemaphoreGive(s_device_ctx.instance_lock);
        ESP_LOGE(TAG, "写入失败: 未找到设备 [%s]", device_name);
        return ESP_ERR_NOT_FOUND;
    }

    device_instance_t *inst = &s_device_ctx.instances[target_slot];

    // 3. 离线保护拦截：若设备处于 OFFLINE 状态，立即拒绝执行
    if (inst->status == DATA_STATUS_OFFLINE) {
        xSemaphoreGive(s_device_ctx.instance_lock);
        ESP_LOGW(TAG, "写入被拦截: 设备 [%s] 当前处于 [离线] 状态", device_name);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t slave_id = inst->config.slave_id;
    uint32_t saved_generation = inst->generation; // 关键：快照保存发起时的实例代数

    // 4. 释放 instance_lock 内存锁
    xSemaphoreGive(s_device_ctx.instance_lock);

    // 5. 确定总线超时策略 (支持高优先级紧急控制)
    uint32_t actual_timeout_ms = timeout_ms;
    if (actual_timeout_ms == 0) {
        actual_timeout_ms = high_priority ? 1000 : BUS_LOCK_DEFAULT_TIMEOUT_MS;
    }

    s_device_ctx.bus_wait_count++;
    TickType_t wait_ticks = pdMS_TO_TICKS(actual_timeout_ms);

    if (xSemaphoreTake(s_device_ctx.bus_lock, wait_ticks) != pdPASS) {
        s_device_ctx.bus_timeout_count++;
        ESP_LOGE(TAG, "写入失败: 等待总线空闲超时 (%lu ms)", (unsigned long)actual_timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    s_device_ctx.is_bus_busy = true;

    // 6. 执行 Modbus 物理写入 (功能码 0x06)
    ESP_LOGI(TAG, "Write request -> 设备: %s, 从站ID: %u, 寄存器: 0x%04X, 写入值: %u",
             device_name, (unsigned int)slave_id, (unsigned int)reg_addr, (unsigned int)value);

    esp_err_t err = modbus_master_write_single_register(slave_id, reg_addr, value);

    s_device_ctx.is_bus_busy = false;

    // 7. 释放总线锁
    xSemaphoreGive(s_device_ctx.bus_lock);

    // 8. instance_lock 保护下进行代数 (generation) 安全校验并更新写操作统计
    int64_t now_ts = esp_timer_get_time() / 1000;
    xSemaphoreTake(s_device_ctx.instance_lock, portMAX_DELAY);

    // 校验槽位是否在写入期间被删除或替换
    if (!s_device_ctx.instances[target_slot].is_valid || 
        s_device_ctx.instances[target_slot].generation != saved_generation) {
        xSemaphoreGive(s_device_ctx.instance_lock);
        ESP_LOGW(TAG, "设备槽位 [%d] 代数已失效 (原代数 %lu, 当前代数 %lu)，跳过统计更新",
                 target_slot, (unsigned long)saved_generation, 
                 (unsigned long)s_device_ctx.instances[target_slot].generation);
        return err;
    }

    inst = &s_device_ctx.instances[target_slot];
    inst->stats.write_total_count++;
    inst->stats.last_write_timestamp_ms = now_ts;

    if (err == ESP_OK) {
        inst->stats.write_success_count++;
        ESP_LOGI(TAG, "Write success -> 设备 [%s] (ID=%u, 寄存器=0x%04X, 写入值=%u)",
                 device_name, (unsigned int)slave_id, (unsigned int)reg_addr, (unsigned int)value);
    } else {
        inst->stats.write_fail_count++;
        ESP_LOGE(TAG, "Write failed -> 设备 [%s] (ID=%u, 寄存器=0x%04X, 错误: %s)",
                 device_name, (unsigned int)slave_id, (unsigned int)reg_addr, esp_err_to_name(err));
    }
    xSemaphoreGive(s_device_ctx.instance_lock);

    return err;
}

esp_err_t device_manager_write_holding_register(
    const char *device_name,
    uint16_t reg_addr,
    uint16_t value,
    uint32_t timeout_ms)
{
    return device_manager_write_holding_register_ex(device_name, reg_addr, value, timeout_ms, false);
}

esp_err_t device_manager_write_metric(
    const char *device_name,
    const char *metric_name,
    sensor_val_t value,
    sensor_val_type_t type,
    uint32_t timeout_ms)
{
    if (device_name == NULL || metric_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_device_ctx.instance_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // 1. instance_lock 保护下查找设备及指定的控制测点配置
    xSemaphoreTake(s_device_ctx.instance_lock, portMAX_DELAY);
    int target_slot = -1;
    for (int i = 0; i < DEVICE_MANAGER_MAX_DEVICES; i++) {
        if (s_device_ctx.instances[i].is_valid && 
            strncmp(s_device_ctx.instances[i].config.name, device_name, CONFIG_DEVICE_NAME_MAX_LEN) == 0) {
            target_slot = i;
            break;
        }
    }

    if (target_slot < 0) {
        xSemaphoreGive(s_device_ctx.instance_lock);
        ESP_LOGE(TAG, "write_metric 失败: 未找到设备 [%s]", device_name);
        return ESP_ERR_NOT_FOUND;
    }

    device_instance_t *inst = &s_device_ctx.instances[target_slot];

    if (inst->status == DATA_STATUS_OFFLINE) {
        xSemaphoreGive(s_device_ctx.instance_lock);
        ESP_LOGW(TAG, "write_metric 被拦截: 设备 [%s] 处于 [离线] 状态", device_name);
        return ESP_ERR_INVALID_STATE;
    }

    // 查找 metric
    int metric_idx = -1;
    for (int m = 0; m < inst->config.metric_count; m++) {
        if (strncmp(inst->config.metrics[m].metric_name, metric_name, CONFIG_METRIC_NAME_MAX_LEN) == 0) {
            metric_idx = m;
            break;
        }
    }

    if (metric_idx < 0) {
        xSemaphoreGive(s_device_ctx.instance_lock);
        ESP_LOGE(TAG, "设备 [%s] 未配置名为 [%s] 的控制测点", device_name, metric_name);
        return ESP_ERR_NOT_FOUND;
    }

    device_metric_config_t m_cfg = inst->config.metrics[metric_idx];
    xSemaphoreGive(s_device_ctx.instance_lock);

    // 2. 提取物理工程量
    float phys_val = 0.0f;
    switch (type) {
    case VAL_TYPE_FLOAT:
        phys_val = value.f32;
        break;
    case VAL_TYPE_DOUBLE:
        phys_val = (float)value.f64;
        break;
    case VAL_TYPE_INT32:
        phys_val = (float)value.i32;
        break;
    case VAL_TYPE_UINT32:
        phys_val = (float)value.u32;
        break;
    case VAL_TYPE_BOOL:
        phys_val = value.b_val ? 1.0f : 0.0f;
        break;
    default:
        phys_val = value.f32;
        break;
    }

    // 3. 安全量程边界校验
    if (phys_val < m_cfg.min_value || phys_val > m_cfg.max_value) {
        ESP_LOGE(TAG, "设定值 %.2f 超出测点 [%s.%s] 安全量程 [%.2f ~ %.2f]",
                 phys_val, device_name, metric_name, m_cfg.min_value, m_cfg.max_value);
        return ESP_ERR_INVALID_ARG;
    }

    // 4. 逆向缩放换算为寄存器 16 位整型字 (raw = phys / scale)
    float scale = (fabs((double)m_cfg.scale) > 1e-6) ? m_cfg.scale : 1.0f;
    float raw_calc = phys_val / scale;
    uint16_t raw_reg_val = (uint16_t)(raw_calc + (raw_calc >= 0 ? 0.5f : -0.5f));

    ESP_LOGI(TAG, "语义写入换算 -> 测点: %s.%s, 物理量: %.2f => 寄存器: 0x%04X, 原始值: %u (scale=%.4f)",
             device_name, metric_name, phys_val, m_cfg.write_register, raw_reg_val, scale);

    // 5. 转发给底层通用安全写接口
    return device_manager_write_holding_register_ex(
        device_name,
        m_cfg.write_register,
        raw_reg_val,
        timeout_ms,
        false
    );
}

esp_err_t device_manager_start(void)
{
    if (s_device_ctx.is_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "正在启动 Modbus Scheduler 集中调度器任务...");
    s_device_ctx.is_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        device_scheduler_task,
        "modbus_sched_task",
        4096,
        NULL,
        4,
        &s_device_ctx.scheduler_task_handle,
        tskNO_AFFINITY
    );

    if (ret != pdPASS) {
        s_device_ctx.is_running = false;
        ESP_LOGE(TAG, "创建 Modbus 调度器任务失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "设备管理器启动成功!");
    return ESP_OK;
}

esp_err_t device_manager_stop(void)
{
    if (!s_device_ctx.is_running) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "正在停止 Modbus 调度器任务...");
    s_device_ctx.is_running = false;

    if (s_device_ctx.scheduler_task_handle != NULL) {
        vTaskDelete(s_device_ctx.scheduler_task_handle);
        s_device_ctx.scheduler_task_handle = NULL;
    }

    return ESP_OK;
}
