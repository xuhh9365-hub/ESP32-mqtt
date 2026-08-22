#ifndef __DATA_FORMATTER_H__
#define __DATA_FORMATTER_H__

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "data_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将网关全局快照格式化为标准工业物模型 JSON 字符串 (基于 cJSON 引擎)
 * 
 * @param[in]  data    网关全局快照数据指针
 * @param[out] buffer  目标字符串输出缓冲区
 * @param[in]  len     缓冲区最大容量
 * @return esp_err_t   ESP_OK 序列化成功, ESP_ERR_NO_MEM 缓冲区不足, ESP_ERR_INVALID_ARG 入参非法
 */
esp_err_t data_format_json(
    const gateway_data_t *data,
    char *buffer,
    size_t len
);

#ifdef __cplusplus
}
#endif

#endif /* __DATA_FORMATTER_H__ */
