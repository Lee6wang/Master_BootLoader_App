import serial
import struct
import zlib
import time
import sys

# ======= 根据自己情况修改这里 =======
PORT      = "COM3"          # 串口号：Windows COM5 / Linux "/dev/ttyUSB0"
BAUDRATE  = 115200
BIN_PATH = "D:\\Code\\Clion\\Cubemx\\STM32F407VET6\\BOOTL_APP\\cmake-build-debug-stm32\\app.bin"       # 要烧录的固件 bin 文件
VERSION   = 0x00000001      # 固件版本号，自定义
CHUNK_SIZE = 512            # 每帧数据负载大小（建议 256~1024）
ACK_TIMEOUT = 2.0           # 等待 ACK 超时时间（秒）
MAX_RETRY  = 5              # 单帧最大重试次数
# ===================================

# 帧头
COMM_HEAD1 = 0x55
COMM_HEAD2 = 0xAA

# 命令字（必须和 comm_proto.h 一致）
CMD_HANDSHAKE      = 0x01
CMD_START_UPDATE   = 0x02
CMD_DATA           = 0x03
CMD_END_UPDATE     = 0x04
CMD_QUERY_VERSION  = 0x05
CMD_ACK            = 0x06

# ACK 状态码（和 MCU 侧 CommStatus_t 对应）
COMM_STATUS_OK          = 0x00
COMM_STATUS_FRAME_CRC   = 0x01
COMM_STATUS_PARAM_ERR   = 0x02
COMM_STATUS_FLASH_ERR   = 0x03
COMM_STATUS_STATE_ERR   = 0x04


def calc_crc32(data: bytes) -> int:
    """
    使用和 MCU 一样的 CRC32 算法：
    - 多项式 0xEDB88320
    - 初值 0xFFFFFFFF
    - 结果异或 0xFFFFFFFF
    Python 的 zlib.crc32 就是这一套。
    """
    return zlib.crc32(data) & 0xFFFFFFFF


def build_frame(cmd: int, seq: int, payload: bytes) -> bytes:
    length = len(payload)
    len_l = length & 0xFF
    len_h = (length >> 8) & 0xFF

    header = bytes([
        COMM_HEAD1,
        COMM_HEAD2,
        cmd,
        seq,
        len_l,
        len_h
    ])

    # 帧 CRC：从 CMD 开始，到 DATA 结束
    crc_input = bytes([cmd, seq, len_l, len_h]) + payload
    crc = calc_crc32(crc_input)
    crc_bytes = struct.pack("<I", crc)  # 小端

    return header + payload + crc_bytes


def send_frame(ser: serial.Serial, cmd: int, seq: int, payload: bytes):
    frame = build_frame(cmd, seq, payload)
    ser.write(frame)


def recv_frame(ser: serial.Serial, timeout: float = 1.0):
    """
    从串口读取一帧：
    返回 (cmd, seq, payload) 或 None
    """
    ser.timeout = 0.1
    start_time = time.time()

    state = 0
    buf = bytearray()

    while True:
        if time.time() - start_time > timeout:
            return None

        b = ser.read(1)
        if not b:
            continue

        ch = b[0]

        if state == 0:
            if ch == COMM_HEAD1:
                state = 1
        elif state == 1:
            if ch == COMM_HEAD2:
                state = 2
                buf.clear()
            else:
                state = 0
        elif state >= 2:
            buf.append(ch)
            # 至少要有 CMD、SEQ、LEN_L、LEN_H
            if len(buf) == 4:
                length = buf[2] | (buf[3] << 8)
                # 后面还有 payload + 4 字节 CRC
                expected_len = 4 + length + 4
            # 收满整帧
            if 'expected_len' in locals() and len(buf) >= expected_len:
                cmd = buf[0]
                seq = buf[1]
                length = buf[2] | (buf[3] << 8)
                payload = bytes(buf[4:4+length])
                crc_recv = struct.unpack("<I", buf[4+length:4+length+4])[0]

                crc_input = bytes([cmd, seq, buf[2], buf[3]]) + payload
                crc_calc = calc_crc32(crc_input)

                if crc_calc == crc_recv:
                    return cmd, seq, payload
                else:
                    # CRC 错误就丢弃这帧，继续找下一帧
                    state = 0
                    buf.clear()


def wait_ack(ser: serial.Serial, expect_cmd: int, expect_seq: int, desc: str) -> bool:
    """
    等待一帧 ACK，并检查状态码
    """
    frame = recv_frame(ser, timeout=ACK_TIMEOUT)
    if frame is None:
        print(f"[ERR] 等待 {desc} 的 ACK 超时")
        return False

    cmd, seq, payload = frame

    if cmd != CMD_ACK:
        print(f"[ERR] 收到非 ACK 帧：cmd=0x{cmd:02X}, seq={seq}")
        return False

    if len(payload) < 3:
        print(f"[ERR] ACK payload 长度错误：len={len(payload)}")
        return False

    status = payload[0]
    cmd_echo = payload[1]
    seq_echo = payload[2]

    if cmd_echo != expect_cmd or seq_echo != expect_seq:
        print(f"[ERR] ACK 回显不匹配：cmd_echo=0x{cmd_echo:02X}, seq_echo={seq_echo}")
        return False

    if status != COMM_STATUS_OK:
        print(f"[ERR] ACK 状态错误：status=0x{status:02X}")
        return False

    print(f"[OK ] {desc} -> ACK")
    return True


def handshake(ser: serial.Serial) -> bool:
    print("[*] 发送握手帧...")
    # 握手可以 payload 为空，也可以带点字符串
    payload = b"PC_HANDSHAKE"
    send_frame(ser, CMD_HANDSHAKE, 0, payload)

    frame = recv_frame(ser, timeout=2.0)
    if frame is None:
        print("[ERR] 等待握手响应超时")
        return False

    cmd, seq, payload = frame
    if cmd != CMD_HANDSHAKE:
        print(f"[ERR] 握手响应命令错误：0x{cmd:02X}")
        return False

    print(f"[OK ] 握手成功，返回：{payload!r}")
    return True


def main():
    # 读取固件文件
    try:
        with open(BIN_PATH, "rb") as f:
            fw = f.read()
    except FileNotFoundError:
        print(f"[ERR] 找不到固件文件：{BIN_PATH}")
        return

    total_size = len(fw)
    if total_size == 0:
        print("[ERR] 固件大小为 0")
        return

    # 整体 CRC 与 MCU 的 FlashCV_CalcCRC 保持一致
    image_crc = calc_crc32(fw)
    print(f"[*] 固件大小: {total_size} 字节, CRC32: 0x{image_crc:08X}")

    # 打开串口
    try:
        ser = serial.Serial(PORT, BAUDRATE, timeout=0.1)
    except Exception as e:
        print(f"[ERR] 打开串口失败: {e}")
        return

    time.sleep(0.5)

    try:
        # 1) 握手
        if not handshake(ser):
            return

        # 2) 发送 START_UPDATE
        print("[*] 发送 START_UPDATE...")
        payload = struct.pack("<III", total_size, image_crc, VERSION)
        seq = 1
        send_frame(ser, CMD_START_UPDATE, seq, payload)

        if not wait_ack(ser, CMD_START_UPDATE, seq, "START_UPDATE"):
            return

        # 3) 分块发送数据
        print("[*] 开始发送固件数据...")
        offset = 0
        seq += 1
        frame_index = 0

        while offset < total_size:
            chunk = fw[offset:offset+CHUNK_SIZE]
            payload = struct.pack("<I", offset) + chunk  # [offset | data...]
            ok = False

            for retry in range(MAX_RETRY):
                print(f"[-->] 发送数据帧 #{frame_index}, offset={offset}, len={len(chunk)}, 重试={retry}")
                send_frame(ser, CMD_DATA, seq & 0xFF, payload)

                if wait_ack(ser, CMD_DATA, seq & 0xFF, f"DATA 帧 #{frame_index}"):
                    ok = True
                    break
                else:
                    print("[!!] 重发该帧")

            if not ok:
                print("[ERR] 数据帧发送失败，放弃升级")
                return

            offset += len(chunk)
            seq += 1
            frame_index += 1

        print("[*] 固件数据全部发送完成")

        # 4) 发送 END_UPDATE
        print("[*] 发送 END_UPDATE...")

        dummy_payload = b"\x00"  # 1 字节占位数据，MCU 端不使用它
        ok = False

        for retry in range(MAX_RETRY):
            print(f"[-->] 发送 END_UPDATE 帧, 重试={retry}")
            send_frame(ser, CMD_END_UPDATE, seq & 0xFF, dummy_payload)

            if wait_ack(ser, CMD_END_UPDATE, seq & 0xFF, "END_UPDATE"):
                ok = True
                break
            else:
                print("[!!] END_UPDATE ACK 异常，准备重发")

        if not ok:
            print("[ERR] END_UPDATE 多次失败，放弃升级")
            return

        print("[*] MCU 已接受结束升级请求，接下来会在 Idle 中做整体 CRC 校验 + 写 Meta + 复位")
        print("[*] 请等待板子自动重启（BootLoader 搬运 APP 后再次运行）")

    finally:
        ser.close()


if __name__ == "__main__":
    main()
