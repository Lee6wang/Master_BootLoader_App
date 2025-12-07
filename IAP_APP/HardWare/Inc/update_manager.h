/* update_manager.h */
#ifndef __UPDATE_MANAGER_H
#define __UPDATE_MANAGER_H

#include "stm32f4xx_hal.h"
#include "FlashCV.h"   // 里面有 BootMeta_t、UPGRADE_FLAG_xxx、地址宏

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 升级状态枚举
 */
typedef enum {
    UPDATE_IDLE = 0,              /*!< 空闲状态 */
    UPDATE_RECEIVING,             /*!< 正在接收数据 */
    UPDATE_FINISH_REQUESTED,      /*!< 已请求完成升级 */
    UPDATE_FINISHED               /*!< 升级已完成 */
} UpdateState_t;

/**
 * @brief 升级上下文结构体
 */
typedef struct {
    UpdateState_t state;          /*!< 当前升级状态 */
    uint32_t total_size;          /*!< 固件总大小（字节） */
    uint32_t image_crc;           /*!< 固件CRC32校验值 */
    uint32_t version;             /*!< 固件版本号 */
    uint32_t received_size;       /*!< 已接收写入的字节数 */
} UpdateContext_t;

/**
 * @brief 初始化升级管理器上下文
 * @note 在系统启动时调用一次
 */
void Update_Init(void);

/**
 * @brief 开始升级流程
 * @param total_size 固件总大小（字节），必须大于0且不超过下载区容量
 * @param crc 固件的CRC32校验值
 * @param version 固件版本号
 * @return HAL_StatusTypeDef HAL_OK表示成功，其他值表示失败
 * @retval HAL_OK 成功开始升级
 * @retval HAL_ERROR 参数无效或下载区空间不足
 */
HAL_StatusTypeDef Update_Start(uint32_t total_size, uint32_t crc, uint32_t version);

/**
 * @brief 接收并写入升级数据块
 * @param offset 数据在固件中的偏移位置（字节）
 * @param data 指向数据缓冲区的指针
 * @param len 数据长度（字节）
 * @return HAL_StatusTypeDef HAL_OK表示成功，其他值表示失败
 * @retval HAL_OK 数据写入成功
 * @retval HAL_ERROR 状态错误、参数无效或超出范围
 */
HAL_StatusTypeDef Update_ReceiveChunk(uint32_t offset, const uint8_t *data, uint16_t len);

/**
 * @brief 请求完成升级过程
 * @note 此函数仅设置完成标志，实际处理在 update_manager.c#L136-L189 中进行
 * @return HAL_StatusTypeDef HAL_OK表示成功，其他值表示失败
 * @retval HAL_OK 成功请求完成
 * @retval HAL_ERROR 当前状态不允许完成操作
 */
HAL_StatusTypeDef Update_RequestFinish(void);

/**
 * @brief 在空闲任务中处理升级收尾工作
 * @note 应在FreeRTOS的vApplicationIdleHook中周期性调用
 * @note 包含CRC校验、元数据写入和系统复位
 */
void Update_ProcessInIdle(void);

/**
 * @brief 获取当前升级状态
 * @return UpdateState_t 当前的升级状态
 */
UpdateState_t Update_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* __UPDATE_MANAGER_H */
