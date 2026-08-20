#ifndef __CONFIG_MANAGER_H__
#define __CONFIG_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_MAX_DEVICES          (16)    /* 网关支持的最大设备配置数量 */
#define CONFIG_DEVICE_NAME_MAX_LEN  (32)    /* 设备名称最大字符长度 */

/**
 * @brief 设备配置标准结构体
 */
typedef struct {
    char        name[CONFIG_DEVICE_NAME_MAX_LEN];   /* 设备名称，如 "temperature_sensor" */
    uint8_t     slave_id;                           /* Modbus 从站地址 (1 ~ 247) */
    uint16_t    register_addr;                      /* 寄存器起始地址 (0x0000 ~ 0xFFFF) */
    char        reg_type[16];                       /* 寄存器类型: "holding", "input" 等 */
    char        data_type[16];                      /* 数据类型: "uint16", "int16", "float" 等 */
    float       scale;                              /* 缩放倍率 (物理值 = 原始值 * scale) */
    uint32_t    period;                             /* 采集周期 (毫秒) */
} device_config_t;

/**
 * @brief 初始化配置管理器
 *        在 Phase 1 阶段，该函数将加载内置默认 JSON 配置并解析至内存缓存中
 * 
 * @return esp_err_t ESP_OK 表示成功，其他表示失败
 */
esp_err_t config_manager_init(void);

/**
 * @brief 获取当前已加载的设备配置总数
 * 
 * @return int 设备数量 (>= 0)，若未初始化返回 -1
 */
int config_get_device_num(void);

/**
 * @brief 根据索引获取指定设备配置 (复制到输出参数)
 * 
 * @param index 设备索引 (0 <= index < device_num)
 * @param[out] device 输出目标结构体指针
 * @return esp_err_t ESP_OK 表示获取成功，ESP_ERR_INVALID_ARG 表示索引越界或入参为空
 */
esp_err_t config_get_device(int index, device_config_t *device);

/**
 * @brief 根据设备名称查找并获取设备配置
 * 
 * @param name 设备名称
 * @param[out] device 输出目标结构体指针
 * @return esp_err_t ESP_OK 表示找到，ESP_ERR_NOT_FOUND 表示未找到
 */
esp_err_t config_get_device_by_name(const char *name, device_config_t *device);

/**
 * @brief 从 JSON 字符串动态加载设备配置
 * 
 * @param json_str 符合规范的 JSON 格式字符串
 * @return esp_err_t ESP_OK 表示解析并加载成功，ESP_ERR_INVALID_ARG 表示解析或校验失败
 */
esp_err_t config_load_from_json(const char *json_str);

/**
 * @brief 将当前内存中的配置导出为 JSON 字符串
 * 
 * @param[out] json_buf 输出缓冲区
 * @param buf_len 缓冲区最大长度
 * @return esp_err_t ESP_OK 表示成功，ESP_ERR_NO_MEM 表示缓冲区空间不足
 */
esp_err_t config_export_to_json(char *json_buf, size_t buf_len);

/**
 * @brief 预留的保存配置接口 (Phase 2 NVS 实现)
 * 
 * @return int 0 表示成功，非 0 表示失败
 */
int config_save(void);

#ifdef __cplusplus
}
#endif

#endif /* __CONFIG_MANAGER_H__ */
