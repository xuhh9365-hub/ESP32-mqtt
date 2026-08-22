#ifndef __DATA_CONVERT_H__
#define __DATA_CONVERT_H__

#include "sensor_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将原始采集数据包根据 Scale 和 Offset 换算为标准工程物理量
 * 
 * @param[in]  raw   原始采集输入包 (包含 raw_type, raw_payload, scale, offset)
 * @param[out] value 换算后的物理量联合体输出
 * @param[out] type  物理量类型标签输出 (如 VAL_TYPE_FLOAT)
 * 
 * @return 
 *      - ESP_OK: 换算成功
 *      - ESP_ERR_INVALID_ARG: 入参指针为空或 raw_type 类型不支持
 */
esp_err_t data_convert_raw_to_value(
    const sensor_raw_data_t *raw,
    sensor_val_t *value,
    sensor_val_type_t *type
);

#ifdef __cplusplus
}
#endif

#endif /* __DATA_CONVERT_H__ */
