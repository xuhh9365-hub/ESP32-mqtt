#ifndef __CONFIG_MANAGER_H__
#define __CONFIG_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_MAX_DEVICES              (16)    /* 网关支持的最大设备配置数量 */
#define CONFIG_DEVICE_NAME_MAX_LEN      (32)    /* 设备名称最大字符长度 */
#define CONFIG_MAX_METRICS_PER_DEVICE   (4)     /* 每个设备支持的最大控制测点数量 */
#define CONFIG_METRIC_NAME_MAX_LEN      (32)    /* 测点名称最大字符长度 */

/**
 * @brief 设备控制测点配置结构体
 */
typedef struct {
    char        metric_name[CONFIG_METRIC_NAME_MAX_LEN];    /* 测点/参数名称，如 "temp_limit" */
    uint16_t    write_register;                             /* 目标写入寄存器地址 (如 19 / 0x0013) */
    float       scale;                                      /* 换算倍率 (写入原始值 = 物理值 / scale) */
    float       min_value;                                  /* 允许设定的物理量下限 */
    float       max_value;                                  /* 允许设定的物理量上限 */
} device_metric_config_t;

/**
 * @brief 设备配置标准结构体
 */
typedef struct {
    char                    name[CONFIG_DEVICE_NAME_MAX_LEN];   /* 设备名称，如 "temperature_sensor" */
    uint8_t                 slave_id;                           /* Modbus 从站地址 (1 ~ 247) */
    uint16_t                register_addr;                      /* 采集寄存器起始地址 (0x0000 ~ 0xFFFF) */
    char                    reg_type[16];                       /* 寄存器类型: "holding", "input" 等 */
    char                    data_type[16];                      /* 数据类型: "uint16", "int16", "float" 等 */
    float                   scale;                              /* 缩放倍率 (物理值 = 原始值 * scale) */
    uint32_t                period;                             /* 采集周期 (毫秒) */
    device_metric_config_t  metrics[CONFIG_MAX_METRICS_PER_DEVICE]; /* 支持的控制测点列表 */
    uint8_t                 metric_count;                       /* 实际配置的测点数量 */
} device_config_t;

/**
 * @brief 初始化配置管理器 (上电自动从 NVS 恢复或回退加载出厂默认配置)
 * 
 * @return esp_err_t ESP_OK 表示初始化并加载成功
 */
esp_err_t config_manager_init(void);

/**
 * @brief 获取当前已加载的设备配置总数
 */
int config_get_device_num(void);

/**
 * @brief 获取当前运行的配置版本号 (从 1 开始单调自增)
 */
uint32_t config_manager_get_version(void);

/**
 * @brief 根据索引获取指定设备配置 (复制到输出参数)
 */
esp_err_t config_get_device(int index, device_config_t *device);

/**
 * @brief 根据设备名称查找并获取设备配置
 */
esp_err_t config_get_device_by_name(const char *name, device_config_t *device);

/**
 * @brief 从 JSON 字符串动态加载设备配置 (内存更新)
 */
esp_err_t config_load_from_json(const char *json_str);

/**
 * @brief 将当前内存中的配置导出为 JSON 字符串
 */
esp_err_t config_export_to_json(char *json_buf, size_t buf_len);

/**
 * @brief 热更新应用并持久化新配置 (基础流程: 解析 -> 校验 -> 版本检查 -> 更新RAM -> 保存NVS)
 * 
 * @param[in]  json_str     新配置 JSON 字符串
 * @param[out] out_version  若成功，输出更新后的新版本号 (可传 NULL)
 * @return esp_err_t        ESP_OK 表示更新成功，其它为错误码
 */
esp_err_t config_manager_apply_update(
    const char *json_str,
    uint32_t *out_version
);

typedef esp_err_t (*config_reload_hook_t)(const device_config_t *new_cfg, uint8_t new_count);

/**
 * @brief 注册运行时配置重载通知回调 (由 device_manager 注册)
 */
esp_err_t config_manager_register_reload_hook(config_reload_hook_t hook);

/**
 * @brief 热更新应用并持久化新配置 (全链路闭环: JSON解析 -> 深度校验 -> 更新RAM -> device_manager热重载 -> NVS持久化)
 * 
 * @param[in]  json         新配置 JSON 字符串
 * @param[out] new_version  若成功，输出更新后的新版本号 (可传 NULL)
 * @return esp_err_t        ESP_OK 表示成功，其它为对应错误码
 */
esp_err_t config_manager_apply_json(
    const char *json,
    uint32_t *new_version
);

/**
 * @brief 恢复出厂默认配置 (清除 NVS 并重新写入出厂默认配置)
 */
esp_err_t config_manager_restore_factory(void);

/**
 * @brief 仅对输入配置 JSON 进行 Dry-run 预校验 (不保存 NVS，不修改运行态设备)
 * 
 * @param[in]  json_str    待校验的配置 JSON 字符串
 * @param[out] out_result  输出详细诊断信息 (可传 NULL)
 * @return esp_err_t       ESP_OK 表示校验通过，ESP_ERR_INVALID_ARG 表示校验失败
 */
esp_err_t config_manager_validate_json(
    const char *json_str,
    void *out_result
);

/**
 * @brief 保存当前内存配置至 NVS 持久化存储
 */
int config_save(void);

#ifdef __cplusplus
}
#endif

#endif /* __CONFIG_MANAGER_H__ */
