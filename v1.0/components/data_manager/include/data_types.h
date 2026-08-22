#ifndef __DATA_TYPES_H__
#define __DATA_TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_DEVICE_NAME_MAX_LEN        32
#define DATA_METRIC_NAME_MAX_LEN        24
#define DATA_UNIT_MAX_LEN               16
#define DATA_MAX_METRICS_PER_DEVICE     8
#define DATA_MAX_DEVICES                16
#define DATA_GATEWAY_ID_MAX_LEN         32

/**
 * @brief 设备生命周期事件类型
 */
typedef enum {
    DEVICE_EVENT_ADD = 0,
    DEVICE_EVENT_REMOVE,
    DEVICE_EVENT_UPDATE
} device_event_type_t;

/**
 * @brief 物理量数值联合体 (静态内存表示)
 */
typedef union {
    bool        b_val;
    int16_t     i16;
    uint16_t    u16;
    int32_t     i32;
    uint32_t    u32;
    float       f32;
    double      f64;
} sensor_value_t;

/**
 * @brief 单测点数据模型 (metric_data_t)
 */
typedef struct {
    char                name[DATA_METRIC_NAME_MAX_LEN];     /* 测点名称，如 "temperature" */
    sensor_val_type_t   val_type;                           /* 数据类型: float/int/bool等 */
    sensor_value_t      value;                              /* 物理量数值 */
    char                unit[DATA_UNIT_MAX_LEN];            /* 工程单位，如 "℃", "%" */
    data_status_t       status;                             /* 数据质量状态 (OK/TIMEOUT/OFFLINE) */
    int64_t             timestamp_ms;                       /* 采样时间戳 (毫秒) */
} metric_data_t;

/**
 * @brief 单设备聚合数据模型 (device_data_t)
 */
typedef struct {
    char                device_name[DATA_DEVICE_NAME_MAX_LEN];  /* 设备标识名称 */
    uint8_t             slave_id;                               /* 从站地址 */
    data_status_t       status;                                 /* 设备整体健康状态 */
    uint8_t             metric_count;                           /* 有效测点数 (0 ~ 8) */
    metric_data_t       metrics[DATA_MAX_METRICS_PER_DEVICE];   /* 测点数组 */
    int64_t             update_timestamp_ms;                    /* 最近更新时间戳 (毫秒) */
} device_data_t;

/**
 * @brief 网关整机全局数据快照 (gateway_data_t - LVC 快照模型)
 */
typedef struct {
    char                gateway_id[DATA_GATEWAY_ID_MAX_LEN];    /* 网关唯一标识符 */
    uint32_t            config_version;                         /* 运行配置版本号 */
    uint8_t             device_count;                           /* 活跃运行设备总数 */
    device_data_t       devices[DATA_MAX_DEVICES];              /* 设备数据集快照 */
    int64_t             snapshot_timestamp_ms;                  /* 快照生成时间戳 (毫秒) */
} gateway_data_t;

#ifdef __cplusplus
}
#endif

#endif /* __DATA_TYPES_H__ */
