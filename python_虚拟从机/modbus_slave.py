import random
import sys
import time

import serial


PORT = sys.argv[1] if len(sys.argv) > 1 else "COM11"  # 可运行时指定,如: python modbus_slave.py COM14
BAUDRATE = 115200
SLAVE_ID = 0x02

registers = {
    0x0010: 253,    # 温度寄存器,初始25.3℃ (单位0.1℃)
    0x0011: 652,
    0x0012: 1,
    0x0013: 400,
    0x0014: 800,
}

temperature = 253  # 25.3℃,单位0.1℃


def update_temperature():
    """每次请求后,温度在 -0.1 / 0 / +0.1℃ 之间随机变化。"""
    global temperature
    temperature += random.choice([-1, 0, 1])
    registers[0x0010] = temperature


def modbus_crc16(data):
    crc = 0xFFFF

    for byte in data:
        crc ^= byte

        for _ in range(8):
            if crc & 0x0001:
                crc >>= 1
                crc ^= 0xA001
            else:
                crc >>= 1

    return crc


def build_response(start_addr, quantity):
    if not (1 <= quantity <= 125):
        quantity = 1

    data = bytearray([
        SLAVE_ID,       # 从站地址
        0x03,           # 功能码
        quantity * 2,   # 数据字节数
    ])

    for i in range(quantity):
        addr = start_addr + i
        value = registers.get(addr, 0)
        data.append((value >> 8) & 0xFF)  # 高字节
        data.append(value & 0xFF)         # 低字节

    crc = modbus_crc16(data)

    data.append(crc & 0xFF)
    data.append((crc >> 8) & 0xFF)

    return data


def read_request(ser):
    """读取一帧8字节的Modbus RTU请求(读保持寄存器的请求固定为8字节)。"""
    buf = bytearray()
    deadline = time.time() + 0.2

    while len(buf) < 8 and time.time() < deadline:
        buf += ser.read(8 - len(buf))

    return bytes(buf) if len(buf) == 8 else None


ser = serial.Serial(
    port=PORT,
    baudrate=BAUDRATE,
    bytesize=8,
    parity=serial.PARITY_NONE,
    stopbits=1,
    timeout=0.05
)

print(f"Modbus从站已启动: {PORT} @ {BAUDRATE}")
print("等待ESP32请求...")

while True:

    request = read_request(ser)

    if request is None:
        continue

    if request[0] != SLAVE_ID or request[1] != 0x03:
        ser.reset_input_buffer()  # 垃圾帧,清缓冲重新同步
        continue

    # 检查CRC
    crc = modbus_crc16(request[:6])
    received_crc = request[6] | (request[7] << 8)

    if crc != received_crc:
        print("CRC错误:", request.hex(" "))
        continue

    print("收到:", request.hex(" "))

    start_addr = (request[2] << 8) | request[3]
    quantity = (request[4] << 8) | request[5]

    print(f"读取:起始地址=0x{start_addr:04X}, 数量={quantity}")

    update_temperature()
    print(f"温度更新: {temperature/10:.1f}℃ (寄存器=0x{temperature:04X})")

    response = build_response(start_addr, quantity)

    print("发送:", response.hex(" "))

    ser.write(response)