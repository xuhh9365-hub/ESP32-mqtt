#ifndef __DEVICE_MANAGER_H__
#define __DEVICE_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sensor_types.h"
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_MANAGER_MAX_DEVICES      16      /* 最大支持设备实例数 */
#define BUS_LOCK_DEFAULT_TIMEOUT_MS     300     /* 默认总线排队等待超时时间 */

/**
 * @brief 设备运行态通信与控制统计结构体
 */
typedef struct {
    uint32_t            total_poll_count;           /* 总采样轮询次数 */
    uint32_t            success_count;              /* 采集成功次数 */
    uint32_t            fail_count;                 /* 采集失败次数 */
    uint16_t            consecutive_fail_count;     /* 当前连续失败计数 */
    uint32_t            write_total_count;          /* 总写操作次数 */
    uint32_t            write_success_count;        /* 写操作成功次数 */
    uint32_t            write_fail_count;           /* 写操作失败次数 */
    int64_t             last_write_timestamp_ms;    /* 最近一次写操作时间戳 */
} device_stats_t;

/**
 * @brief 单个设备运行态实例结构体 (含生命周期 generation 保护)
 */
typedef struct {
    bool                is_valid;               /* 实例槽位是否有效 */
    bool                is_enabled;             /* 运行时采集使能开关 */
    uint32_t            generation;             /* 实例代数计数器 (生命周期防竞争保护) */
    device_config_t     config;                 /* 静态配置副本 */
    data_status_t       status;                 /* 当前质量与在线状态 */
    uint32_t            next_poll_tick;         /* 下一次轮询的 FreeRTOS Tick 时刻 */
    device_stats_t      stats;                  /* 通信与控制健康度统计 */
} device_instance_t;

/**
 * @brief 设备外部信息查询结构体 (Copy-out 隔离设计)
 */
typedef struct {
    char                name[CONFIG_DEVICE_NAME_MAX_LEN];
    uint8_t             slave_id;
    uint16_t            register_addr;
    float               scale;
    uint32_t            period;
    data_status_t       status;
    bool                enabled;
    uint8_t             metric_count;
} device_info_t;

/**
 * @brief RS485 物理总线健康与负载状态
 */
typedef struct {
    bool                busy;                   /* 当前总线是否正处于物理收发中 */
    uint32_t            wait_count;             /* 累计总线请求排队次数 */
    uint32_t            timeout_count;          /* 累计总线竞争超时次数 */
} device_bus_status_t;

/**
 * @brief 从 config_manager 装载静态配置并初始化设备管理器实例池与互斥锁
 */
esp_err_t device_manager_init(void);

/**
 * @brief 启动 Modbus Scheduler 集中调度器任务 (modbus_sched_task)
 */
esp_err_t device_manager_start(void);

/**
 * @brief 停止 Modbus 调度器任务
 */
esp_err_t device_manager_stop(void);

/**
 * @brief 动态添加一个新设备实例 (线程安全，自动递增代数 generation)
 */
esp_err_t device_manager_add_device(const device_config_t *config);

/**
 * @brief 动态删除指定设备 (线程安全，销毁槽位并递增代数 generation)
 */
esp_err_t device_manager_remove_device(const char *device_name);

/**
 * @brief 动态更新已有设备配置 (原子重置配置并递增 generation)
 */
esp_err_t device_manager_update_device(const device_config_t *config);

/**
 * @brief 获取当前正在运行的有效设备数量
 */
uint8_t device_manager_get_device_count(void);

/**
 * @brief 应用配置差异 (Diff)，智能执行增删改调度
 * 
 * @param[in] old_cfg   旧设备配置表
 * @param[in] old_count 旧设备数量
 * @param[in] new_cfg   新设备配置表
 * @param[in] new_count 新设备数量
 * @return esp_err_t    ESP_OK 表示差异热重载全部成功
 */
esp_err_t device_manager_apply_config_diff(
    const device_config_t *old_cfg,
    uint8_t old_count,
    const device_config_t *new_cfg,
    uint8_t new_count
);

/**
 * @brief 将新配置列表应用至运行态实例池 (内部自动提取旧运行态实例并执行 Diff 增删改热重载)
 * 
 * @param[in] devices 新配置设备数组
 * @param[in] count   新配置设备数量
 * @return esp_err_t  ESP_OK 表示热重载成功
 */
esp_err_t device_manager_apply_config(
    const device_config_t *devices,
    uint8_t count
);

/**
 * @brief 动态使能或暂停指定设备的采集
 */
esp_err_t device_manager_set_device_enabled(const char *device_name, bool enable);

/**
 * @brief 查询指定设备的运行态统计与在线状态
 */
esp_err_t device_manager_get_device_status(
    const char *device_name,
    device_stats_t *stats,
    data_status_t *status
);

/**
 * @brief 获取设备运行静态与动态信息 (Copy-out 隔离，避免指针泄漏)
 */
esp_err_t device_manager_get_device_info(
    const char *device_name,
    device_info_t *info
);

/**
 * @brief 获取当前 RS485 物理总线繁忙度与统计
 */
esp_err_t device_manager_get_bus_status(
    device_bus_status_t *status
);

/**
 * @brief 向指定设备安全写入单个保持寄存器 (底层寄存器操作)
 */
esp_err_t device_manager_write_holding_register(
    const char *device_name,
    uint16_t reg_addr,
    uint16_t value,
    uint32_t timeout_ms
);

/**
 * @brief 向指定设备安全写入单个保持寄存器 (支持紧急控制高优先级超时策略)
 */
esp_err_t device_manager_write_holding_register_ex(
    const char *device_name,
    uint16_t reg_addr,
    uint16_t value,
    uint32_t timeout_ms,
    bool high_priority
);

/**
 * @brief 根据设备 metric 名称写入物理工程量 (工业语义化控制接口)
 */
esp_err_t device_manager_write_metric(
    const char *device_name,
    const char *metric_name,
    sensor_val_t value,
    sensor_val_type_t type,
    uint32_t timeout_ms
);

#ifdef __cplusplus
}
#endif

#endif /* __DEVICE_MANAGER_H__ */
