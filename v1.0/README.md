# 基于 ESP32-S3 的多协议工业物联网边缘网关

English:

Design and Implementation of ESP32-S3 Based Industrial IoT Edge Gateway


## 1. 项目简介

本项目设计并实现一种基于 ESP32-S3 的低成本工业物联网边缘网关。

针对传统工业设备（PLC、温控仪、电能表、传感器等）通常采用 RS485 + Modbus RTU 通信，但缺少网络连接能力的问题。

通过 ESP32-S3 实现：

工业设备

↓

RS485 / Modbus RTU

↓

ESP32-S3工业网关

↓

MQTT

↓

云端平台


实现工业设备的数据采集、协议转换、远程监控。


---

# 2. 项目目标


设计一个具有通用性的工业边缘网关。


区别于传统固定程序模式：


修改代码

↓

重新编译

↓

重新烧录

↓

适配设备



本项目采用：


设备配置

↓

参数解析

↓

自动生成采集任务

↓

数据上传



实现：

- 新增设备无需修改固件
- 修改设备参数无需重新烧录
- 支持多个Modbus设备接入


---

# 3. 已实现功能


## Modbus RTU Master

实现：

- RS485通信
- Modbus RTU协议
- CRC16校验
- Holding Register读取
- Register数据解析


## MQTT通信

实现：

- WiFi连接
- MQTT连接服务器
- 数据上传
- MQTT控制下发


## 数据处理

实现：

- 数据格式转换
- JSON封装
- cJSON解析



---

# 4. 计划实现功能


## 配置驱动设备管理

通过JSON文件保存设备信息：


设备名称

Slave ID

寄存器地址

数据类型

倍率

采集周期



启动后自动加载配置。


---

## Web配置


用户通过浏览器访问ESP32：


192.168.x.x


实现：

- WiFi配置
- 添加设备
- 修改Modbus参数
- 保存配置



---

# 5. 软件架构


```
main
├── wifi_manager
├── mqtt_client
├── rs485_driver
├── modbus_master
├── config_manager
├── device_manager
├── web_server
└── data_process
```


详细架构：

docs/architecture.md


---

# 6. 开发环境


Hardware:

- ESP32-S3


Software:

- ESP-IDF 5.5
- FreeRTOS
- C Language


Components:

- esp-modbus
- mqtt
- cJSON



---

# 7. 当前开发阶段


Current Phase:

配置驱动型设备管理


目标：

实现：

config_manager

↓

device_manager

↓

自动创建Modbus任务
