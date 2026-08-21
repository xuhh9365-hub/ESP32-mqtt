#include "device_internal.h"
#include "modbus.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "DEV_SCHEDULER";

/**
 * @brief 预留的 data_process 处理钩子函数 (v1.0 阶段进行物理量换算并打印)
 */
void data_process_handle_raw_data(const device_config_t *config, uint16_t raw_value)
{
    if (config == NULL) return;

    float physical_value = (float)raw_value * config->scale;
    ESP_LOGI("DATA_PROCESS_STUB", "设备 [%s] 数据处理 -> 物理量: %.2f (原始值: %u, 倍率: %.2f)",
             config->name, physical_value, raw_value, config->scale);

    // 未来此处接入 data_process 模块的全局消息队列投放
}

/**
 * @brief 单个设备执行 Modbus 采集事务
 */
static void poll_single_device(device_instance_t *inst)
{
    uint16_t reg_val = 0;
    
    // 调用统一的 Modbus Master 动态读取接口 (读取 1 个保持寄存器)
    esp_err_t err = modbus_master_read_holding_registers(
        (uint8_t)inst->config.slave_id,
        (uint16_t)inst->config.register_addr,
        1,
        &reg_val
    );
    inst->stats.total_poll_count++;

    if (err == ESP_OK) {
        inst->stats.success_count++;
        inst->stats.consecutive_fail_count = 0;
        inst->status = DEVICE_STATUS_ONLINE;

        ESP_LOGI(TAG, "采集成功 -> 设备 [%s] (ID=%u, 寄存器=0x%04X, 原始值=%u)",
                 inst->config.name, 
                 (unsigned int)inst->config.slave_id, 
                 (unsigned int)inst->config.register_addr, 
                 reg_val);

        // 投递至数据处理钩子
        data_process_handle_raw_data(&inst->config, reg_val);
    } else {
        inst->stats.fail_count++;
        inst->stats.consecutive_fail_count++;

        if (inst->stats.consecutive_fail_count >= DEVICE_CONSECUTIVE_FAIL_LIMIT) {
            inst->status = DEVICE_STATUS_OFFLINE;
            ESP_LOGW(TAG, "⚠️ 设备 [%s] (ID=%u) 连续通信失败 %d 次 (错误: %s)，已判定为 [离线]",
                     inst->config.name, 
                     (unsigned int)inst->config.slave_id, 
                     inst->stats.consecutive_fail_count,
                     esp_err_to_name(err));
        } else {
            ESP_LOGW(TAG, "设备 [%s] (ID=%u) 通信失败 (%d/%d, 错误: %s)",
                     inst->config.name, 
                     (unsigned int)inst->config.slave_id, 
                     inst->stats.consecutive_fail_count, 
                     DEVICE_CONSECUTIVE_FAIL_LIMIT,
                     esp_err_to_name(err));
        }
    }
}

/**
 * @brief 集中式 Modbus 采集调度器主任务 (modbus_scheduler_task)
 */
void device_scheduler_task(void *pvParameters)
{
    device_manager_context_t *ctx = device_manager_get_context();
    ESP_LOGI(TAG, "===== Modbus Scheduler 调度器任务已启动 =====");

    while (1) {
        if (!ctx->is_running || ctx->device_count == 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        TickType_t min_wait_ticks = pdMS_TO_TICKS(100); /* 默认最大空闲延时 100ms */

        for (int i = 0; i < ctx->device_count; i++) {
            device_instance_t *inst = &ctx->instances[i];
            if (!inst->is_valid) continue;

            // 检查系统 Tick 是否已到达计划触发时间 (带溢出安全检查)
            int32_t diff_ticks = (int32_t)(now - (TickType_t)inst->next_poll_tick);

            if (diff_ticks >= 0) {
                // 到期：执行采集
                poll_single_device(inst);

                // 更新下一次触发计划
                inst->next_poll_tick = now + pdMS_TO_TICKS(inst->config.period);
            } else {
                // 尚未到期：计算距离该设备到期的剩余时间
                TickType_t remaining = (TickType_t)(-diff_ticks);
                if (remaining < min_wait_ticks) {
                    min_wait_ticks = remaining;
                }
            }
        }

        // 保护性限幅，确保调度器至少让出 10ms CPU 给低优先级任务 (如 MQTT/WiFi)
        if (min_wait_ticks < pdMS_TO_TICKS(10)) {
            min_wait_ticks = pdMS_TO_TICKS(10);
        } else if (min_wait_ticks > pdMS_TO_TICKS(100)) {
            min_wait_ticks = pdMS_TO_TICKS(100);
        }

        vTaskDelay(min_wait_ticks);
    }
}
