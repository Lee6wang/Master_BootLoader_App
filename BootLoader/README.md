# BOOTLOADER

这是一个基于STM32F407的引导程序（Bootloader），用于加载和运行应用程序内核。

author:Haitao Li
date:11/21/2025# BOOTLOADER

**作者**: Haitao Li  
**创建日期**: 11/21/2025  
**版本**: 1.0.0  
**邮箱**: haitaoli_0718@qq.com


## 功能特性

- **固件升级**: 支持通过外部接口进行固件更新
- **Flash管理**: 管理Flash存储器的分区和数据读写
- **CRC校验**: 使用标准CRC-32算法确保固件完整性
- **安全启动**: 验证应用程序有效性后跳转执行

## 系统架构

### Flash分区布局
```c
#define FLASH_BOOT_START_ADDR      0x08000000UL  // 扇区0~1: Bootloader区域
#define FLASH_APP_START_ADDR       0x08008000UL  // 扇区2~4: 应用程序区域
#define FLASH_META_ADDR            0x08007F00UL  // 扇区1末尾: 元数据存储
#define FLASH_DOWNLOAD_START_ADDR  0x08020000UL  // 扇区5~6: 下载缓冲区
```


### 主要组件
- `Bootloader.c`: 主控制逻辑，负责升级检查和应用跳转
- `FlashCV.c`: Flash操作接口，提供读写和擦除功能
- 元数据管理: 存储升级标志、固件大小、CRC校验等信息

## 工作流程

1. **启动检查**: 检查是否存在有效的固件升级请求
2. **数据验证**: 验证下载区固件的完整性和正确性
3. **固件搬运**: 将新固件从下载区复制到应用程序区
4. **应用跳转**: 验证并跳转到应用程序入口点

## 注意事项

- 确保Flash地址分配不重叠
- 升级过程中保持电源稳定
- 建议在调试模式下验证所有Flash操作