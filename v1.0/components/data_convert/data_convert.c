#include "data_convert.h"
#include <stddef.h>

/**
 * @brief 将原始采集数据包根据 Scale 和 Offset 换算为标准工程物理量
 */
esp_err_t data_convert_raw_to_value(
    const sensor_raw_data_t *raw,
    sensor_val_t *value,
    sensor_val_type_t *type)
{
    // 1. 参数有效性检查
    if (raw == NULL || value == NULL || type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float physical_value = 0.0f;

    // 2. 根据原始数据物理类型进行换算
    switch (raw->raw_type) {
    case RAW_TYPE_UINT16:
        physical_value = ((float)raw->raw_payload.u16) * raw->scale + raw->offset;
        value->f32 = physical_value;
        *type = VAL_TYPE_FLOAT;
        break;

    case RAW_TYPE_INT16:
        physical_value = ((float)raw->raw_payload.i16) * raw->scale + raw->offset;
        value->f32 = physical_value;
        *type = VAL_TYPE_FLOAT;
        break;

    case RAW_TYPE_UINT32:
        physical_value = ((float)raw->raw_payload.u32) * raw->scale + raw->offset;
        value->f32 = physical_value;
        *type = VAL_TYPE_FLOAT;
        break;

    case RAW_TYPE_INT32:
        physical_value = ((float)raw->raw_payload.i32) * raw->scale + raw->offset;
        value->f32 = physical_value;
        *type = VAL_TYPE_FLOAT;
        break;

    case RAW_TYPE_FLOAT:
        physical_value = (raw->raw_payload.f32) * raw->scale + raw->offset;
        value->f32 = physical_value;
        *type = VAL_TYPE_FLOAT;
        break;

    // 暂未实现的原始数据类型
    case RAW_TYPE_DOUBLE:
    case RAW_TYPE_BOOL:
    case RAW_TYPE_BYTES:
    default:
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}
