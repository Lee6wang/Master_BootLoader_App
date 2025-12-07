# STM32 Bootloader

一个基于STM32F407VET6的嵌入式bootloader实现，支持固件升级功能。

## 项目概述

本项目是一个针对STM32F407VET6微控制器设计的bootloader，实现了安全可靠的固件升级机制。bootloader运行在Flash的起始区域(0x08000000)，负责检测、验证和安装新的固件映像，然后跳转到应用程序。

## 硬件平台

- 主控芯片: STM32F407VET6
- Flash容量: 512KB
- RAM容量: 128KB

## 存储区域划分

| 区域名称 | 地址范围 | 扇区 | 用途 |
|---------|---------|------|------|
| Bootloader区 | 0x08000000 - 0x08007FFF | 扇区0-1 | 存放bootloader代码 |
| 元数据区 | 0x08007F00 - 0x08007FFF | 扇区1末256字节 | 存放升级元数据 |
| Application区 | 0x08008000 - 0x0801FFFF | 扇区2-4 | 存放主应用程序 |
| Download区 | 0x08020000 - 0x0805FFFF | 扇区5-6 | 存放待升级固件 |

## 功能特性

1. **安全升级机制**：
   - 支持双区备份（Application区和Download区）
   - CRC32校验确保固件完整性
   - 升级过程中断电保护

2. **升级流程**：
   - 检查元数据中的升级标志
   - 验证待升级固件的大小和CRC
   - 擦除应用程序区域
   - 将新固件从下载区复制到应用程序区
   - 复制完成后再次校验确保正确性
   - 清除升级标志，防止重复升级

3. **应用程序跳转**：
   - 验证应用程序的有效性
   - 重新配置中断向量表
   - 跳转到应用程序入口点

## 代码结构

```
Core/                    // STM32CubeMX生成的核心代码
├── Inc/                 // 头文件
└── Src/                 // 源文件
Drivers/                 // STM32驱动库
├── CMSIS/               // ARM CMSIS
└── STM32F4xx_HAL_Driver/ // ST官方HAL库
HardWare/                // 用户硬件相关代码
├── Inc/                 // 硬件头文件
│   ├── Bootloader.h     // Bootloader接口定义
│   └── FlashCV.h        // Flash操作相关定义
└── Src/                 // 硬件源文件
    ├── Bootloader.c     // Bootloader核心逻辑
    └── FlashCV.c        // Flash操作实现
```

## 工作原理

### 启动流程

1. 系统复位后首先运行Bootloader
2. 检查是否存在有效的固件升级请求
3. 如有需要则执行固件升级过程
4. 跳转到应用程序入口地址运行主程序
5. 如果跳转失败，则进入错误指示状态（LED闪烁）

### 升级机制

1. 上位机将新固件写入Download区
2. 上位机更新元数据区的升级标志和固件信息
3. 系统复位后Bootloader检测到升级标志
4. Bootloader验证固件完整性后进行搬运
5. 搬运完成后清除升级标志

## 编译构建

本项目使用CMake构建系统，配合CLion开发环境：

```bash
# 创建构建目录
mkdir build && cd build

# 配置项目
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/gcc-arm-none-eabi.cmake

# 编译项目
make
```

## 使用方法

1. 编译bootloader并烧录到STM32Flash起始地址
2. 开发应用程序时，需设置应用程序的起始地址为0x08008000
3. 通过串口或其他通信接口发送升级命令和固件数据
4. 系统重启后自动完成升级并运行新固件

## 注意事项

1. 应用程序必须从0x08008000地址开始编译链接
2. 升级过程中应保证电源稳定，避免中途断电
3. 固件大小不能超过Application区容量(112KB)
4. 所有Flash操作均已考虑扇区擦除特性

## 依赖项

- STM32CubeMX HAL库
- ARM GCC工具链
- CMake构建系统

## 许可证

本项目仅供学习交流使用，遵循MIT许可证。