/* comm_proto.h */
#ifndef __COMM_PROTO_H
#define __COMM_PROTO_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief 通信协议帧头定义
     */
#define COMM_HEAD1        0x55  /*!< 帧头第一个字节 */
#define COMM_HEAD2        0xAA  /*!< 帧头第二个字节 */

    /**
     * @brief 通信命令字定义
     */
#define CMD_HANDSHAKE      0x01  /*!< 握手命令 */
#define CMD_START_UPDATE   0x02  /*!< 开始升级命令 */
#define CMD_DATA           0x03  /*!< 数据传输命令 */
#define CMD_END_UPDATE     0x04  /*!< 结束升级命令 */
#define CMD_QUERY_VERSION  0x05  /*!< 查询版本命令 */
#define CMD_ACK            0x06  /*!< 应答命令 */

    /**
     * @brief 通信应答状态码
     */
    typedef enum {
        COMM_STATUS_OK          = 0x00,  /*!< 操作成功 */
        COMM_STATUS_FRAME_CRC   = 0x01,  /*!< 帧CRC校验错误 */
        COMM_STATUS_PARAM_ERR   = 0x02,  /*!< 参数错误 */
        COMM_STATUS_FLASH_ERR   = 0x03,  /*!< Flash操作错误 */
        COMM_STATUS_STATE_ERR   = 0x04   /*!< 状态错误 */
    } CommStatus_t;

    /**
     * @brief 通信协议最大负载长度定义
     */
#define COMM_MAX_PAYLOAD_LEN   1024  /*!< 单帧最大数据负载长度(字节) */

    /**
     * @brief 初始化通信模块
     *
     * 配置UART接收中断，启动接收状态机
     */
    void Comm_Init(void);

    /**
     * @brief 接收字节处理函数
     *
     * 在UART接收中断中调用，用于处理接收到的每个字节
     * @param ch 接收到的字节数据
     */
    void Comm_OnByteReceived(uint8_t ch);

    /**
     * @brief 发送数据帧
     *
     * 构造并发送完整的通信帧，自动计算并附加CRC校验
     * @param cmd 命令字
     * @param seq 序列号
     * @param data 数据指针
     * @param len 数据长度
     */
    void Comm_SendFrame(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t len);

    /**
     * @brief 发送应答帧
     *
     * 发送ACK应答帧，包含状态信息
     * @param cmd 原始命令字
     * @param seq 原始序列号
     * @param status 应答状态码
     */
    void Comm_SendAck(uint8_t cmd, uint8_t seq, CommStatus_t status);

#ifdef __cplusplus
}
#endif

#endif /* __COMM_PROTO_H */
