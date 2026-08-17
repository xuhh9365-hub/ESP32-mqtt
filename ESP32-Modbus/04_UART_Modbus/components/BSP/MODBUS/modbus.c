#include "modbus.h"
#include "rs485.h"        // 引用引脚定义
#include <stddef.h>       // offsetof

static const char *TAG = "MODBUS_MASTER";
static void *master_handler = NULL;   // Modbus 主机句柄

// ========== 数据字典（描述表） ==========
// 工业级最佳实践：一次性批量读取 5 个连续的保持寄存器 (0x0010 ~ 0x0014)
// 这样每次轮询只需 1 次请求-响应交互，避免了多次单寄存器轮询导致的状态错位和时序竞争
const mb_parameter_descriptor_t device_parameters[] = {
    {
        .cid = CID_DEV_DATA,
        .param_key = "DevData",
        .param_units = "raw",
        .mb_slave_addr = 0x02,
        .mb_param_type = MB_PARAM_HOLDING,
        .mb_reg_start = 0x0010,
        .mb_size = 5,
        .param_offset = 0,
        .param_type = PARAM_TYPE_U16,
        .param_size = sizeof(holding_reg_params_t),
        .param_opts = { .opt1 = 0, .opt2 = 0, .opt3 = 0 },
        .access = PAR_PERMS_READ
    }
};

const uint16_t num_device_parameters = sizeof(device_parameters) / sizeof(device_parameters[0]);

// ========== 初始化函数 ==========
esp_err_t modbus_master_init(void)
{
    esp_err_t err;

    if (master_handler != NULL) {
        return ESP_OK;
    }

    // 第1步：创建串口 Master（配置波特率、模式、校验等）
    mb_communication_info_t comm = {
        .ser_opts = {
            .mode = MB_RTU,
            .port = RS485_UART_NUM,       // UART_NUM_1
            .baudrate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .stop_bits = UART_STOP_BITS_1,
            .parity = UART_PARITY_DISABLE,
        }
    };
    err = mbc_master_create_serial(&comm, &master_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "创建串口Master失败: %s", esp_err_to_name(err));
        return err;
    }

    // 第2步：绑定物理引脚
    err = uart_set_pin(RS485_UART_NUM, RS485_UART_TX_PIN, RS485_UART_RX_PIN,
                       RS485_UART_RTS_PIN, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置UART引脚失败: %s", esp_err_to_name(err));
        return err;
    }

    // 第3步：开启 RS485 硬件半双工（ESP32 自动控制 RTS 收发切换）
    err = uart_set_mode(RS485_UART_NUM, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置RS485模式失败: %s", esp_err_to_name(err));
        return err;
    }

    // 第4步：注册数据字典
    err = mbc_master_set_descriptor(master_handler, device_parameters, num_device_parameters);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "注册数据字典失败: %s", esp_err_to_name(err));
        return err;
    }

    // 第5步：启动协议栈
    err = mbc_master_start(master_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "启动协议栈失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Modbus Master 初始化成功!");
    return ESP_OK;
}

// ========== 异常重置函数 ==========
esp_err_t modbus_master_reset(void)
{
    ESP_LOGW(TAG, "重置 Modbus Master 协议栈状态...");
    if (master_handler != NULL) {
        mbc_master_delete(master_handler);
        master_handler = NULL;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    return modbus_master_init();
}

// ========== 读取函数 ==========
esp_err_t modbus_master_read_all(gateway_data_t *out_data)
{
    if (out_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (master_handler == NULL) {
        esp_err_t init_err = modbus_master_init();
        if (init_err != ESP_OK) {
            return init_err;
        }
    }

    esp_err_t err;
    uint8_t type = 0;
    holding_reg_params_t raw_regs = {0};

    // 一次性读取 5 个保持寄存器
    err = mbc_master_get_parameter(master_handler, CID_DEV_DATA, (uint8_t *)&raw_regs, &type);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "从站数据读取失败: %s", esp_err_to_name(err));
        // 当通信超时或异常中断时，重置协议栈内部状态，防止旧事件残留在 EventGroup 中
        modbus_master_reset();
        return err;
    }

    // 解析转换物理量
    out_data->temperature = (float)raw_regs.temperature / 10.0f;
    out_data->humidity    = (float)raw_regs.humidity / 10.0f;
    out_data->status      = raw_regs.status;
    out_data->temp_limit  = (float)raw_regs.temp_limit / 10.0f;
    out_data->humi_limit  = (float)raw_regs.humi_limit / 10.0f;

    return ESP_OK;
}
