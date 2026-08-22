#ifndef __CONFIG_VALIDATOR_H__
#define __CONFIG_VALIDATOR_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 配置校验错误原因枚举
 */
typedef enum {
    CONFIG_VAL_OK = 0,
    CONFIG_VAL_ERR_DEVICE_COUNT,        /* 设备总数超出限制 (0 或 > CONFIG_MAX_DEVICES) */
    CONFIG_VAL_ERR_DEVICE_NAME_EMPTY,   /* 设备名称为空 */
    CONFIG_VAL_ERR_DEVICE_NAME_INVALID, /* 设备名称包含非法字符 */
    CONFIG_VAL_ERR_DEVICE_NAME_DUP,     /* 设备名称重复定义 */
    CONFIG_VAL_ERR_SLAVE_ID_INVALID,    /* Modbus 从站 ID 非法 (不在 1~247 范围内) */
    CONFIG_VAL_ERR_PERIOD_INVALID,      /* 采样周期超出允许范围 (50~60000ms) */
    CONFIG_VAL_ERR_SCALE_ZERO,          /* 换算倍率不能为 0 */
    CONFIG_VAL_ERR_METRIC_COUNT,        /* 测点数超出单个设备容量 ( > CONFIG_MAX_METRICS_PER_DEVICE) */
    CONFIG_VAL_ERR_METRIC_NAME_EMPTY,   /* 测点名称为空 */
    CONFIG_VAL_ERR_METRIC_NAME_INVALID, /* 测点名称包含非法字符 */
    CONFIG_VAL_ERR_METRIC_NAME_DUP,     /* 同一设备内测点名重复 */
    CONFIG_VAL_ERR_METRIC_RANGE_INVALID /* 测点量程非法 (min_value >= max_value) */
} config_val_err_t;

/**
 * @brief 配置校验诊断结果模型
 */
typedef struct {
    config_val_err_t err_code;                              /* 错误枚举代码 */
    char             err_target[CONFIG_DEVICE_NAME_MAX_LEN];/* 出错的目标设备或测点名称 */
    char             err_detail[128];                       /* 错误详细原因描述 */
} config_validation_result_t;

/**
 * @brief 深度校验一组设备配置的完整性与合法性
 * 
 * @param[in]  devices      待校验设备配置数组
 * @param[in]  count        设备配置数量
 * @param[out] out_result   若校验失败，输出详细诊断信息 (可传 NULL)
 * @return esp_err_t        ESP_OK 表示校验完全通过，ESP_ERR_INVALID_ARG 表示存在非法配置
 */
esp_err_t config_validator_check(
    const device_config_t *devices,
    uint8_t count,
    config_validation_result_t *out_result
);

/**
 * @brief 将配置校验错误码转换为易读的可视化字符串
 */
const char *config_val_err_to_str(config_val_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* __CONFIG_VALIDATOR_H__ */
