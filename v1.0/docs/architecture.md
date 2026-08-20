# ESP32工业网关软件架构设计


## 1. 设计思想


本项目采用分层架构设计。


核心思想：

业务逻辑与硬件通信分离。


设备参数不写死在代码中。

采用：

配置驱动方式。



---

# 2. 软件总体架构



```
            MQTT Cloud

                ↑

          mqtt_client


                ↑


          data_process


                ↑


        device_manager


                ↑


        config_manager


                ↑


          JSON/NVS


               


          modbus_master


                ↑


          rs485_driver


                ↑


           ESP32 UART
```



---

# 3. 模块职责


## config_manager


负责：

- 配置文件读取
- JSON解析
- 参数保存
- 配置更新



不负责：

- Modbus通信
- 数据上传



---

## device_manager


负责：

- 管理设备实例
- 根据配置创建任务
- 管理设备生命周期



输入：

device_config_t


输出：

Modbus采集任务



---

## modbus_master


负责：

- Modbus RTU协议
- 请求帧生成
- 数据解析
- CRC校验



不负责：

- MQTT
- Web
- 配置



---

## mqtt_client


负责：

- MQTT连接
- Publish
- Subscribe



---

## web_server


负责：

- 提供网页
- 接收用户配置
- 调用config_manager保存



---

# 4. FreeRTOS任务规划



```
main_task
|
|
device_manager
|
|
---
|        |          |
Task1   Task2     Task3
设备1   设备2      设备3

mqtt_task
web_task
```



---

# 5. 扩展设计


未来支持：


Ethernet

4G

Modbus TCP

OTA


通过增加驱动层实现。


上层业务无需修改。
