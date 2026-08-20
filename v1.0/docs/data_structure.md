# JSON配置数据结构设计


## 1. 设计目标


设备参数不固定在代码中。


通过修改JSON实现设备适配。



---


# 2. 配置文件示例



device_config.json



```json
{

"devices":

[

{

"name":"temperature_sensor",

"slave_id":1,


"register":

{

"address":16,

"type":"holding"

},


"data":

{

"type":"uint16",

"scale":0.1

},


"period":1000


}


]

}
```

---

# 3. 字段说明

| 字段        | 说明       |
| --------- | -------- |
| name      | 设备名称     |
| slave_id  | Modbus地址 |
| address   | 寄存器地址    |
| type      | 寄存器类型    |
| data type | 数据类型     |
| scale     | 倍率       |
| period    | 采集周期     |

---

# 4. 数据流程

JSON

↓

cJSON解析

↓

device_config_t

↓

device_manager

↓

Modbus任务
