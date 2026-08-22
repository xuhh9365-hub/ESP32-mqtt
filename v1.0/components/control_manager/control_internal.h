#ifndef __CONTROL_INTERNAL_H__
#define __CONTROL_INTERNAL_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "sensor_types.h"
#include "control_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* ========================================================================= */
/*                              宏定义与系统常量                             */
/* ========================================================================= */

#define CONTROL_QUEUE_SIZE              (8)     /* 控制报文缓冲队列深度 */
#define CONTROL_TASK_PRIORITY           (5)     /* 任务优先级 (高于普通遥测任务) */
#define CONTROL_TASK_STACK_SIZE         (4096)  /* 任务栈深度 (字节) */
#define CONTROL_RAW_BUF_SIZE            (256)   /* 原始控制 JSON 报文最大缓冲长度 */
#define CONTROL_MSG_ID_MAX_LEN          (32)    /* msg_id 字符串最大长度 */
#define CONTROL_DEVICE_NAME_MAX_LEN     (32)    /* 设备名称最大长度 */
#define CONTROL_COMMAND_MAX_LEN         (16)    /* 命令动作名称最大长度 */
#define CONTROL_METRIC_NAME_MAX_LEN     (32)    /* 测点名称最大长度 */
#define CONTROL_REPLY_MAX_LEN           (256)   /* ACK/NACK JSON 回执最大缓冲长度 */
#define CONTROL_TXN_CACHE_SIZE          (16)    /* 幂等事务历史缓存容量 (16条) */

#define MQTT_CONTROL_RESPONSE_TOPIC     "gateway/control/response"

/* ========================================================================= */
/*                              核心数据结构定义                             */
/* ========================================================================= */

/**
 * @brief 命令动作类型枚举
 */
typedef enum {
    CMD_TYPE_WRITE = 0,     /* 写入测点物理量 (当前支持) */
    CMD_TYPE_READ,          /* 读取设备信息 (预留) */
    CMD_TYPE_REBOOT,        /* 设备/网关重启 (预留) */
    CMD_TYPE_UPGRADE,       /* OTA 固件升级 (预留) */
    CMD_TYPE_UNKNOWN
} control_command_type_t;

/**
 * @brief 解析后的结构化控制指令模型
 */
typedef struct {
    char                    msg_id[CONTROL_MSG_ID_MAX_LEN];         /* 指令唯一标识 (RPC 令牌) */
    char                    device[CONTROL_DEVICE_NAME_MAX_LEN];    /* 目标设备名称 */
    char                    command[CONTROL_COMMAND_MAX_LEN];       /* 命令动作字符串 (如 "write") */
    char                    metric[CONTROL_METRIC_NAME_MAX_LEN];    /* 目标测点名称 (如 "temp_limit") */
    control_command_type_t  cmd_type;                               /* 命令类型枚举 */
    sensor_val_type_t       val_type;                               /* 物理量数据类型 */
    sensor_val_t            value;                                  /* 设定物理量工程值 */
    uint32_t                timeout_ms;                             /* 执行超时时限 (毫秒) */
    int64_t                 receive_timestamp_ms;                   /* 指令接收时刻 */
} control_message_t;

/**
 * @brief 控制执行回执模型
 */
typedef struct {
    char                    msg_id[CONTROL_MSG_ID_MAX_LEN];         /* 对应的指令 ID */
    int                     code;                                   /* 状态码 (0=成功) */
    char                    message[64];                            /* 状态描述 ("success", "device_not_found") */
    int64_t                 timestamp_ms;                           /* 回执生成时刻 */
    uint32_t                cost_ms;                                /* 物理执行总耗时 */
} control_response_t;

/**
 * @brief 原始下行数据包载荷 (队列元素类型)
 */
typedef struct {
    char                    payload[CONTROL_RAW_BUF_SIZE];          /* 原始 JSON 字符串缓存 */
    size_t                  length;                                 /* 字符串实际长度 */
} control_raw_packet_t;

/**
 * @brief 幂等性事务缓存项 (防止 MQTT QoS 重复指令二次执行)
 */
typedef struct {
    bool                    is_valid;                               /* 缓存项是否有效 */
    char                    msg_id[CONTROL_MSG_ID_MAX_LEN];         /* 历史指令 ID */
    control_response_t      response;                               /* 历史执行回执快照 */
    int64_t                 timestamp_ms;                           /* 记录时戳 */
} control_txn_record_t;

/**
 * @brief control_manager 模块私有全局上下文
 */
typedef struct {
    bool                    is_running;                             /* 任务运行标志 */
    QueueHandle_t           cmd_queue;                              /* 原始 JSON 指令入队队列 */
    TaskHandle_t            task_handle;                            /* control_task 任务句柄 */
    control_reply_sender_t  reply_sender;                           /* 注入的回执发送函数指针 */
    void                   *reply_user_ctx;                         /* 用户私有上下文 */
    control_txn_record_t    txn_cache[CONTROL_TXN_CACHE_SIZE];      /* 幂等事务循环缓存池 */
    uint8_t                 txn_cache_head;                         /* 环形写入游标 */
    uint32_t                rx_cmd_count;                           /* 累计接收指令数 */
    uint32_t                success_cmd_count;                      /* 累计成功执行数 */
    uint32_t                fail_cmd_count;                         /* 累计失败拒绝数 */
    uint32_t                duplicate_cmd_count;                    /* 累计幂等拦截重复指令数 */
} control_manager_context_t;

/* ========================================================================= */
/*                              内部模块交互接口                             */
/* ========================================================================= */

/**
 * @brief 获取模块全局私有上下文
 */
control_manager_context_t *control_manager_get_context(void);

/**
 * @brief 解析原始 JSON 字符串为结构化指令对象 (control_parser.c)
 * 
 * @param[in]  raw_json 原始 JSON 字符串
 * @param[in]  len      字符串长度
 * @param[out] out_msg  解析生成的结构化指令
 * @param[out] out_err_code 若解析失败，输出对应的标准错误码
 * @return esp_err_t    ESP_OK 表示解析成功且所有字段合法
 */
esp_err_t control_parser_parse(
    const char *raw_json,
    size_t len,
    control_message_t *out_msg,
    int *out_err_code
);

/**
 * @brief 格式化生成 ACK/NACK JSON 回执报文 (control_response.c)
 * 
 * @param[in]  resp     回执结构体
 * @param[out] out_json 输出 JSON 字符串缓冲区
 * @param[in]  max_len  输出缓冲区最大长度
 * @return esp_err_t    ESP_OK 表示格式化成功
 */
esp_err_t control_response_format(
    const control_response_t *resp,
    char *out_json,
    size_t max_len
);

/**
 * @brief control_task 任务主入口循环 (control_task.c)
 */
void control_task_entry(void *pvParameters);

/**
 * @brief 幂等缓存查询：检查是否为重复的 msg_id (control_manager.c)
 * 
 * @param[in]  msg_id            指令 ID
 * @param[out] out_cached_resp   若命中，输出历史回执
 * @return true 命中历史缓存 (属于重复指令), false 首次接收的新指令
 */
bool control_txn_cache_lookup(const char *msg_id, control_response_t *out_cached_resp);

/**
 * @brief 插入最新执行结果至幂等环形缓存池 (control_manager.c)
 * 
 * @param[in] resp 执行回执
 */
void control_txn_cache_insert(const control_response_t *resp);

#endif /* __CONTROL_INTERNAL_H__ */
