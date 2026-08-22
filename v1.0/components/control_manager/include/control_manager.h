#ifndef __CONTROL_MANAGER_H__
#define __CONTROL_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/*                      标准工业控制状态码与错误码定义                        */
/* ========================================================================= */

#define CONTROL_OK                          (0)     /* 控制执行成功 */
#define CONTROL_ERR_INVALID_JSON            (400)   /* JSON 格式非法或缺失必要字段 */
#define CONTROL_ERR_UNSUPPORTED_CMD         (401)   /* 不支持的命令类型 (非 write) */
#define CONTROL_ERR_PARAM_OUT_OF_RANGE      (402)   /* 设定物理量超出测点量程 */
#define CONTROL_ERR_DEVICE_NOT_FOUND        (404)   /* 指定设备名称未注册 */
#define CONTROL_ERR_METRIC_NOT_FOUND        (405)   /* 指定设备无此控制测点 */
#define CONTROL_ERR_DEVICE_OFFLINE          (407)   /* 目标设备处于离线状态被拦截 */
#define CONTROL_ERR_WRITE_FAILED            (500)   /* 底层 Modbus 通信或校验失败 */
#define CONTROL_ERR_TIMEOUT                 (504)   /* 等待总线空闲或从机应答超时 */
#define CONTROL_ERR_QUEUE_FULL              (507)   /* 控制指令队列已满 (过载保护) */

/* ========================================================================= */
/*                              类型与回调定义                               */
/* ========================================================================= */

/**
 * @brief 控制回执发送函数指针类型 (由 MQTT 适配层注入)
 * 
 * @param[in] topic        发布的目标回执 Topic (如 "gateway/control/response")
 * @param[in] json_payload 回执 JSON 字符串
 * @param[in] user_ctx     用户上下文指针 (如 MQTT Client 句柄)
 * @return esp_err_t       ESP_OK 表示发布成功
 */
typedef esp_err_t (*control_reply_sender_t)(const char *topic, const char *json_payload, void *user_ctx);

/* ========================================================================= */
/*                               公共 API 接口                               */
/* ========================================================================= */

/**
 * @brief 初始化 control_manager (创建 FreeRTOS 指令队列与事务缓存)
 * 
 * @return esp_err_t ESP_OK 表示初始化成功
 */
esp_err_t control_manager_init(void);

/**
 * @brief 启动 control_manager 核心工作任务 (control_task)
 * 
 * @return esp_err_t ESP_OK 表示启动成功
 */
esp_err_t control_manager_start(void);

/**
 * @brief 停止 control_manager 任务
 * 
 * @return esp_err_t ESP_OK 表示停止成功
 */
esp_err_t control_manager_stop(void);

/**
 * @brief 检查 control_manager 是否处于运行状态
 * 
 * @return true 运行中, false 未运行
 */
bool control_manager_is_running(void);

/**
 * @brief 下行控制报文推入接口 (供 MQTT 接收回调极速调用)
 * @note  0 阻塞非等待推入；若队列满则严禁静默丢弃旧指令，明确返回 ESP_ERR_NO_MEM
 * 
 * @param[in] json_str 原始 JSON 字符串
 * @param[in] json_len 字符串字节长度
 * @return esp_err_t   ESP_OK 入队成功，ESP_ERR_NO_MEM 队列已满拒绝
 */
esp_err_t control_manager_push_raw_json(const char *json_str, size_t json_len);

/**
 * @brief 注册控制执行回执发送通道
 * 
 * @param[in] sender   回调函数指针
 * @param[in] user_ctx 用户上下文 (如 MQTT Client 句柄或指定主题)
 * @return esp_err_t   ESP_OK 注册成功
 */
esp_err_t control_manager_register_reply_sender(control_reply_sender_t sender, void *user_ctx);

#ifdef __cplusplus
}
#endif

#endif /* __CONTROL_MANAGER_H__ */
