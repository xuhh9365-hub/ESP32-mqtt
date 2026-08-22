#ifndef __MODBUS_H__
#define __MODBUS_H__

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_modbus_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/*                   1. 通用 Modbus Master 动态接口 (新设计)                  */
/* ========================================================================= */

/**
 * @brief 初始化并启动 Modbus Master 协议栈 (默认 115200 波特率)
 * 
 * @return esp_err_t ESP_OK 表示初始化成功
 */
esp_err_t modbus_master_init(void);

/**
 * @brief 使用自定义波特率初始化并启动 Modbus Master 协议栈
 * 
 * @param baudrate 串口波特率 (如 9600, 115200 等)
 * @return esp_err_t ESP_OK 表示初始化成功
 */
esp_err_t modbus_master_init_baud(uint32_t baudrate);

/**
 * @brief 重置/重建 Modbus Master 协议栈
 */
esp_err_t modbus_master_reset(void);

/**
 * @brief 动态读取保持寄存器 (功能码 0x03)
 * 
 * @param slave_id 从站地址 (1 ~ 247)
 * @param reg_addr 起始寄存器地址 (0x0000 ~ 0xFFFF)
 * @param reg_num  读取寄存器数量 (1 ~ 125)
 * @param[out] dest_buf 目标缓冲区 (由调用者分配，大小至少为 reg_num * sizeof(uint16_t))
 * @return esp_err_t ESP_OK 表示读取成功
 */
esp_err_t modbus_master_read_holding_registers(uint8_t slave_id,
                                               uint16_t reg_addr,
                                               uint16_t reg_num,
                                               uint16_t *dest_buf);

/**
 * @brief 动态读取输入寄存器 (功能码 0x04)
 * 
 * @param slave_id 从站地址 (1 ~ 247)
 * @param reg_addr 起始寄存器地址 (0x0000 ~ 0xFFFF)
 * @param reg_num  读取寄存器数量 (1 ~ 125)
 * @param[out] dest_buf 目标缓冲区 (大小至少为 reg_num * sizeof(uint16_t))
 * @return esp_err_t ESP_OK 表示读取成功
 */
esp_err_t modbus_master_read_input_registers(uint8_t slave_id,
                                             uint16_t reg_addr,
                                             uint16_t reg_num,
                                             uint16_t *dest_buf);

/**
 * @brief 动态写入单个保持寄存器 (功能码 0x06)
 * 
 * @param slave_id 从站地址 (1 ~ 247)
 * @param reg_addr 目标寄存器地址
 * @param value 写入数值 (16位无符号整型)
 * @return esp_err_t ESP_OK 表示写入成功
 */
esp_err_t modbus_master_write_single_register(uint8_t slave_id,
                                              uint16_t reg_addr,
                                              uint16_t value);

/**
 * @brief 动态写入多个保持寄存器 (功能码 0x10)
 * 
 * @param slave_id 从站地址 (1 ~ 247)
 * @param reg_addr 起始寄存器地址
 * @param reg_num  写入寄存器数量 (1 ~ 123)
 * @param src_buf  待写入数据源缓冲区
 * @return esp_err_t ESP_OK 表示写入成功
 */
esp_err_t modbus_master_write_multiple_registers(uint8_t slave_id,
                                                 uint16_t reg_addr,
                                                 uint16_t reg_num,
                                                 const uint16_t *src_buf);

/* ========================================================================= */
/*               2. 兼容旧版本业务 API (为保证 main.c 兼容性暂留)              */
/* ========================================================================= */

// Modbus 特征 ID
enum {
    CID_DEV_DATA = 0,
    CID_SET_TEMP_LIMIT,
    CID_SET_HUMI_LIMIT,
    CID_SET_STATUS,
    CID_COUNT
};

// 保持寄存器原始数据结构体
typedef struct {
    uint16_t temperature;
    uint16_t humidity;
    uint16_t status;
    uint16_t temp_limit;
    uint16_t humi_limit;
} __attribute__((packed)) holding_reg_params_t;

// 网关兼容层数据结构体 (旧版 Demo)
typedef struct {
    float    temperature;
    float    humidity;
    uint16_t status;
    float    temp_limit;
    float    humi_limit;
} modbus_legacy_data_t;

esp_err_t modbus_master_read_all(modbus_legacy_data_t *out_data);
esp_err_t modbus_master_write_temp_limit(float limit);
esp_err_t modbus_master_write_humi_limit(float limit);
esp_err_t modbus_master_write_status(uint16_t status);

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_H__ */
