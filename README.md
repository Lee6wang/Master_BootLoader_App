# STM32 Bootloader与IAP升级完整解决方案

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

这是一个完整的STM32微控制器引导加载程序(Bootloader)和应用内编程(IAP)升级解决方案，包含固件、上位机工具以及Python实现的升级工具。

## 项目作者
## 项目作者

- **姓名**: lihaitao
- **日期**: 2025年7月12日
- **邮箱**: lht20030718@buaa.edu.cn

## 项目概述

本项目提供了一个完整的STM32嵌入式系统固件升级方案，包含三个主要组成部分：
1. **BootLoader** - 运行在MCU上的引导加载程序
2. **IAP_APP** - 支持IAP升级的应用程序框架
3. **IAP_Tool_Python** - 用于固件升级的Python上位机工具

该方案使STM32微控制器能够通过串口等通信接口实现固件的安全可靠升级，适用于需要远程更新固件的嵌入式产品。

## 项目结构

```
Master_BootLoader_App/
├── BootLoader/           # 引导加载程序
├── IAP_APP/              # 支持IAP升级的应用程序
├── IAP_Tool_Python/      # Python实现的IAP上位机工具
└── README.md             # 项目顶层说明文档
```

### BootLoader 引导加载程序

位于 [BootLoader/](BootLoader/) 目录下，是一个基于STM32F407VET6微控制器设计的bootloader，实现了安全可靠的固件升级机制。

#### 主要特性
- 在系统启动时运行，检查是否有新的固件需要升级
- 支持固件完整性校验（CRC32）
- 安全的升级流程，避免升级过程中断电导致系统损坏
- 升级完成后跳转到应用程序

详细信息请参阅 [BootLoader/README.md](BootLoader/README.md)

### IAP_APP 应用程序

位于 [IAP_APP/](IAP_APP/) 目录下，是一个基于STM32F407VET6微控制器和FreeRTOS实时操作系统的应用程序示例，支持通过IAP方式进行固件升级。

#### 主要特性
- 基于FreeRTOS的任务调度
- UART串口通信实现IAP协议
- 支持固件分块接收和校验
- 与BootLoader协同工作实现完整的升级流程

详细信息请参阅 [IAP_APP/README.md](IAP_APP/README.md)

### IAP_Tool_Python 上位机工具

位于 [IAP_Tool_Python/](IAP_Tool_Python/) 目录下，是一套使用Python编写的IAP上位机工具，用于通过串口将固件发送到MCU。

#### 主要特性
- 提供命令行和图形界面两种交互方式
- 实现完整的IAP通信协议
- 支持固件校验和重传机制
- 错误检测和处理机制

详细信息请参阅 [IAP_Tool_Python/README.md](IAP_Tool_Python/README.md)

## 工作原理

整个IAP升级流程如下：

1. **准备阶段**
   - 用户使用IAP工具准备好待升级的固件(.bin文件)
   - MCU上电或复位后首先运行BootLoader

2. **BootLoader检查**
   - BootLoader检查是否有升级标志
   - 如果没有升级需求，则直接跳转到应用程序
   - 如果有升级需求，则等待接收新固件

3. **固件传输**
   - 用户运行IAP工具连接MCU
   - 通过IAP协议将固件分块传输到MCU的下载缓冲区

4. **固件验证与安装**
   - MCU对接收到的固件进行完整性校验
   - 校验通过后，将固件从缓冲区搬运到应用程序区域
   - 清除升级标志

5. **运行新固件**
   - 系统重启后BootLoader检查到无升级需求
   - 跳转到应用程序入口地址运行新固件

## 开发环境

### 嵌入式软件
- IDE: CLion / Keil / STM32CubeIDE
- 编译器: GCC ARM Embedded / ARMCC
- 构建系统: CMake
- 调试工具: OpenOCD / ST-Link

### 上位机软件
- Python 3.x
- pyserial库
- tkinter (用于GUI界面)

## 快速开始

1. 编译和烧录 [BootLoader](BootLoader/) 到STM32微控制器
2. 编译 [IAP_APP](IAP_APP/) 应用程序，确保链接地址正确设置
3. 准备好要升级的固件文件(.bin格式)
4. 运行 [IAP_Tool_Python](IAP_Tool_Python/) 工具进行固件升级

## Flash分区布局

| 区域 | 地址范围 | 扇区 | 用途 | 大小 |
|------|----------|------|------|------|
| Bootloader区 | 0x08000000-0x08007FFF | 0-1 | 存放Bootloader代码 | 32KB |
| 元数据区 | 0x08007F00-0x08007FFF | 1末尾 | 存放升级标志和元数据 | 256字节 |
| Application区 | 0x08008000-0x0801FFFF | 2-4 | 存放主应用程序 | 96KB |
| Download区 | 0x08020000-0x0805FFFF | 5-6 | 存放待升级固件 | 256KB |

## 许可证

本项目采用MIT许可证，详情请见 [LICENSE](LICENSE) 文件。

## 联系方式

如有任何问题、建议或反馈，请提交Issue或联系项目维护者。