# AI_CONTEXT


## 项目名称

ESP32-S3工业物联网边缘网关



## 项目目标


设计一个类似商业工业网关的系统。


核心：

配置驱动型Modbus设备管理。



---

# 技术栈


Hardware:

ESP32-S3


Framework:

ESP-IDF 5.5


Language:

C


RTOS:

FreeRTOS



通信：

RS485

Modbus RTU

MQTT

WiFi



---

# 当前完成


已经完成：

- RS485驱动
- Modbus Master基础通信
- CRC校验
- Modbus数据解析
- MQTT上传
- MQTT控制下发



---

# 当前开发任务


实现：


config_manager

↓

device_manager

↓

动态创建Modbus采集任务



---

# 开发原则


1. 不允许设备参数硬编码。


错误：

modbus_read(1,0x0010)



正确：

读取device_config



---


2. 模块必须解耦。



config_manager:

只管理配置。



modbus:

只负责协议。



mqtt:

只负责通信。



---


# 当前限制


不要大范围修改：

rs485_driver

modbus_master

mqtt接口


优先新增模块。


---

# AI工作要求


修改代码前：

1. 先分析当前架构

2. 给出修改方案

3. 确认接口设计

4. 再生成代码



不要一次生成整个项目。
