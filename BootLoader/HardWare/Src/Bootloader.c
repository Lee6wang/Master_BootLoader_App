#include "Bootloader.h"
#include "gpio.h"

/********* 内部函数声明 *********/
/**
 * @brief   检查是否存在有效的升级镜像，并完成搬运及验证过程
 * @details
 *          - 从Flash中读取升级元数据（BootMeta_t）；
 *          - 判断标志位是否表示有效升级请求；
 *          - 校验镜像大小和CRC值是否合法；
 *          - 将下载区域的数据复制到应用程序区域；
 *          - 最终确认拷贝结果并清除升级标志
 * @note
 *          - 所有非法情况均提前返回，不触发升级动作；
 *          - 只有完全验证无误后才会清除升级标记
 */
static void Bootloader_CheckAndUpgrade(void);
/**
 * @brief   跳转到已部署的应用程序
 * @details
 *          - 获取应用程序起始地址处的栈指针和复位处理函数地址；
 *          - 进行基础栈地址范围判断以确认有效性；
 *          - 关闭中断、去初始化部分外设；
 *          - 设置MSP为主程序栈顶地址；
 *          - 更新中断向量表偏移地址；
 *          - 最终通过函数指针跳转到用户程序入口
 * @note
 *          - 必须确保目标应用已被正确烧录且头部信息完整；
 *          - 失败情况下会进入死循环并通过LED快速闪烁提示错误
 */
static void Bootloader_JumpToApp(void);


void Bootloader_Run(void)
{

    Bootloader_CheckAndUpgrade();
    Bootloader_JumpToApp();

    // 如果能正常跳转，这里不会执行到
    while (1)
    {
        // 错误处理：比如闪灯、等待调试
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        HAL_Delay(500);
    }
}

static void Bootloader_CheckAndUpgrade(void)
{
    BootMeta_t meta;
    FlashCV_ReadMeta(&meta);

    // 没有有效升级，直接 return
    if (meta.flag != UPGRADE_FLAG_VALID)
        return;

    // 基本合法性检查
    if (meta.image_size == 0 ||
        (FLASH_DOWNLOAD_START_ADDR + meta.image_size) > (FLASH_DOWNLOAD_END_ADDR + 1) ||
        (FLASH_APP_START_ADDR + meta.image_size) > (FLASH_APP_END_ADDR + 1))
    {
        // 元数据不合法，忽略这次升级
        return;
    }

    // 先对下载区做一次校验
    uint32_t crc_calc = FlashCV_CalcCRC(FLASH_DOWNLOAD_START_ADDR, meta.image_size);
    if (crc_calc != meta.image_crc)
    {
        // CRC 不匹配，视为下载失败
        return;
    }

    // 搬运固件到App区
    if (FlashCV_CopyImageToApp(meta.image_size) != HAL_OK)
    {
        // 搬运失败，保留旧App
        return;
    }

    // 再对App区做一次CRC校验
    crc_calc = FlashCV_CalcCRC(FLASH_APP_START_ADDR, meta.image_size);
    if (crc_calc != meta.image_crc)
    {
        // 拷贝后验证失败，同样不清除标志，方便上位机重新下发
        return;
    }

    // 一切正常，清除升级标志，避免下次再升级
    FlashCV_ClearMetaFlag();
}

/**
 * @brief   定义一个函数指针类型，用于跳转到应用程序复位处理函数
 */
typedef void (*pFunction)(void);

static void Bootloader_JumpToApp(void)
{
    uint32_t appStack = *(uint32_t *)FLASH_APP_START_ADDR;
    uint32_t appResetHandler = *(uint32_t *)(FLASH_APP_START_ADDR + 4);

    // 简单检查栈顶地址是否在 SRAM 范围
    if (appStack < 0x20000000 || appStack > 0x20020000)
    {
        // 说明应用不存在或未正确烧录
        while (1)
        {
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            HAL_Delay(100);
        }
    }

    __disable_irq();

    HAL_RCC_DeInit();
    HAL_GPIO_DeInit(LED_GPIO_Port,LED_Pin);

    // 重定位中断向量表
    SCB->VTOR = FLASH_APP_START_ADDR;

    // 设置 MSP 为应用程序的栈顶
    __set_MSP(appStack);

    // 跳转到应用复位向量
    pFunction Jump = (pFunction)appResetHandler;
    Jump();
}
