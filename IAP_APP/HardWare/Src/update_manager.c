
/* update_manager.c */
#include "update_manager.h"
#include <string.h>

/**
 * @brief 升级上下文全局实例
 */
static UpdateContext_t g_ctx;

/**
 * @brief 升级处理状态机枚举
 */
typedef enum {
    UPROC_IDLE = 0,        /*!< 空闲状态 */
    UPROC_VERIFYING,       /*!< 正在校验CRC */
    UPROC_WRITE_META,      /*!< 正在写入元数据 */
    UPROC_DONE             /*!< 处理完成 */
} UpdateProcState_t;

static volatile uint8_t        g_finish_request = 0;  /*!< 完成请求标志 */
static volatile UpdateProcState_t g_proc_state  = UPROC_IDLE;  /*!< 处理状态 */
static uint32_t                g_crc_calc       = 0;  /*!< 计算得到的CRC值 */

/**
 * @brief 内部函数：擦除下载区 (Sector5/6)
 * @return HAL_StatusTypeDef 操作状态
 */
static HAL_StatusTypeDef Update_EraseDownloadArea(void)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0;

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector       = FLASH_SECTOR_5;     // 从 Sector5 开始
    erase.NbSectors    = 2;                  // Sector5、6

    HAL_FLASH_Unlock();
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &sector_error);
    HAL_FLASH_Lock();

    if (status != HAL_OK) return status;
    if (sector_error != 0xFFFFFFFFU) return HAL_ERROR;
    return HAL_OK;
}

void Update_Init(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.state = UPDATE_IDLE;
    g_finish_request = 0;
    g_proc_state     = UPROC_IDLE;
}

UpdateState_t Update_GetState(void)
{
    return g_ctx.state;
}

HAL_StatusTypeDef Update_Start(uint32_t total_size, uint32_t crc, uint32_t version)
{
    // 参数检查
    if (total_size == 0U) {
        return HAL_ERROR;
    }

    /* 不能越界下载区 */
    if ((FLASH_DOWNLOAD_START_ADDR + total_size) > (FLASH_DOWNLOAD_END_ADDR + 1U)) {
        return HAL_ERROR;
    }

    g_ctx.total_size    = total_size;
    g_ctx.image_crc     = crc;
    g_ctx.version       = version;
    g_ctx.received_size = 0U;
    g_ctx.state         = UPDATE_RECEIVING;

    /* 擦除下载区 */
    HAL_StatusTypeDef st = Update_EraseDownloadArea();
    if (st != HAL_OK) {
        g_ctx.state = UPDATE_IDLE;
        return st;
    }

    return HAL_OK;
}

HAL_StatusTypeDef Update_ReceiveChunk(uint32_t offset, const uint8_t *data, uint16_t len)
{
    if (g_ctx.state != UPDATE_RECEIVING) return HAL_ERROR;
    if (data == NULL || len == 0U)       return HAL_ERROR;

    // 越界检查
    if ((offset + len) > g_ctx.total_size) return HAL_ERROR;

    uint32_t addr = FLASH_DOWNLOAD_START_ADDR + offset;
    uint32_t i    = 0U;

    HAL_StatusTypeDef status;

    HAL_FLASH_Unlock();

    while (i < len) {
        uint8_t buf[4] = {0xFF,0xFF,0xFF,0xFF};
        uint32_t remain = len - i;
        uint32_t copy   = (remain >= 4U) ? 4U : remain;

        memcpy(buf, &data[i], copy);
        uint32_t word = *(uint32_t *)buf;

        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word);
        if (status != HAL_OK) {
            HAL_FLASH_Lock();
            return status;
        }

        addr += 4U;
        i    += copy;
    }

    HAL_FLASH_Lock();

    uint32_t new_end = offset + len;
    if (g_ctx.received_size < new_end) {
        g_ctx.received_size = new_end;
    }

    return HAL_OK;
}

HAL_StatusTypeDef Update_RequestFinish(void)
{
    if (g_ctx.state != UPDATE_RECEIVING) {
        return HAL_ERROR;
    }
    if (g_ctx.received_size != g_ctx.total_size) {
        return HAL_ERROR;
    }

    g_finish_request = 1U;
    g_proc_state     = UPROC_VERIFYING;
    g_ctx.state      = UPDATE_FINISH_REQUESTED;
    return HAL_OK;
}

void Update_ProcessInIdle(void)
{
    if (!g_finish_request) return;

    switch (g_proc_state)
    {
    case UPROC_VERIFYING:
        /* 整体 CRC 校验：对下载区做一次 CRC32 */
        g_crc_calc = FlashCV_CalcCRC(FLASH_DOWNLOAD_START_ADDR, g_ctx.total_size);
        if (g_crc_calc == g_ctx.image_crc) {
            g_proc_state = UPROC_WRITE_META;
        } else {
            /* CRC 错误，升级失败 */
            g_finish_request = 0U;
            g_proc_state     = UPROC_IDLE;
            g_ctx.state      = UPDATE_IDLE;
            /* 这里你可以增加一个错误标志，后续任务里通知上位机 */
        }
        break;

    case UPROC_WRITE_META:
    {
        BootMeta_t meta;
        memset(&meta, 0, sizeof(meta));
        meta.flag       = UPGRADE_FLAG_VALID;
        meta.image_size = g_ctx.total_size;
        meta.image_crc  = g_ctx.image_crc;
        meta.version    = g_ctx.version;

        if (FlashCV_WriteMeta(&meta) == HAL_OK) {
            g_proc_state = UPROC_DONE;
        } else {
            g_finish_request = 0U;
            g_proc_state     = UPROC_IDLE;
            g_ctx.state      = UPDATE_IDLE;
        }
    }
        break;

    case UPROC_DONE:
        g_ctx.state      = UPDATE_FINISHED;
        g_finish_request = 0U;
        /* 写完 Meta，交给 Bootloader 去搬运，直接软复位 */
        NVIC_SystemReset();
        while (1) { }
        break;

    default:
        g_finish_request = 0U;
        g_proc_state     = UPROC_IDLE;
        g_ctx.state      = UPDATE_IDLE;
        break;
    }
}