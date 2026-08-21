#ifndef __DEVICE_MANAGER_H__
#define __DEVICE_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_MANAGER_MAX_DEVICES      (16)  /* 最大支持的运行态设备数 */
#define DEVICE_CONSECUTIVE_FAIL_LIMIT   (3)   /* 判定离线的连续通信失败次数上限 */

/**
 * @brief 设备在线通信状态枚举
 */
typedef enum {
    DEVICE_STATUS_UNKNOWN = 0,  /* 初始未知状态 */
    DEVICE_STATUS_ONLINE,       /* 通信正常在线 */
    DEVICE_STATUS_OFFLINE       /* 通信连续超时离线 */
} device_status_t;

/**
 * @brief 单个设备的运行统计数据
 */
typedef struct {
    uint32_t total_poll_count;        /* 总采集请求轮次 */
    uint32_t success_count;           /* 采集成功次数 */
    uint32_t fail_count;              /* 采集失败次数 */
    uint8_t  consecutive_fail_count;  /* 当前连续通信失败计数 */
} device_statistics_t;

/**
 * @brief 运行态设备实例结构体
 */
typedef struct {
    device_config_t     config;          /* 静态配置参数 (来自 config_manager) */
    device_status_t     status;          /* 当前在线/离线状态 */
    device_statistics_t stats;           /* 通信统计数据 */
    uint32_t            next_poll_tick;  /* 下一次计划触发采集的系统 Tick */
    bool                is_valid;        /* 实例有效标志 */
} device_instance_t;

/**
 * @brief 初始化设备管理器
 *        - 从 config_manager 读取设备静态配置
 *        - 在本地内存构建 device_instance_t 运行态列表
 * 
 * @return esp_err_t ESP_OK 表示初始化成功，其他表示失败
 */
esp_err_t device_manager_init(void);

/**
 * @brief 启动集中式 Modbus 采集调度器任务 (modbus_scheduler_task)
 * 
 * @return esp_err_t ESP_OK 表示启动成功，其他表示失败
 */
esp_err_t device_manager_start(void);

/**
 * @brief 获取当前设备管理器中管理的设备实例总数
 * 
 * @return int 设备实例数量 (>= 0)，未初始化返回 -1
 */
int device_manager_get_device_count(void);

/**
 * @brief 根据索引获取设备运行态实例
 * 
 * @param index 实例索引号 (0 <= index < device_count)
 * @param[out] out_inst 输出目标结构体指针
 * @return esp_err_t ESP_OK 表示获取成功
 */
esp_err_t device_manager_get_device_instance(int index, device_instance_t *out_inst);

/**
 * @brief 根据设备名称查找并获取设备实例
 * 
 * @param name 设备名称
 * @param[out] out_inst 输出目标结构体指针
 * @return esp_err_t ESP_OK 表示找到，ESP_ERR_NOT_FOUND 表示未找到
 */
esp_err_t device_manager_get_instance_by_name(const char *name, device_instance_t *out_inst);

#ifdef __cplusplus
}
#endif

#endif /* __DEVICE_MANAGER_H__ */
