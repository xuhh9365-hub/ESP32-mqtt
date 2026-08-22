#include "device_internal.h"
#include "modbus.h"
#include "data_convert.h"
#include "data_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "DEVICE_MANAGER";

/**
 * @brief 执行单设备的 Modbus 采样与数据流转 (双锁隔离机制 & Generation 生命周期防竞争)
 * 
 * @param slot_idx 设备实例槽位索引
 */
static void poll_single_device_by_slot(int slot_idx)
{
    device_manager_context_t *ctx = device_manager_get_context();

    // ==================== 阶段 1: instance_lock 保护下提取设备配置与代数 ====================
    device_config_t config;
    uint32_t saved_gen = 0;

    xSemaphoreTake(ctx->instance_lock, portMAX_DELAY);
    if (!ctx->instances[slot_idx].is_valid || !ctx->instances[slot_idx].is_enabled) {
        xSemaphoreGive(ctx->instance_lock);
        return;
    }
    config = ctx->instances[slot_idx].config;
    saved_gen = ctx->instances[slot_idx].generation;
    xSemaphoreGive(ctx->instance_lock);

    // ==================== 阶段 2: bus_lock 保护下执行物理 Modbus 读取 ====================
    uint16_t reg_val = 0;
    xSemaphoreTake(ctx->bus_lock, portMAX_DELAY);
    esp_err_t err = modbus_master_read_holding_registers(
        (uint8_t)config.slave_id,
        (uint16_t)config.register_addr,
        1,
        &reg_val
    );
    xSemaphoreGive(ctx->bus_lock);

    // ==================== 阶段 3: 记录物理时戳并在 instance_lock 下做代数校验与更新 ====================
    int64_t sample_ts = esp_timer_get_time() / 1000;
    data_status_t data_status = DATA_STATUS_OK;
    sensor_data_priority_t prio = DATA_PRIO_NORMAL;

    xSemaphoreTake(ctx->instance_lock, portMAX_DELAY);
    device_instance_t *inst = &ctx->instances[slot_idx];

    // 生命周期安全校验：若在物理 Modbus 采集期间槽位已被删除或被更新换代，立即丢弃本次结果
    if (!inst->is_valid || inst->generation != saved_gen) {
        xSemaphoreGive(ctx->instance_lock);
        ESP_LOGW(TAG, "device [%s] changed during poll (gen: %lu != %lu), discard result",
                 config.name, (unsigned long)saved_gen, (unsigned long)inst->generation);
        return;
    }

    inst->stats.total_poll_count++;

    if (err == ESP_OK) {
        inst->stats.success_count++;
        inst->stats.consecutive_fail_count = 0;
        inst->status = DATA_STATUS_OK;
        data_status = DATA_STATUS_OK;
    } else {
        inst->stats.fail_count++;
        inst->stats.consecutive_fail_count++;
        data_status = DATA_STATUS_TIMEOUT;

        if (inst->stats.consecutive_fail_count >= DEVICE_CONSECUTIVE_FAIL_LIMIT) {
            inst->status = DATA_STATUS_OFFLINE;
            data_status = DATA_STATUS_OFFLINE;
            prio = DATA_PRIO_ALARM;
            ESP_LOGW(TAG, "⚠️ 设备 [%s] (ID=%u) 连续通信失败 %d 次 (错误: %s)，已判定为 [离线]",
                     config.name, 
                     (unsigned int)config.slave_id, 
                     inst->stats.consecutive_fail_count,
                     esp_err_to_name(err));
        } else {
            ESP_LOGW(TAG, "设备 [%s] (ID=%u) 通信失败 (%d/%d, 错误: %s)",
                     config.name, 
                     (unsigned int)config.slave_id, 
                     inst->stats.consecutive_fail_count, 
                     DEVICE_CONSECUTIVE_FAIL_LIMIT,
                     esp_err_to_name(err));
        }
    }
    xSemaphoreGive(ctx->instance_lock);

    // ==================== 阶段 4: 构造标准原始采集包 sensor_raw_data_t ====================
    sensor_raw_data_t raw_data = {
        .raw_type = RAW_TYPE_UINT16,
        .raw_payload.u16 = reg_val,
        .scale = config.scale,
        .offset = 0.0f,
        .sample_timestamp_ms = sample_ts,
        .status = data_status,
        .priority = prio
    };
    strncpy(raw_data.device_id, config.name, sizeof(raw_data.device_id) - 1);
    raw_data.device_id[sizeof(raw_data.device_id) - 1] = '\0';
    strncpy(raw_data.metric_name, config.name, sizeof(raw_data.metric_name) - 1);
    raw_data.metric_name[sizeof(raw_data.metric_name) - 1] = '\0';
    strncpy(raw_data.source, "MODBUS_RS485_1", sizeof(raw_data.source) - 1);
    raw_data.source[sizeof(raw_data.source) - 1] = '\0';
    strncpy(raw_data.unit, "raw", sizeof(raw_data.unit) - 1);
    raw_data.unit[sizeof(raw_data.unit) - 1] = '\0';

    // ==================== 阶段 5: 调用 data_convert 换算工程物理量 ====================
    sensor_val_t conv_val = {0};
    sensor_val_type_t conv_type = VAL_TYPE_FLOAT;
    if (data_status == DATA_STATUS_OK) {
        esp_err_t conv_err = data_convert_raw_to_value(&raw_data, &conv_val, &conv_type);
        if (conv_err == ESP_OK) {
            ESP_LOGI(TAG, "采集成功 -> 设备 [%s] (ID=%u, 寄存器=0x%04X, 原始值=%u, 物理量=%.2f)",
                     config.name, 
                     (unsigned int)config.slave_id, 
                     (unsigned int)config.register_addr, 
                     reg_val,
                     conv_val.f32);
        }
    } else {
        conv_val.f32 = 0.0f;
        conv_type = VAL_TYPE_FLOAT;
    }

    // ==================== 阶段 6: 构造标准设备聚合数据包 device_data_t ====================
    device_data_t dev_data;
    memset(&dev_data, 0, sizeof(device_data_t));
    strncpy(dev_data.device_name, config.name, DATA_DEVICE_NAME_MAX_LEN - 1);
    dev_data.slave_id = config.slave_id;
    dev_data.status = data_status;
    dev_data.update_timestamp_ms = sample_ts;
    dev_data.metric_count = 1;

    strncpy(dev_data.metrics[0].name, config.name, DATA_METRIC_NAME_MAX_LEN - 1);
    dev_data.metrics[0].val_type = conv_type;
    dev_data.metrics[0].value.f32 = conv_val.f32;
    strncpy(dev_data.metrics[0].unit, (strstr(config.name, "humi") != NULL) ? "%" : "℃", DATA_UNIT_MAX_LEN - 1);
    dev_data.metrics[0].status = data_status;
    dev_data.metrics[0].timestamp_ms = sample_ts;

    // ==================== 阶段 7: 投递至 data_manager 数据出口中枢 (0 阻塞) ====================
    esp_err_t ret = data_manager_push(&dev_data);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "data_manager push failed: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief 集中式 Modbus 采集调度器主任务 (modbus_sched_task)
 */
void device_scheduler_task(void *pvParameters)
{
    device_manager_context_t *ctx = device_manager_get_context();
    ESP_LOGI(TAG, "===== Modbus Scheduler 调度器任务 (modbus_sched_task) 已启动 =====");

    while (1) {
        if (!ctx->is_running || ctx->device_count == 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        TickType_t min_wait_ticks = pdMS_TO_TICKS(100);

        for (int i = 0; i < DEVICE_MANAGER_MAX_DEVICES; i++) {
            bool should_poll = false;

            // instance_lock 细粒度保护：检查并更新调度时钟
            xSemaphoreTake(ctx->instance_lock, portMAX_DELAY);
            device_instance_t *inst = &ctx->instances[i];
            if (inst->is_valid && inst->is_enabled) {
                int32_t diff_ticks = (int32_t)(now - (TickType_t)inst->next_poll_tick);
                if (diff_ticks >= 0) {
                    should_poll = true;
                    inst->next_poll_tick = now + pdMS_TO_TICKS(inst->config.period);
                } else {
                    TickType_t remaining = (TickType_t)(-diff_ticks);
                    if (remaining < min_wait_ticks) {
                        min_wait_ticks = remaining;
                    }
                }
            }
            xSemaphoreGive(ctx->instance_lock);

            // 若到期，执行单设备采集 (内部独立获取 bus_lock 与 generation 校验)
            if (should_poll) {
                poll_single_device_by_slot(i);
            }
        }

        // 保护性限幅，确保调度器让出 CPU
        if (min_wait_ticks < pdMS_TO_TICKS(10)) {
            min_wait_ticks = pdMS_TO_TICKS(10);
        } else if (min_wait_ticks > pdMS_TO_TICKS(100)) {
            min_wait_ticks = pdMS_TO_TICKS(100);
        }

        vTaskDelay(min_wait_ticks);
    }
}
