#ifndef __CONFIG_PARSER_H__
#define __CONFIG_PARSER_H__

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 解析 JSON 字符串并填充 device_config_t 数组
 * 
 * @param json_str 输入 JSON 字符串
 * @param[out] out_devices 输出配置数组缓冲区
 * @param max_devices 缓冲区最大设备容量
 * @param[out] out_count 成功解析的设备数量
 * @return esp_err_t ESP_OK 表示解析和校验全部通过，ESP_ERR_INVALID_ARG 表示格式或数据错误
 */
esp_err_t config_parser_parse_json(const char *json_str, 
                                  device_config_t *out_devices, 
                                  uint8_t max_devices, 
                                  uint8_t *out_count);

/**
 * @brief 将 device_config_t 数组序列化为 JSON 字符串
 * 
 * @param devices 输入设备配置数组
 * @param count 设备数量
 * @param[out] out_buf 输出字符串缓冲区
 * @param max_len 缓冲区最大长度
 * @return esp_err_t ESP_OK 表示成功，ESP_ERR_NO_MEM 表示空间不足
 */
esp_err_t config_parser_serialize_json(const device_config_t *devices, 
                                      uint8_t count, 
                                      char *out_buf, 
                                      size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* __CONFIG_PARSER_H__ */
