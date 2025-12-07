# STM32 IAP 升级工具

这是一个用于STM32微控制器的In-Application Programming (IAP)升级工具，包含命令行版本和图形界面版本。

## 功能特点

- 支持通过串口对STM32微控制器进行固件升级
- 提供命令行界面(iap_send.py)和图形用户界面(iap_gui.py)两种使用方式
- 实现了完整的IAP通信协议，包括握手、开始更新、数据传输、结束更新等步骤
- 具有错误检测和重传机制，确保升级过程的可靠性
- 支持大文件分块传输，可配置块大小
- 使用CRC32校验确保数据完整性

## 目录结构

```
iap_send/
├── iap_send.py      # 命令行版本IAP工具
├── iap_gui.py       # 图形界面版本IAP工具
└── README.md        # 说明文档
```

## 依赖环境

- Python 3.x
- pyserial库

安装依赖：
```bash
pip install pyserial
```

## 使用方法

### 命令行版本(iap_send.py)

1. 修改脚本中的配置参数：
   ```python
   PORT      = "COM3"          # 串口号
   BAUDRATE  = 115200          # 波特率
   BIN_PATH  = "path/to/app.bin"  # 固件路径
   VERSION   = 0x00000001      # 固件版本号
   ```

2. 运行脚本：
   ```bash
   python iap_send.py
   ```

### 图形界面版本(iap_gui.py)

直接运行脚本启动GUI界面：
```bash
python iap_gui.py
```

在图形界面中可以：
1. 选择可用的串口设备
2. 设置波特率（默认115200）
3. 输入固件版本号（支持十进制或十六进制）
4. 浏览并选择要烧录的.bin固件文件
5. 点击"开始升级"按钮执行升级操作

![1](..\Figure\1.png)

## 协议说明

IAP通信协议包含以下命令：

| 命令 | 值 | 说明 |
|------|----|------|
| CMD_HANDSHAKE | 0x01 | 握手命令 |
| CMD_START_UPDATE | 0x02 | 开始升级 |
| CMD_DATA | 0x03 | 数据传输 |
| CMD_END_UPDATE | 0x04 | 结束升级 |
| CMD_QUERY_VERSION | 0x05 | 查询版本 |
| CMD_ACK | 0x06 | 应答 |

### 帧格式

每帧数据由以下部分组成：
```
帧头(2字节) | 命令(1字节) | 序号(1字节) | 长度(2字节) | 数据(N字节) | CRC32(4字节)
```

- 帧头：固定为0x55 0xAA
- 命令：表示操作类型
- 序号：帧序号，用于确认机制
- 长度：数据部分的长度
- 数据：具体的数据内容
- CRC32：从命令到数据部分的CRC32校验值

### 升级流程

1. 握手：PC端发送握手请求，MCU回应确认连接
2. 开始升级：发送固件总大小、整体CRC和版本号
3. 数据传输：分块发送固件数据，每帧都有ACK确认和重传机制
4. 结束升级：通知MCU升级完成，MCU进行最终校验并重启

## 配置参数

### 通用参数
- CHUNK_SIZE：每帧数据负载大小（建议256~1024，默认512）
- ACK_TIMEOUT：等待ACK超时时间（秒，默认2.0）
- MAX_RETRY：单帧最大重试次数（默认5）

### 命令行版本独有参数
- PORT：串口号（如"COM3"或"/dev/ttyUSB0"）
- BAUDRATE：波特率（默认115200）
- BIN_PATH：固件文件路径
- VERSION：固件版本号

## 错误处理

工具实现了多种错误检测和处理机制：

- CRC校验：确保数据传输的完整性
- 序号确认：保证帧顺序正确
- 超时重传：应对数据丢失情况
- 状态码反馈：提供详细的错误信息

ACK状态码说明：
- COMM_STATUS_OK (0x00)：操作成功
- COMM_STATUS_FRAME_CRC (0x01)：帧CRC错误
- COMM_STATUS_PARAM_ERR (0x02)：参数错误
- COMM_STATUS_FLASH_ERR (0x03)：Flash操作错误
- COMM_STATUS_STATE_ERR (0x04)：状态错误

## 注意事项

1. 确保MCU端已实现对应的IAP协议
2. 选择正确的串口号和波特率
3. 固件文件必须是.bin格式
4. 升级过程中不要断开串口连接
5. 升级完成后MCU会自动重启运行新固件

## 开发说明

此工具需要配合实现了相应IAP协议的STM32固件使用。协议的具体实现细节需参考MCU端代码。