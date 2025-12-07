#ifndef __FLASH_CV_H
#define __FLASH_CV_H

#include "stm32f4xx_hal.h"

/**
 * @brief Flash 各区域起始与结束地址定义
 */
#define FLASH_BOOT_START_ADDR      0x08000000UL      // Bootloader 区域起始地址（扇区0~1）
#define FLASH_BOOT_END_ADDR        0x08007FFFUL      // Bootloader 区域结束地址

#define FLASH_APP_START_ADDR       0x08008000UL      // 应用程序区域起始地址（扇区2~4）
#define FLASH_APP_END_ADDR         0x0801FFFFUL      // 应用程序区域结束地址

#define FLASH_META_ADDR            0x08007F00UL      // 元数据存储地址（位于扇区1末尾，占用256字节）
#define FLASH_DOWNLOAD_START_ADDR  0x08020000UL      // 下载缓冲区起始地址（扇区5~6）
#define FLASH_DOWNLOAD_END_ADDR    0x0805FFFFUL      // 下载缓冲区结束地址

/**
 * @brief 升级状态标识符
 */
#define UPGRADE_FLAG_EMPTY   0xFFFFFFFFUL           // 空闲状态，无升级任务
#define UPGRADE_FLAG_VALID   0xA5A5A5A5UL           // 表示存在有效的待升级固件
#define UPGRADE_FLAG_DONE    0x55AA55AAUL           // 表示固件已成功搬运至应用程序区

/**
 * @brief 固件升级元数据结构体定义
 */
typedef struct {
    uint32_t flag;         /*!< 升级标志位 */
    uint32_t image_size;   /*!< 待升级固件大小（单位：字节）*/
    uint32_t image_crc;    /*!< 固件校验值（CRC32） */
    uint32_t version;      /*!< 固件版本号 */
    uint32_t reserved[4];  /*!< 预留字段，可用于扩展功能 */
} BootMeta_t;

/**
 * @brief 读取当前Flash中的元数据
 * @param[out] meta 输出参数，指向用于保存读取结果的结构体
 */
void FlashCV_ReadMeta(BootMeta_t *meta);

/**
 * @brief 写入新的元数据到Flash指定位置（Sector1）
 * @param[in] meta 指向待写入的元数据结构体
 * @return HAL_StatusTypeDef 返回操作状态
 */
HAL_StatusTypeDef FlashCV_WriteMeta(const BootMeta_t *meta);

/**
 * @brief 清除升级标志位，将flag从VALID改为DONE
 * @return HAL_StatusTypeDef 返回操作状态
 */
HAL_StatusTypeDef FlashCV_ClearMetaFlag(void);

/**
 * @brief 擦除应用区域（Sector2~4）
 * @return HAL_StatusTypeDef 返回操作状态
 */
HAL_StatusTypeDef FlashCV_EraseAppArea(void);

/**
 * @brief 将下载区的固件复制到应用程序区
 * @param[in] img_size 待搬运固件的实际大小（字节）
 * @return HAL_StatusTypeDef 返回操作状态
 */
HAL_StatusTypeDef FlashCV_CopyImageToApp(uint32_t img_size);

/**
 * @brief 使用CRC32算法计算一段Flash内存的数据校验值
 * @param[in] start_addr 起始地址
 * @param[in] length 数据长度（字节）
 * @return uint32_t 计算得到的CRC32值
 */
uint32_t FlashCV_CalcCRC(uint32_t start_addr, uint32_t length);

#endif /* __FLASH_CV_H */
