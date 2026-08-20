# 项目开发路线


## Phase 1

完成配置驱动框架


目标：


JSON

↓

config_manager

↓

device_manager

↓

自动创建Modbus任务



状态：

进行中



---


# Phase 2


加入Flash存储


实现：


NVS/SPIFFS保存配置



断电后保持配置。



---


# Phase 3


Web配置


实现：


浏览器

↓

ESP32 Web Server

↓

修改配置

↓

保存Flash



---


# Phase 4


工业化增强


增加：


- Ethernet
- 4G通信
- OTA升级
- 用户权限
- Modbus TCP
