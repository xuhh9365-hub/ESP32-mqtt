# ESP32工业网关项目架构分析报告 (v1.0)

---

## 1. 项目当前状态

### 1.1 硬件与基础环境
- **核心芯片**：ESP32-S3（双核 Xtensa LX7 @ 240MHz，支持硬件半双工 RS485 UART 控制、WiFi STA/AP、BLE）
- **开发框架**：ESP-IDF v5.5 + FreeRTOS + CMake 体系
- **第三方组件**：`espressif/esp-modbus` (v2.1.3)、`cJSON`、`mqtt` (ESP-IDF 官方组件)
- **引脚分配**：
  - RS485 UART1：TX = GPIO 4, RX = GPIO 5, RTS = GPIO 6
  - 状态指示 LED：GPIO 1
  - 按键 BOOT：GPIO 0

### 1.2 当前开发阶段判定
项目当前处于 **从“原型硬编码验证阶段（PoC/Demo）”向“Phase 1: 配置驱动型设备管理”过渡的阶段**。
- **已完成能力**：已打通 `RS485/Modbus RTU -> ESP32-S3 -> 队列中转 -> MQTT 上报` 以及 `云端 MQTT 下发 -> JSON解析 -> Modbus 写入` 的端到端闭环链路。
- **当前瓶颈**：现存代码采用**单传感器专用硬编码**模式，不具备通用工业网关所需的配置驱动与多设备动态管理能力。

---

## 2. 当前软件架构图

### 2.1 运行时任务与模块交互架构

```text
                                  ┌───────────────────────────┐
                                  │      MQTT Cloud Broker     │
                                  │   (192.168.0.7:1883)      │
                                  └─────────────▲─────────────┘
                                                │ MQTT Pub / Sub
                                                ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│ ESP32-S3 Firmware                                                                           │
│                                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────────────────────┐  │
│  │                                       main.c                                          │  │
│  │                                                                                       │  │
│  │  ┌─────────────────────────┐   sensor1_queue   ┌───────────────────────────────────┐  │  │
│  │  │       modbus_task       ├──────────────────►│             MQTT_task             │  │  │
│  │  │ (固定1s周期/单设备轮询) │   (gateway_data_t)│ (snprintf硬编码JSON并Publish)     │  │  │
│  │  └────────────┬────────────┘                   └─────────────────▲─────────────────┘  │  │
│  │               │                                                  │                    │  │
│  │               │                                                  │ mqtt_app_publish   │  │
│  │               │                                                  │                    │  │
│  │  ┌────────────▼────────────────────────────┐   回调函数注册      │                    │  │
│  │  │   on_mqtt_control_received 回调函数     │◄────────────────────┤                    │  │
│  │  │ (cJSON解析后直接调用写Modbus函数)       │                     │                    │  │
│  │  └────────────┬────────────────────────────┘                     │                    │  │
│  └───────────────┼──────────────────────────────────────────────────┼────────────────────┘  │
│                  │                                                  │                       │
│                  ▼                                                  ▼                       │
│  ┌───────────────────────────────────┐            ┌───────────────────────────────────┐     │
│  │   components/BSP/MODBUS (modbus.c)│            │ components/Middlewares/MQTT       │     │
│  │   - 硬编码数据字典(0x0010~0x0014) │            │ (mqtt_client_app.c)               │     │
│  │   - 绑定特定从站地址 0x02         │            │ - 管理连接状态与事件循环          │     │
│  │   - 物理量换算 (raw / 10.0f)      │            │ - 订阅固定控制主题                │     │
│  └───────────────────┬───────────────┘            └───────────────────────────────────┘     │
│                      │                                                                      │
│                      ▼                                                                      │
│  ┌───────────────────────────────────┐            ┌───────────────────────────────────┐     │
│  │   components/BSP/RS485 (rs485.c)  │            │ components/BSP/WIFI (wifi_sta.c)  │     │
│  │   - UART1 引脚与硬件半双工设置    │            │ - 硬编码 WiFi SSID/PWD            │     │
│  │   - [冗余] 手写Modbus帧组装与解析 │            │ - 阻塞等待网络就绪                │     │
│  └───────────────────┬───────────────┘            └───────────────────────────────────┘     │
│                      │                                                                      │
└──────────────────────┼──────────────────────────────────────────────────────────────────────┘
                       │ RS485 Bus (UART1)
                       ▼
       ┌───────────────────────────────┐
       │ 工业从站设备 (温湿度传感器 0x02) │
       └───────────────────────────────┘
```

### 2.2 当前数据流

```text
【上行采集流】
  从站设备(0x02)
       │ (RS485 差分信号)
       ▼
  [rs485.c] UART1 接收
       │
       ▼
  [modbus.c] modbus_master_read_all() 读取 5 个 Holding Register (0x0010~0x0014)
       │ 内部直接做转换: temp = raw / 10.0f
       ▼
  [main.c] modbus_task 封装为 gateway_data_t
       │ xQueueSend(sensor1_queue, ...)
       ▼
  [main.c] MQTT_task 从队列接收
       │ snprintf() 组装固定 JSON 字符串: {"temperature":..., "humidity":...}
       ▼
  [mqtt_client_app.c] mqtt_app_publish("home/room1/dht11", payload, 1)
       │
       ▼
  MQTT Broker (EMQX/Cloud)

--------------------------------------------------------------------------------

【下行控制流】
  MQTT Broker
       │ 下发消息至 "home/room1/control"
       ▼
  [mqtt_client_app.c] mqtt_event_handler (MQTT_EVENT_DATA)
       │ 调用注册的 s_data_callback
       ▼
  [main.c] on_mqtt_control_received()
       │ cJSON_Parse 解析 "temp_limit" / "humi_limit" / "status"
       ▼
  [modbus.c] modbus_master_write_temp_limit() 等专用函数
       │ mbc_master_set_parameter (写 0x0013 / 0x0014 / 0x0012)
       ▼
  [rs485.c] UART1 发送 Modbus 写帧至从站 (0x02)
```

---

## 3. 当前代码分析

经过对现有源文件的完整静态分析，各组件现状如下：

### 3.1 `main/` 目录 (`main.c`)
- **职责现状**：充当所有业务逻辑的中枢与“粘合胶水”。
- **具体行为**：
  1. 顺序执行 `led_init()`, `modbus_master_init()`, `wifi_sta_init()`, `mqtt_app_start()`。
  2. 创建深度为 1 的全局静态队列 `sensor1_queue`（传递特定结构体 `gateway_data_t`）。
  3. 创建 3 个 FreeRTOS 任务：`LED_task`（优先级 1）、`modbus_task`（优先级 4）、`MQTT_task`（优先级 3）。
  4. 承载具体的 MQTT 下发 JSON 解析与 Modbus 写指令派发。

### 3.2 `components/BSP/`
1. **`MODBUS` (`modbus.c`)**：
   - 基于官方 `esp-modbus` 组件实现了 Master 驱动。
   - 内部定义了静态常量数组 `device_parameters[]`，固定绑定了 Slave ID `0x02`、寄存器 `0x0010~0x0014`。
   - 定义了温湿度特化的结构体 `holding_reg_params_t` 与 `gateway_data_t`。
   - 提供了业务特化的函数：`modbus_master_write_temp_limit`、`modbus_master_write_humi_limit`、`modbus_master_write_status`。
2. **`RS485` (`rs485.c`)**：
   - 配置了 UART1 硬件半双工模式（GPIO 4/5/6）。
   - **代码冗余**：包含了一整套手工实现的 CRC16 校验、组包、单寄存器读取函数（`modbus_read_holding_registers`），与 `MODBUS/modbus.c` 中的 `esp-modbus` 协议栈产生功能重叠。
3. **`WIFI` (`wifi_sta.c`)**：
   - 基于 ESP-IDF Event Loop 实现了 STA 模式连接。
   - SSID/PWD 宏写死在代码中（`"88888"` / `"888888aa"`），`wifi_sta_init()` 会阻塞等待连接成功。
4. **`LED` (`led.c`) & `KEY` (`key.c`)**：
   - 基础 GPIO 输入输出驱动，功能独立完备。

### 3.3 `components/Middlewares/`
1. **`MQTT` (`mqtt_client_app.c`)**：
   - 封装了 ESP-IDF `mqtt_client`。
   - 提供了 `mqtt_app_start()`、`mqtt_app_publish()`、`mqtt_app_set_data_callback()` 接口。
   - Broker URI（`"mqtt://192.168.0.7:1883"`）、主题（`"home/room1/dht11"`、`"home/room1/control"`）在头文件中宏定义。

---

## 4. 存在问题分析

### 4.1 严重的问题 1：设备参数与协议强绑定（硬编码）
- **现象**：从机地址 `0x02`、寄存器 `0x0010`、数据长度、倍率 `0.1`、JSON 键名全部以常量或结构体形式固化在 C 源码中。
- **后果**：现场如果需要增加一台电表或更换温控器，必须改写 C 语言结构体、修改数据字典、重新编译工程并烧录固件，违背工业网关“通用适配”的核心目标。

### 4.2 严重的问题 2：分层不清与职责倒挂
- **现象**：
  - `modbus.h` 包含了业务层字段（`temperature`, `humidity`, `temp_limit` 等），协议层“知道”了业务含义。
  - `main.c` 既管理任务生命周期，又直接调用协议栈写函数，还亲自负责 JSON 字段序列化/反序列化。
- **后果**：模块间无法独立复用或进行单元测试。

### 4.3 严重的问题 3：RS485 共享总线互斥机制缺失（重大并发隐患）
- **现象**：
  - RS485 是半双工串行总线，同一时刻只能有一个 Master 请求在总线上进行。
  - 当前代码中，`modbus_task` 在周期性轮询读从站；而云端下发控制指令时，`on_mqtt_control_received` 是在 MQTT 事件循环的上下文中**直接且同步**调用 `modbus_master_write_xxx()`。
- **后果**：一旦云端下发控制与定时轮询在同一瞬间发生，将产生**严重的总线数据冲突（Collision）与 UART 驱动重入**，导致 Modbus 通信超时或崩溃。

### 4.4 严重的问题 4：MQTT 回调中执行同步阻塞操作
- **现象**：`on_mqtt_control_received` 直接执行 Modbus 阻塞通信（超时可达数百毫秒）。
- **后果**：阻塞了 ESP-IDF 内部的 MQTT 事件循环线程，可能导致 MQTT 心跳包未能按时发送而被 Broker 断开连接。

### 4.5 严重的问题 5：代码冗余与资产混乱
- **现象**：`rs485.c` 中手写的 Modbus 协议解析代码与 `modbus.c` 中的 `esp-modbus` 协议栈共存，职责重叠。

---

## 5. 与目标架构差距（Gap Analysis）

根据 `docs/architecture.md` 与 `docs/module_interface.md`，差距按优先级梳理如下：

| 优先级 | 缺失/待重构模块 | 现状差距 | 影响 |
| :--- | :--- | :--- | :--- |
| **P0 (必须解决)** | **`config_manager` 缺失** | 无配置解析层，无 `device_config_t` 数据模型与设备列表管理 | 无法实现配置驱动，无法脱离重新编译 |
| **P0 (必须解决)** | **`device_manager` 缺失** | 仅有单个硬编码任务，无根据配置动态创建与管理采集任务的机制 | 无法接入多设备 |
| **P0 (必须解决)** | **`modbus_master` 协议纯化与互斥** | 协议栈与温湿度业务绑定，且无 RS485 总线互斥锁 | 通用性差，多任务/下发时并发崩溃 |
| **P1 (重要优化)** | **`data_process` 缺失** | 物理量缩放（`raw * scale`）和 JSON 封装散落在 `modbus.c` 与 `main.c` 中 | 无法支持不同数据类型的动态上报 |
| **P1 (重要优化)** | **MQTT 下发异步化** | 下发直接同步写从站 | 阻塞 MQTT 线程，缺乏统一命令分发通道 |
| **P1 (重要优化)** | **驱动层清理** | `rs485.c` 包含冗余 Modbus 逻辑 | 代码混乱，维护成本高 |
| **P2 (后续阶段)** | **NVS/Flash 存储** | 属于 Phase 2 规划（断电持久化配置） | 当前阶段可先支持内置 JSON 字符串 |
| **P2 (后续阶段)** | **Web 配置页面** | 属于 Phase 3 规划 | 当前阶段不引入 HTTP Web Server |

---

## 6. 下一阶段开发计划（Phase 1 实施方案）

> **本阶段核心目标**：实现 `config_manager` $\rightarrow$ `device_manager` $\rightarrow$ `动态创建 Modbus 采集任务` $\rightarrow$ `通用数据处理与上传`（**暂不引入 Web**）。

### 6.1 模块职责划分与接口设计

#### 模块 1：`config_manager`（配置管理组件）
- **路径**：`components/config_manager/`
- **职责**：
  1. 提供统一的设备配置数据结构 `device_config_t`。
  2. 解析 JSON 配置字符串（Phase 1 阶段内置静态默认配置 JSON，为 Phase 2 NVS 存储预留输入）。
  3. 维护设备配置列表，提供只读查询接口。
- **核心数据结构与接口**：
  ```c
  typedef struct {
      char name[32];          // 设备名称，如 "temperature_sensor"
      uint8_t slave_id;       // Modbus 从站地址
      uint16_t register_addr; // 寄存器起始地址
      char reg_type[16];      // "holding", "input" 等
      char data_type[16];     // "uint16", "int16", "float" 等
      float scale;            // 缩放倍率，如 0.1
      uint32_t period_ms;     // 采集周期(ms)，如 1000
  } device_config_t;

  esp_err_t config_manager_init(void);
  int config_get_device_num(void);
  esp_err_t config_get_device(int index, device_config_t *device);
  esp_err_t config_load_from_json(const char *json_str);
  ```

#### 模块 2：`modbus_master`（通用协议栈与总线互斥层）
- **路径**：重构 `components/BSP/MODBUS/`（或更名为 `components/modbus_master/`）
- **职责**：
  1. 仅负责 Modbus RTU 标准协议通信，剥离所有业务逻辑。
  2. 封装 **RS485 总线互斥锁（Mutex）**，确保多任务轮询与云端下发并发安全。
- **核心接口**：
  ```c
  esp_err_t modbus_master_init(void);
  esp_err_t modbus_read_holding_registers(uint8_t slave_id, uint16_t start_addr, uint16_t reg_num, uint16_t *dest_buf);
  esp_err_t modbus_write_single_register(uint8_t slave_id, uint16_t reg_addr, uint16_t value);
  ```

#### 模块 3：`device_manager`（设备生命周期与任务管理组件）
- **路径**：`components/device_manager/`
- **职责**：
  1. 系统启动后调用 `config_manager` 获取设备配置数量与参数。
  2. 动态为每个设备创建独立的 FreeRTOS 采集任务（或基于单任务时间轮调度器）。
  3. 任务根据配置周期定时调用 `modbus_master` 读取寄存器。
  4. 采集结果交由 `data_process` 处理。
- **核心接口**：
  ```c
  esp_err_t device_manager_init(void);
  esp_err_t device_manager_start(void);
  ```

#### 模块 4：`data_process`（通用数据处理与打包）
- **路径**：`components/data_process/`
- **职责**：
  1. 接收原始寄存器数据，根据 `device_config_t` 中的 `scale` 和 `data_type` 换算为物理量。
  2. 格式化为通用的 JSON 报文（如包含设备名、时间戳/周期、物理量数值），投递至 MQTT 发送队列。
- **核心接口**：
  ```c
  esp_err_t data_process_format_json(const device_config_t *cfg, uint16_t raw_val, char *json_buf, size_t max_len);
  ```

### 6.2 新增与修改文件清单

```text
├── components/
│   ├── config_manager/         [NEW] 配置管理器
│   │   ├── CMakeLists.txt
│   │   ├── config_manager.c
│   │   └── include/config_manager.h
│   │
│   ├── device_manager/         [NEW] 设备管理器
│   │   ├── CMakeLists.txt
│   │   ├── device_manager.c
│   │   └── include/device_manager.h
│   │
│   ├── data_process/           [NEW] 通用数据加工组件
│   │   ├── CMakeLists.txt
│   │   ├── data_process.c
│   │   └── include/data_process.h
│   │
│   ├── BSP/
│   │   ├── MODBUS/             [MODIFY] 纯化为通用Modbus接口，增加总线Mutex
│   │   └── RS485/              [MODIFY] 清理冗余的手写Modbus代码
│   │
│   └── Middlewares/
│       └── MQTT/               [MODIFY] 接收通用JSON消息队列，控制下发改为异步队列
│
└── main/
    └── main.c                  [MODIFY] 精简为系统引导装配，消除具体业务硬编码
```

*以上报告严格基于当前工程实际代码分析生成，未改动任何源码文件。*
