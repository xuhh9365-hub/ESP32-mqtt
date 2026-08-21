import random
import sys
import time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM11"  # 可运行时指定,如: python modbus_slave.py COM14
BAUDRATE = 115200
SLAVE_ID = 0x02

# 保持寄存器初始值
registers = {
    0x0010: 253,    # 温度寄存器, 初始 25.3℃ (单位 0.1℃)
    0x0011: 652,    # 湿度寄存器, 初始 65.2% (单位 0.1%)
    0x0012: 1,      # 设备状态, 初始 1 (运行中)
    0x0013: 400,    # 温度上限, 初始 40.0℃ (单位 0.1℃)
    0x0014: 800,    # 湿度下限, 初始 80.0% (单位 0.1%)
}

temperature = 253  # 25.3℃, 单位 0.1℃


def update_temperature():
    """每次读请求后, 温度在 -0.1 / 0 / +0.1℃ 之间随机微变。"""
    global temperature
    temperature += random.choice([-1, 0, 1])
    registers[0x0010] = temperature


def modbus_crc16(data):
    """计算 Modbus RTU CRC16 校验码"""
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
    """构建 0x03 读保持寄存器响应帧"""
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
    """
    工业级低延迟 Modbus RTU 请求帧读取函数：
    非阻塞检测，根据 3.5 字符静默间隔和功能码精准切帧，毫秒级响应。
    """
    if ser.in_waiting == 0:
        time.sleep(0.005)
        return None

    # 等待 5ms 确保整包字节已进入串口芯片缓冲区
    time.sleep(0.005)
    raw = bytearray(ser.read(ser.in_waiting))

    # 如果字节太少，稍作等待读齐
    deadline = time.time() + 0.03
    while len(raw) < 8 and time.time() < deadline:
        if ser.in_waiting > 0:
            raw.extend(ser.read(ser.in_waiting))
        else:
            time.sleep(0.002)

    if len(raw) < 8:
        return None

    # 寻找从站 ID 帧头
    start_idx = raw.find(bytes([SLAVE_ID]))
    if start_idx < 0:
        return None

    frame = raw[start_idx:]
    if len(frame) < 4:
        return None

    func_code = frame[1]
    if func_code in (0x03, 0x06):
        expected_len = 8
    elif func_code == 0x10:
        # 0x10 写多个寄存器: 第 7 字节 (frame[6]) 为 byte_count
        while len(frame) < 7 and time.time() < deadline:
            if ser.in_waiting > 0:
                frame.extend(ser.read(ser.in_waiting))
            time.sleep(0.002)
        if len(frame) < 7:
            return None
        byte_count = frame[6]
        expected_len = 7 + byte_count + 2
    else:
        return None

    # 补齐剩余数据字节
    while len(frame) < expected_len and time.time() < deadline:
        if ser.in_waiting > 0:
            frame.extend(ser.read(ser.in_waiting))
        time.sleep(0.002)

    if len(frame) < expected_len:
        return None

    actual_frame = bytes(frame[:expected_len])

    # 校验 CRC16
    crc = modbus_crc16(actual_frame[:-2])
    received_crc = actual_frame[-2] | (actual_frame[-1] << 8)
    if crc != received_crc:
        return None

    return actual_frame


ser = serial.Serial(
    port=PORT,
    baudrate=BAUDRATE,
    bytesize=8,
    parity=serial.PARITY_NONE,
    stopbits=1,
    timeout=0.01
)

print(f"==================================================")
print(f"Modbus 虚拟从站已启动: {PORT} @ {BAUDRATE} (从站ID: 0x{SLAVE_ID:02X})")
print(f"支持功能码: 0x03 (读保持寄存器) / 0x06 (写单个) / 0x10 (写多个)")
print(f"等待 ESP32-S3 请求...")
print(f"==================================================")

while True:
    request = read_request(ser)
    if request is None:
        continue

    func_code = request[1]

    # ==================== 0x03：读保持寄存器 ====================
    if func_code == 0x03:
        start_addr = (request[2] << 8) | request[3]
        quantity = (request[4] << 8) | request[5]
        update_temperature()
        response = build_response(start_addr, quantity)
        ser.write(response)
        print(f"📖 [0x03 读] 地址=0x{start_addr:04X}, 数量={quantity} | 温度={temperature/10:.1f}℃, 温度上限={registers[0x0013]/10.0:.1f}℃, 状态={registers[0x0012]}")

    # ==================== 0x06：写单个保持寄存器 ====================
    elif func_code == 0x06:
        reg_addr = (request[2] << 8) | request[3]
        reg_val = (request[4] << 8) | request[5]
        registers[reg_addr] = reg_val
        ser.write(request)  # 0x06 响应：原样回传
        print(f"✍️  [0x06 写成功] 寄存器 0x{reg_addr:04X} ➔ 新数值 = {reg_val} ({reg_val/10.0 if reg_addr in (0x0010,0x0011,0x0013,0x0014) else reg_val})")

    # ==================== 0x10：写多个保持寄存器 ====================
    elif func_code == 0x10:
        start_addr = (request[2] << 8) | request[3]
        quantity = (request[4] << 8) | request[5]
        for i in range(quantity):
            val = (request[7 + i * 2] << 8) | request[8 + i * 2]
            addr = start_addr + i
            registers[addr] = val
            print(f"✍️  [0x10 写成功] 寄存器 0x{addr:04X} ➔ 新数值 = {val} ({val/10.0 if addr in (0x0010,0x0011,0x0013,0x0014) else val})")

        # 0x10 标准响应帧 (8字节): SlaveID(1B) + 0x10(1B) + StartAddr(2B) + Quantity(2B) + CRC(2B)
        resp = bytearray([SLAVE_ID, 0x10, request[2], request[3], request[4], request[5]])
        crc = modbus_crc16(resp)
        resp.append(crc & 0xFF)
        resp.append((crc >> 8) & 0xFF)
        ser.write(resp)