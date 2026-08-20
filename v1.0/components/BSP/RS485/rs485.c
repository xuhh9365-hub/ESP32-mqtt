#include "rs485.h"

//CRC16校验函数
static uint16_t modbus_crc16(uint8_t *data, uint16_t length)
{
    // 你的代码
    uint16_t crc = 0xFFFF;
    for(int i=0;i<length;i++)
    {
        crc^= data[i];
        for(int j=0;j<8;j++)
        {
            if(crc&0x0001)
            {
                crc>>=1;
                crc^=0xA001;
            }
            else
            {
                crc>>=1;
            }
        }
    }


    return crc;
}

//组装Modbus请求帧函数
void modbus_build_request(
    uint8_t slave_addr,// 从机地址
    uint16_t start_addr,// 起始寄存器地址
    uint16_t quantity,// 寄存器数量
    uint8_t *request_buffer// 请求帧缓冲区
)
{
    // 1. 构建Modbus请求帧
     
    request_buffer[0] = slave_addr; // 从机地址
    request_buffer[1] = 0x03; // 功能码
    request_buffer[2] = (start_addr >> 8) & 0xFF; // 起始寄存器高字节
    request_buffer[3] = start_addr & 0xFF; // 起始寄存器低字节
    request_buffer[4] = (quantity >> 8) & 0xFF; // 寄存器数量高字节
    request_buffer[5] = quantity & 0xFF; // 寄存器数量低字节
    uint16_t crc = modbus_crc16(request_buffer, 6);
    request_buffer[6] = crc & 0xFF; // CRC低字节
    request_buffer[7] = (crc >> 8) & 0xFF; // CRC高字节

}

//发送Modbus请求函数
static void modbus_send_request(uint8_t *request, uint16_t length)
{
    uart_write_bytes(
        RS485_UART_NUM,
        (const char *)request,
        length
    );
}
//解析Modbus响应函数
static bool modbus_parse_response(
    uint8_t *data,
    uint16_t length,
    uint8_t slave_addr,
    uint16_t quantity,
    uint16_t *registers
)
{
    // ① 长度检查
    if (length != 5 + quantity * 2)
    {
        printf("数据长度错误\n");
        return false;
    }


    // ② 从站地址检查
    if (data[0] != slave_addr)
    {
        printf("从机地址错误\n");
        return false;
    }


    // ③ 功能码检查
    if (data[1]!=0x03)
    {
        printf("功能码错误\n");
        return false;
    }
    


    // ④ 数据字节数检查

    if (data[2] != quantity * 2)
    {
        printf("数据字节数错误\n");
        return false;
    }


    // ⑤ CRC检查
    uint16_t crc = modbus_crc16(data, length - 2);
    uint16_t received_crc =((uint16_t)data[length - 1] << 8) | data[length - 2];
    if (crc != received_crc)
    {
        printf("CRC错误\n");
        return false;
    }

    // ⑥ 解析寄存器
    for (uint16_t  i = 0; i < quantity; i++)
    {
        registers[i] = ((uint16_t)data[3 + i * 2] << 8) | data[4 + i * 2];
    }


    return true;
}
//
    bool modbus_read_holding_registers( uint8_t slave_addr, uint16_t start_addr, uint16_t quantity, uint16_t *registers)
{
    uint8_t request[8];

    // ① 组装
    modbus_build_request(
        slave_addr,
        start_addr,
        quantity,
        request
    );

    // ② 发送
    modbus_send_request(request, 8);

    // ③ 接收
    uint16_t response_length = 5 + quantity * 2;

    uint8_t response[256];
    uint16_t time=0;
   while (1)
    {
        size_t len = 0;
        

        uart_get_buffered_data_len(RS485_UART_NUM, &len);
    

    if (len >= response_length)
    {


        break;
    }
        time+=10;
        if (time > 1000) // 超时1秒
        {   
            return false; // 超时，返回错误
        }
    

    vTaskDelay(pdMS_TO_TICKS(10));

  
    }

    int received_len = uart_read_bytes(
        RS485_UART_NUM,
        response,
        response_length,
        pdMS_TO_TICKS(100)
    );

    if (received_len != response_length)
    {
        printf("接收长度错误\n");
        return false;
    }

    // ⑥ 解析响应
    if (!modbus_parse_response(
            response,
            response_length,
            slave_addr,
            quantity,
            registers))
    {
        return false;
    }

    return true;
}

void rs485_init(uint32_t baudrate)
{



    const uart_config_t uart_config = {
        .baud_rate = baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    // Configure UART parameters
    uart_param_config(RS485_UART_NUM, &uart_config);
    // Set UART pins
    uart_set_pin(RS485_UART_NUM, RS485_UART_TX_PIN, RS485_UART_RX_PIN, RS485_UART_RTS_PIN, UART_PIN_NO_CHANGE);

   
    
    // Install UART driver
    uart_driver_install(RS485_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0);
     uart_set_mode(RS485_UART_NUM, UART_MODE_RS485_HALF_DUPLEX);


}