#ifndef __DATA_MANAGER_H__
#define __DATA_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "data_types.h"
#include "data_sink.h"
#include "data_formatter.h"
#include "sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 data_manager 内部队列、LVC 互斥锁与 Sink 框架
 * 
 * @return esp_err_t ESP_OK 表示初始化成功
 */
esp_err_t data_manager_init(void);

/**
 * @brief 启动 data_manager 异步消费分发任务 (data_mgr_task)
 * 
 * @return esp_err_t ESP_OK 表示启动成功
 */
esp_err_t data_manager_start(void);

/**
 * @brief 停止 data_manager 任务
 */
esp_err_t data_manager_stop(void);

/**
 * @brief 向 data_manager 异步推入一帧设备数据包 (供 device_scheduler 非阻塞投递)
 * @note  内部耗时 < 2μs，支持环形丢弃防溢出保护
 * 
 * @param[in] data 待推入的设备数据包指针
 * @return esp_err_t ESP_OK 表示推入成功，ESP_ERR_INVALID_ARG 表示入参为空
 */
esp_err_t data_manager_push(const device_data_t *data);

/**
 * @brief 兼容适配：向 data_manager 推入单测点数据包
 */
esp_err_t data_manager_push_sensor_data(const sensor_data_t *data);

/**
 * @brief 查询指定设备的最新实时数据快照 (Copy-out 隔离设计)
 * 
 * @param[in]  name 目标设备名称
 * @param[out] out  输出设备数据副本
 * @return esp_err_t ESP_OK 表示成功，ESP_ERR_NOT_FOUND 表示设备未在 LVC 中注册
 */
esp_err_t data_manager_get_device_snapshot(
    const char *name,
    device_data_t *out
);

/**
 * @brief 查询网关整机全部设备与测点的全局数字孪生快照 (Copy-out 隔离设计)
 * 
 * @param[out] out 输出网关全局快照副本
 * @return esp_err_t ESP_OK 表示成功
 */
esp_err_t data_manager_get_gateway_snapshot(
    gateway_data_t *out
);

/**
 * @brief 处理设备生命周期事件 (如删除设备时同步清理 LVC 快照)
 * 
 * @param event       生命周期事件类型 (ADD/REMOVE/UPDATE)
 * @param device_name 设备名称
 * @return esp_err_t  ESP_OK 表示处理成功
 */
esp_err_t data_manager_handle_device_event(
    device_event_type_t event,
    const char *device_name
);

/**
 * @brief 从 LVC 实时快照池中即时移除已删除的设备
 * 
 * @param device_name 待移除的设备名称
 * @return esp_err_t  ESP_OK 表示清理成功
 */
esp_err_t data_lvc_remove_device(
    const char *device_name
);

#ifdef __cplusplus
}
#endif

#endif /* __DATA_MANAGER_H__ */
