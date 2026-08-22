#ifndef __SENSOR_TYPES_H__
#define __SENSOR_TYPES_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_DEVICE_ID_LEN      32
#define DATA_METRIC_NAME_LEN    24
#define DATA_SOURCE_LEN         20
#define DATA_UNIT_LEN           16

/**
 * @brief 工业通用数据质量戳与通信状态枚举
 */
typedef enum {
    DATA_STATUS_OK = 0,         /* 数据正常有效 (Good Quality) */
    DATA_STATUS_OFFLINE,        /* 物理设备已判定离线 */
    DATA_STATUS_TIMEOUT,        /* 单次采集响应超时 */
    DATA_STATUS_COMM_ERROR,     /* 校验或通信物理错误 (CRC/Parity/Frame) */
    DATA_STATUS_OUT_OF_RANGE,   /* 测量值超出工程量程 */
    DATA_STATUS_INVALID,        /* 数据无效/未初始化 */
} data_status_t;

/**
 * @brief 数据优先级等级
 */
typedef enum {
    DATA_PRIO_NORMAL = 0,       /* 常规周期遥测数据 */
    DATA_PRIO_HIGH,             /* 高优先级状态变位数据 */
    DATA_PRIO_ALARM,            /* 严重越限告警数据 */
} sensor_data_priority_t;

/**
 * @brief 原始采集数据物理类型枚举
 */
typedef enum {
    RAW_TYPE_UINT16 = 0,        /* Modbus 标准 16 位无符号寄存器 */
    RAW_TYPE_INT16,             /* 16 位有符号整型 */
    RAW_TYPE_UINT32,            /* 32 位无符号整型 */
    RAW_TYPE_INT32,             /* 32 位有符号整型 */
    RAW_TYPE_FLOAT,             /* 32 位单精度浮点 */
    RAW_TYPE_DOUBLE,            /* 64 位双精度浮点 */
    RAW_TYPE_BOOL,              /* 单点开关量/线圈状态 */
    RAW_TYPE_BYTES,             /* 原始字节块 (CAN 扩展帧/自定义透传) */
} raw_data_type_t;

/**
 * @brief 原始物理载荷通用联合体
 */
typedef union {
    uint16_t    u16;
    int16_t     i16;
    uint32_t    u32;
    int32_t     i32;
    float       f32;
    double      f64;
    bool        b_val;
    uint8_t     bytes[8];       /* 最大支持 8 字节原始物理帧 (如 CAN 报文 Payload) */
} raw_payload_t;

/**
 * @brief 标准物理量类型标签
 */
typedef enum {
    VAL_TYPE_FLOAT = 0,         /* 32 位单精度浮点 */
    VAL_TYPE_DOUBLE,            /* 64 位双精度浮点 */
    VAL_TYPE_INT32,             /* 32 位有符号整型 */
    VAL_TYPE_UINT32,            /* 32 位无符号整型 */
    VAL_TYPE_BOOL,              /* 布尔开关量 */
} sensor_val_type_t;

/**
 * @brief 标准物理量载荷联合体
 */
typedef union {
    float       f32;
    double      f64;
    int32_t     i32;
    uint32_t    u32;
    bool        b_val;
} sensor_val_t;

/**
 * @brief 原始采集输入包 (由采集调度器构造，输入给转换引擎)
 */
typedef struct {
    char                    device_id[DATA_DEVICE_ID_LEN];      /* 设备标识, 如 "temperature_sensor" */
    char                    metric_name[DATA_METRIC_NAME_LEN];  /* 测点名称, 如 "temperature" */
    char                    source[DATA_SOURCE_LEN];            /* 数据物理源: "MODBUS_RS485_1", "CAN_BUS_0", "ADC_CH3" */
    raw_data_type_t         raw_type;                           /* 原始数据类型 */
    raw_payload_t           raw_payload;                        /* 原始数据载荷 */
    float                   scale;                              /* 换算倍率 */
    float                   offset;                             /* 零点偏移 */
    char                    unit[DATA_UNIT_LEN];                /* 工程单位, 如 "℃", "%", "V" */
    int64_t                 sample_timestamp_ms;                /* 【采样物理时戳】: 硬件采集到的物理时刻 */
    data_status_t           status;                             /* 通信与质量状态 */
    sensor_data_priority_t  priority;                           /* 优先级 */
} sensor_raw_data_t;

/**
 * @brief 标准工业遥测数据包 (在 data_manager 队列与 Sinks 中流转)
 */
typedef struct {
    /* 1. 标识与数据来源 (支持追溯) */
    char                    device_id[DATA_DEVICE_ID_LEN];      /* 设备标识 */
    char                    metric_name[DATA_METRIC_NAME_LEN];  /* 测点名称 */
    char                    source[DATA_SOURCE_LEN];            /* 数据来源 (如 "MODBUS_RS485_1") */

    /* 2. 测量物理量与原始数据 (工业级双向溯源) */
    sensor_val_type_t       val_type;                           /* 物理量类型 */
    sensor_val_t            value;                              /* 已换算的物理工程量 */
    raw_payload_t           raw_value;                          /* 原始数据载荷保留 (用于溯源审计) */
    char                    unit[DATA_UNIT_LEN];                /* 工程单位 */

    /* 3. 双时间戳机制 (时延与抖动可观测性) */
    int64_t                 sample_timestamp_ms;                /* 【采样物理时戳】: 硬件采集到的物理时刻 */
    int64_t                 process_timestamp_ms;               /* 【处理调度时戳】: data_manager 入队/分发时刻 */

    /* 4. 质量与优先级 */
    data_status_t           status;                             /* 数据质量状态 */
    sensor_data_priority_t  priority;                           /* 优先级 */
} sensor_data_t;

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_TYPES_H__ */
