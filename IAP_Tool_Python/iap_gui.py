import serial
import struct
import zlib
import time
import threading
import sys

import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from serial.tools import list_ports

# ===================== 升级协议相关常量 =====================

# 命令字
CMD_HANDSHAKE      = 0x01
CMD_START_UPDATE   = 0x02
CMD_DATA           = 0x03
CMD_END_UPDATE     = 0x04
CMD_QUERY_VERSION  = 0x05
CMD_ACK            = 0x06

# 帧头
COMM_HEAD1 = 0x55
COMM_HEAD2 = 0xAA

# ACK 状态码
COMM_STATUS_OK          = 0x00
COMM_STATUS_FRAME_CRC   = 0x01
COMM_STATUS_PARAM_ERR   = 0x02
COMM_STATUS_FLASH_ERR   = 0x03
COMM_STATUS_STATE_ERR   = 0x04

# 其他参数
CHUNK_SIZE   = 512       # 每帧负载大小
ACK_TIMEOUT  = 2.0       # 等待 ACK 超时时间(s)
MAX_RETRY    = 5         # 单帧最大重试次数


# ===================== CRC & 帧处理函数 =====================

def calc_crc32(data: bytes) -> int:
    """跟 MCU 一致的 CRC32 算法"""
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

    crc_input = bytes([cmd, seq, len_l, len_h]) + payload
    crc = calc_crc32(crc_input)
    crc_bytes = struct.pack("<I", crc)  # 小端

    return header + payload + crc_bytes


def send_frame(ser: serial.Serial, cmd: int, seq: int, payload: bytes):
    frame = build_frame(cmd, seq, payload)
    ser.write(frame)


def recv_frame(ser: serial.Serial, timeout: float = 1.0):
    """从串口读取一帧，返回 (cmd, seq, payload) 或 None"""
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
        else:
            buf.append(ch)
            # 前 4 字节是 CMD, SEQ, LEN_L, LEN_H
            if len(buf) == 4:
                length = buf[2] | (buf[3] << 8)
                expected_len = 4 + length + 4  # 头部4 + payload + CRC4
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
                    # CRC 错误，丢弃
                    state = 0
                    buf.clear()


def wait_ack(ser: serial.Serial, expect_cmd: int, expect_seq: int,
             desc: str, log_func=print) -> bool:
    frame = recv_frame(ser, timeout=ACK_TIMEOUT)
    if frame is None:
        log_func(f"[ERR] 等待 {desc} 的 ACK 超时")
        return False

    cmd, seq, payload = frame

    if cmd != CMD_ACK:
        log_func(f"[ERR] 收到非 ACK 帧：cmd=0x{cmd:02X}, seq={seq}")
        return False

    if len(payload) < 3:
        log_func(f"[ERR] ACK payload 长度错误：len={len(payload)}")
        return False

    status = payload[0]
    cmd_echo = payload[1]
    seq_echo = payload[2]

    if cmd_echo != expect_cmd or seq_echo != expect_seq:
        log_func(f"[ERR] ACK 回显不匹配：cmd_echo=0x{cmd_echo:02X}, seq_echo={seq_echo}")
        return False

    if status != COMM_STATUS_OK:
        log_func(f"[ERR] ACK 状态错误：status=0x{status:02X}")
        return False

    log_func(f"[OK ] {desc} -> ACK")
    return True


def handshake(ser: serial.Serial, log_func=print) -> bool:
    log_func("[*] 发送握手帧...")
    payload = b"PC_HANDSHAKE"
    send_frame(ser, CMD_HANDSHAKE, 0, payload)

    frame = recv_frame(ser, timeout=2.0)
    if frame is None:
        log_func("[ERR] 等待握手响应超时")
        return False

    cmd, seq, payload = frame
    if cmd != CMD_HANDSHAKE:
        log_func(f"[ERR] 握手响应命令错误：0x{cmd:02X}")
        return False

    log_func(f"[OK ] 握手成功，返回：{payload!r}")
    return True


# ===================== 升级主流程函数 =====================

def do_upgrade(port: str, baud: int, bin_path: str, version: int, log_func=print):
    # 读取固件
    try:
        with open(bin_path, "rb") as f:
            fw = f.read()
    except FileNotFoundError:
        log_func(f"[ERR] 找不到固件文件：{bin_path}")
        return

    total_size = len(fw)
    if total_size == 0:
        log_func("[ERR] 固件大小为 0")
        return

    image_crc = calc_crc32(fw)
    log_func(f"[*] 固件大小: {total_size} 字节, CRC32: 0x{image_crc:08X}")

    # 打开串口
    try:
        ser = serial.Serial(port, baudrate=baud, timeout=0.1)
    except Exception as e:
        log_func(f"[ERR] 打开串口失败: {e}")
        return

    time.sleep(0.5)

    try:
        # 1) 握手
        if not handshake(ser, log_func=log_func):
            return

        # 2) START_UPDATE
        log_func("[*] 发送 START_UPDATE...")
        payload = struct.pack("<III", total_size, image_crc, version)
        seq = 1
        send_frame(ser, CMD_START_UPDATE, seq, payload)

        if not wait_ack(ser, CMD_START_UPDATE, seq, "START_UPDATE", log_func=log_func):
            return

        # 3) DATA 帧
        log_func("[*] 开始发送固件数据...")
        offset = 0
        seq += 1
        frame_index = 0

        while offset < total_size:
            chunk = fw[offset:offset+CHUNK_SIZE]
            payload = struct.pack("<I", offset) + chunk

            ok = False
            for retry in range(MAX_RETRY):
                log_func(f"[-->] DATA帧 #{frame_index}, offset={offset}, len={len(chunk)}, 重试={retry}")
                send_frame(ser, CMD_DATA, seq & 0xFF, payload)

                if wait_ack(ser, CMD_DATA, seq & 0xFF,
                            f"DATA 帧 #{frame_index}", log_func=log_func):
                    ok = True
                    break
                else:
                    log_func("[!!] 重发该 DATA 帧")

            if not ok:
                log_func("[ERR] 数据帧发送失败，放弃升级")
                return

            offset += len(chunk)
            seq += 1
            frame_index += 1

        log_func("[*] 固件数据全部发送完成")

        # 4) END_UPDATE（带1字节占位 payload + 重试）
        log_func("[*] 发送 END_UPDATE...")
        dummy_payload = b"\x00"
        ok = False

        for retry in range(MAX_RETRY):
            log_func(f"[-->] 发送 END_UPDATE 帧, 重试={retry}")
            send_frame(ser, CMD_END_UPDATE, seq & 0xFF, dummy_payload)

            if wait_ack(ser, CMD_END_UPDATE, seq & 0xFF,
                        "END_UPDATE", log_func=log_func):
                ok = True
                break
            else:
                log_func("[!!] END_UPDATE ACK 异常，准备重发")

        if not ok:
            log_func("[ERR] END_UPDATE 多次失败，放弃升级")
            return

        log_func("[*] MCU 已接受结束升级请求，接下来会在 Idle 中做整体 CRC 校验 + 写 Meta + 复位")
        log_func("[*] 请等待板子自动重启（BootLoader 搬运 APP 后再次运行）")

    finally:
        ser.close()


# ===================== Tkinter GUI =====================

class IAPGui(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("STM32 IAP 升级工具")
        self.geometry("640x480")

        # 上半部分参数设置
        frame_top = ttk.LabelFrame(self, text="参数配置")
        frame_top.pack(fill=tk.X, padx=10, pady=10)

        # 串口选择
        ttk.Label(frame_top, text="串口:").grid(row=0, column=0, padx=5, pady=5, sticky="e")
        self.combo_port = ttk.Combobox(frame_top, width=15, state="readonly")
        self.combo_port.grid(row=0, column=1, padx=5, pady=5, sticky="w")

        btn_refresh = ttk.Button(frame_top, text="刷新", command=self.refresh_ports)
        btn_refresh.grid(row=0, column=2, padx=5, pady=5)

        # 波特率
        ttk.Label(frame_top, text="波特率:").grid(row=1, column=0, padx=5, pady=5, sticky="e")
        self.entry_baud = ttk.Entry(frame_top, width=15)
        self.entry_baud.grid(row=1, column=1, padx=5, pady=5, sticky="w")
        self.entry_baud.insert(0, "115200")

        # 版本号
        ttk.Label(frame_top, text="版本号(十六进制):").grid(row=2, column=0, padx=5, pady=5, sticky="e")
        self.entry_version = ttk.Entry(frame_top, width=15)
        self.entry_version.grid(row=2, column=1, padx=5, pady=5, sticky="w")
        self.entry_version.insert(0, "0x00010001")

        # 固件路径
        ttk.Label(frame_top, text="固件文件:").grid(row=3, column=0, padx=5, pady=5, sticky="e")
        self.entry_bin = ttk.Entry(frame_top, width=40)
        self.entry_bin.grid(row=3, column=1, padx=5, pady=5, sticky="w")
        btn_browse = ttk.Button(frame_top, text="浏览...", command=self.browse_bin)
        btn_browse.grid(row=3, column=2, padx=5, pady=5)

        # 开始按钮
        self.btn_start = ttk.Button(frame_top, text="开始升级", command=self.on_start)
        self.btn_start.grid(row=4, column=1, padx=5, pady=10)

        # 日志窗口
        frame_log = ttk.LabelFrame(self, text="日志输出")
        frame_log.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        self.text_log = tk.Text(frame_log, wrap="word")
        self.text_log.pack(fill=tk.BOTH, expand=True)

        # 初始化串口列表
        self.refresh_ports()

        # 升级线程
        self.upgrade_thread = None

    def log(self, msg: str):
        self.text_log.insert(tk.END, msg + "\n")
        self.text_log.see(tk.END)
        self.update_idletasks()

    def refresh_ports(self):
        ports = list_ports.comports()
        items = [p.device for p in ports]
        self.combo_port["values"] = items
        if items and not self.combo_port.get():
            self.combo_port.set(items[0])

    def browse_bin(self):
        file_path = filedialog.askopenfilename(
            title="选择固件 bin 文件",
            filetypes=[("BIN 文件", "*.bin"), ("所有文件", "*.*")]
        )
        if file_path:
            self.entry_bin.delete(0, tk.END)
            self.entry_bin.insert(0, file_path)

    def on_start(self):
        if self.upgrade_thread and self.upgrade_thread.is_alive():
            messagebox.showwarning("提示", "升级进行中，请稍候...")
            return

        port = self.combo_port.get().strip()
        baud_str = self.entry_baud.get().strip()
        bin_path = self.entry_bin.get().strip()
        version_str = self.entry_version.get().strip()

        if not port:
            messagebox.showerror("错误", "请选择串口")
            return
        if not baud_str.isdigit():
            messagebox.showerror("错误", "波特率必须是数字")
            return
        try:
            baud = int(baud_str)
        except ValueError:
            messagebox.showerror("错误", "波特率格式错误")
            return

        if not bin_path:
            messagebox.showerror("错误", "请先选择固件文件")
            return

        try:
            if version_str.lower().startswith("0x"):
                version = int(version_str, 16)
            else:
                version = int(version_str)
        except ValueError:
            messagebox.showerror("错误", "版本号格式错误，请输入十进制或0x前缀十六进制")
            return

        self.log("========================================")
        self.log(f"端口: {port}, 波特率: {baud}, 版本号: 0x{version:08X}")
        self.log(f"固件: {bin_path}")
        self.log("开始升级...")

        # 启动子线程执行升级，避免卡死界面
        self.btn_start.config(state=tk.DISABLED)

        def run_upgrade():
            try:
                do_upgrade(port, baud, bin_path, version, log_func=self.log)
            finally:
                self.btn_start.config(state=tk.NORMAL)

        self.upgrade_thread = threading.Thread(target=run_upgrade, daemon=True)
        self.upgrade_thread.start()


if __name__ == "__main__":
    app = IAPGui()
    app.mainloop()
