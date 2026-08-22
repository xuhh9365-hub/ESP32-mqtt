#ifndef __CONFIG_STORAGE_H__
#define __CONFIG_STORAGE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_STORAGE_MAGIC        (0x47574346)    /* 幻数 "GWCF" (Gateway Config) */
#define CONFIG_NVS_NAMESPACE        "gw_cfg"        /* NVS 专用命名空间 */
#define CONFIG_NVS_KEY_META         "meta"          /* 元数据键名 */
#define CONFIG_NVS_KEY_DATA         "data"          /* 配置数据载荷键名 */

/**
 * @brief NVS 存储头部元数据结构体 (掉电校验与版本控制)
 */
typedef struct {
    uint32_t magic;             /* 幻数 0x47574346 */
    uint32_t version;           /* 单调递增版本号 (从 1 开始) */
    uint32_t data_len;          /* 配置有效数据字节长度 */
    uint32_t crc32;             /* 数据载荷的 CRC32 校验码 */
    int64_t  update_timestamp;  /* 写入持久化时的 Unix 时戳 (秒) */
} __attribute__((packed)) config_storage_meta_t;

/**
 * @brief 初始化 NVS 配置存储层
 * 
 * @return esp_err_t ESP_OK 表示初始化成功
 */
esp_err_t config_storage_init(void);

/**
 * @brief 将配置数据原子持久化至 NVS
 * @note  采用掉电安全顺序: 写入 data -> 写入 meta -> commit
 * 
 * @param[in]  data         待保存的 JSON 配置字符串
 * @param[in]  len          数据字节长度
 * @param[out] out_version  若成功，输出递增后的新版本号 (可传 NULL)
 * @return esp_err_t        ESP_OK 表示保存并提交成功
 */
esp_err_t config_storage_save(
    const char *data,
    size_t len,
    uint32_t *out_version
);

/**
 * @brief 从 NVS 加载持久化的配置数据并进行 CRC32 完整性校验
 * 
 * @param[out] buffer       数据接收缓冲区
 * @param[in]  buffer_size  缓冲区最大容量
 * @param[out] out_version  若成功，输出当前配置版本号 (可传 NULL)
 * @return esp_err_t        ESP_OK 表示加载并校验成功
 *                          ESP_ERR_NOT_FOUND 表示 NVS 中无持久化配置
 *                          ESP_ERR_INVALID_CRC 表示数据损坏校验失败
 */
esp_err_t config_storage_load(
    char *buffer,
    size_t buffer_size,
    uint32_t *out_version
);

/**
 * @brief 清除 NVS 中保存的配置数据与元数据 (恢复出厂状态)
 * 
 * @return esp_err_t ESP_OK 表示清除成功
 */
esp_err_t config_storage_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __CONFIG_STORAGE_H__ */
