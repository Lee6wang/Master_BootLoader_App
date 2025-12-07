#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "stm32f4xx_hal.h"
#include "FlashCV.h"

/**
 * @brief   Bootloader主流程控制函数
 * @details
 *          - 首先尝试检查是否有新的固件待升级；
 *          - 若存在，则执行升级操作；
 *          - 升级完成后尝试跳转到应用程序；
 *          - 如果跳转成功则不会继续向下执行；
 *          - 否则进入错误状态指示循环（例如LED闪烁）
 * @note    此函数通常由main()调用，是Bootloader逻辑的核心入口点
 */
void Bootloader_Run(void);

#endif /* __BOOTLOADER_H */
