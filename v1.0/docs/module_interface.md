# 模块接口设计


## 1. config_manager


路径：

components/config_manager



职责：

管理设备配置。



接口：


```c
void config_manager_init();


int config_get_device_num();


int config_get_device(
        int index,
        device_config_t *device
);


int config_save();
```

数据结构：

```c
typedef struct
{

char name[32];

uint8_t slave_id;

uint16_t register_addr;

float scale;

uint32_t period;


}device_config_t;
```

---

# 2. device_manager

接口：

```c
void device_manager_start();


void device_add(
device_config_t *config
);
```

功能：

读取配置

↓

创建Modbus任务

---

# 3. modbus_master

接口：

```c
int modbus_read_register(

uint8_t slave,

uint16_t addr,

uint16_t *value

);
```

输入：

Slave ID

Register地址

输出：

寄存器数据

---

# 4. mqtt_client

接口：

```c
void mqtt_publish_data(

char *json

);
```

---

# 模块依赖关系

```
main

 |

config_manager

 |

device_manager

 |

modbus_master

 |

rs485_driver



mqtt_client

↑

data_process
```
