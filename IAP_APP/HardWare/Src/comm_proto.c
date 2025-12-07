
/* comm_proto.c */
#include "../Inc/comm_proto.h"
#include "update_manager.h"
#include "FlashCV.h"
#include <string.h>

/* 使用 USART1 */
extern UART_HandleTypeDef huart1;

/**
 * @brief 接收状态机状态枚举
 */
typedef enum {
    RX_STATE_HEAD1 = 0,    /*!< 等待帧头第一个字节 */
    RX_STATE_HEAD2,        /*!< 等待帧头第二个字节 */
    RX_STATE_CMD,          /*!< 接收命令字 */
    RX_STATE_SEQ,          /*!< 接收序列号 */
    RX_STATE_LEN_L,        /*!< 接收长度低字节 */
    RX_STATE_LEN_H,        /*!< 接收长度高字节 */
    RX_STATE_DATA,         /*!< 接收数据 */
    RX_STATE_CRC0,         /*!< 接收CRC第0字节 */
    RX_STATE_CRC1,         /*!< 接收CRC第1字节 */
    RX_STATE_CRC2,         /*!< 接收CRC第2字节 */
    RX_STATE_CRC3          /*!< 接收CRC第3字节 */
} RxState_t;

static RxState_t  rx_state = RX_STATE_HEAD1;  /*!< 接收状态机当前状态 */
static uint8_t    rx_cmd;                     /*!< 接收到的命令字 */
static uint8_t    rx_seq;                     /*!< 接收到的序列号 */
static uint16_t   rx_len;                     /*!< 接收到的数据长度 */
static uint8_t    rx_buf[COMM_MAX_PAYLOAD_LEN]; /*!< 数据接收缓冲区 */
static uint16_t   rx_index;                   /*!< 数据接收索引 */
static uint8_t    crc_bytes[4];               /*!< CRC字节缓冲区 */
static uint8_t    crc_index;                  /*!< CRC接收索引 */
static uint8_t    s_rx_byte;                  /*!< UART接收字节缓冲 */

/**
 * @brief 计算数据的CRC32校验值
 * 
 * 使用FlashCV模块的CRC计算函数
 * @param data 数据指针
 * @param len 数据长度
 * @return uint32_t CRC32校验值
 */
static uint32_t Comm_CalcCRC32(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return 0;
    }

    return FlashCV_CalcCRC((uint32_t)data, len);
}

/**
 * @brief 处理完整接收的数据包
 * 
 * 在CRC校验通过后调用，根据命令字执行相应操作
 * @param cmd 命令字
 * @param seq 序列号
 * @param data 数据指针
 * @param len 数据长度
 */
static void Comm_HandlePacket(uint8_t cmd, uint8_t seq,
                              const uint8_t *data, uint16_t len);

/**
 * @brief 重置接收状态机
 * 
 * 将接收状态机恢复到初始状态
 */
static void Comm_ResetRxState(void)
{
    rx_state = RX_STATE_HEAD1;
}

void Comm_Init(void)
{
    rx_state = RX_STATE_HEAD1;
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
}

/**
 * @brief UART接收完成回调函数
 * 
 * 在HAL库UART接收中断中调用
 * @param huart UART句柄指针
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        Comm_OnByteReceived(s_rx_byte);
        HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
    }
}

void Comm_OnByteReceived(uint8_t ch)
{
    switch (rx_state) {
    case RX_STATE_HEAD1:
        if (ch == COMM_HEAD1) rx_state = RX_STATE_HEAD2;
        break;

    case RX_STATE_HEAD2:
        if (ch == COMM_HEAD2) rx_state = RX_STATE_CMD;
        else rx_state = RX_STATE_HEAD1;
        break;

    case RX_STATE_CMD:
        rx_cmd = ch;
        rx_state = RX_STATE_SEQ;
        break;

    case RX_STATE_SEQ:
        rx_seq = ch;
        rx_state = RX_STATE_LEN_L;
        break;

    case RX_STATE_LEN_L:
        rx_len = ch;
        rx_state = RX_STATE_LEN_H;
        break;

    case RX_STATE_LEN_H:
        rx_len |= ((uint16_t)ch << 8);
        if (rx_len > COMM_MAX_PAYLOAD_LEN) {
            Comm_ResetRxState();
        } else {
            rx_index = 0;
            rx_state = (rx_len == 0U) ? RX_STATE_CRC0 : RX_STATE_DATA;
        }
        break;

    case RX_STATE_DATA:
        rx_buf[rx_index++] = ch;
        if (rx_index >= rx_len) {
            crc_index = 0;
            rx_state  = RX_STATE_CRC0;
        }
        break;

    case RX_STATE_CRC0:
    case RX_STATE_CRC1:
    case RX_STATE_CRC2:
    case RX_STATE_CRC3:
        crc_bytes[crc_index++] = ch;
        if (crc_index >= 4U) {
            uint32_t crc_recv =  (uint32_t)crc_bytes[0]
                               | ((uint32_t)crc_bytes[1] << 8)
                               | ((uint32_t)crc_bytes[2] << 16)
                               | ((uint32_t)crc_bytes[3] << 24);

            /* 计算从 CMD 到 DATA 的 CRC32 */
            uint8_t tmp[4 + COMM_MAX_PAYLOAD_LEN];
            uint16_t pos = 0;
            tmp[pos++] = rx_cmd;
            tmp[pos++] = rx_seq;
            tmp[pos++] = (uint8_t)(rx_len & 0xFFU);
            tmp[pos++] = (uint8_t)(rx_len >> 8);
            if (rx_len > 0U) {
                memcpy(&tmp[pos], rx_buf, rx_len);
                pos += rx_len;
            }
            uint32_t crc_calc = Comm_CalcCRC32(tmp, pos);

            if (crc_calc == crc_recv) {
                Comm_HandlePacket(rx_cmd, rx_seq, rx_buf, rx_len);
            } else {
                Comm_SendAck(rx_cmd, rx_seq, COMM_STATUS_FRAME_CRC);
            }

            Comm_ResetRxState();
        }
        break;

    default:
        Comm_ResetRxState();
        break;
    }
}

void Comm_SendFrame(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t len)
{
    uint8_t header[6];
    header[0] = COMM_HEAD1;
    header[1] = COMM_HEAD2;
    header[2] = cmd;
    header[3] = seq;
    header[4] = (uint8_t)(len & 0xFFU);
    header[5] = (uint8_t)(len >> 8);

    uint8_t tmp[4 + COMM_MAX_PAYLOAD_LEN];
    uint16_t pos = 0;
    tmp[pos++] = cmd;
    tmp[pos++] = seq;
    tmp[pos++] = header[4];
    tmp[pos++] = header[5];
    if (len > 0U && data != NULL) {
        memcpy(&tmp[pos], data, len);
        pos += len;
    }

    uint32_t crc = Comm_CalcCRC32(tmp, pos);
    uint8_t crc_out[4];
    crc_out[0] = (uint8_t)(crc & 0xFFU);
    crc_out[1] = (uint8_t)((crc >> 8) & 0xFFU);
    crc_out[2] = (uint8_t)((crc >> 16) & 0xFFU);
    crc_out[3] = (uint8_t)((crc >> 24) & 0xFFU);

    HAL_UART_Transmit(&huart1, header, 6, HAL_MAX_DELAY);
    if (len > 0U && data != NULL) {
        HAL_UART_Transmit(&huart1, (uint8_t *)data, len, HAL_MAX_DELAY);
    }
    HAL_UART_Transmit(&huart1, crc_out, 4, HAL_MAX_DELAY);
}

void Comm_SendAck(uint8_t cmd, uint8_t seq, CommStatus_t status)
{
    uint8_t payload[3];
    payload[0] = (uint8_t)status;
    payload[1] = cmd;
    payload[2] = seq;
    Comm_SendFrame(CMD_ACK, 0, payload, sizeof(payload));
}

/**
 * @brief 处理完整接收的数据包
 * 
 * 在CRC校验通过后调用，根据命令字执行相应操作
 * @param cmd 命令字
 * @param seq 序列号
 * @param data 数据指针
 * @param len 数据长度
 */
static void Comm_HandlePacket(uint8_t cmd, uint8_t seq,
                              const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef st;

    switch (cmd)
    {
    case CMD_HANDSHAKE:
    {
        const char reply[] = "STM32F4-APP-BOOT";
        Comm_SendFrame(CMD_HANDSHAKE, seq, (const uint8_t *)reply, sizeof(reply));
    }
        break;

    case CMD_START_UPDATE:
        if (len < 12U) {
            Comm_SendAck(cmd, seq, COMM_STATUS_PARAM_ERR);
        } else {
            uint32_t total_size = *(uint32_t *)&data[0];
            uint32_t crc        = *(uint32_t *)&data[4];
            uint32_t version    = *(uint32_t *)&data[8];

            if (total_size == 0) {
                Comm_SendAck(cmd, seq, COMM_STATUS_PARAM_ERR);
                break;
            }

            st = Update_Start(total_size, crc, version);
            Comm_SendAck(cmd, seq, (st == HAL_OK) ? COMM_STATUS_OK : COMM_STATUS_FLASH_ERR);
        }
        break;

    case CMD_DATA:
        if (len < 4U) {
            Comm_SendAck(cmd, seq, COMM_STATUS_PARAM_ERR);
        } else if (Update_GetState() != UPDATE_RECEIVING) {
            Comm_SendAck(cmd, seq, COMM_STATUS_STATE_ERR);
        } else {
            uint32_t offset = *(uint32_t *)&data[0];
            const uint8_t *payload = &data[4];
            uint16_t plen = (uint16_t)(len - 4U);

            // 验证偏移量和长度的有效性
            if (plen == 0) {
                Comm_SendAck(cmd, seq, COMM_STATUS_PARAM_ERR);
                break;
            }

            st = Update_ReceiveChunk(offset, payload, plen);
            Comm_SendAck(cmd, seq, (st == HAL_OK) ? COMM_STATUS_OK : COMM_STATUS_FLASH_ERR);
        }
        break;

    case CMD_END_UPDATE:
        st = Update_RequestFinish();
        Comm_SendAck(cmd, seq, (st == HAL_OK) ? COMM_STATUS_OK : COMM_STATUS_STATE_ERR);
        /* 真正的 CRC+写Meta+复位由 Idle Hook 中的 Update_ProcessInIdle 完成 */
        break;

    case CMD_QUERY_VERSION:
    {
        BootMeta_t meta;
        FlashCV_ReadMeta(&meta);
        uint32_t now_ver = meta.version;
        Comm_SendFrame(CMD_QUERY_VERSION, seq, (uint8_t *)&now_ver, sizeof(now_ver));
    }
        break;

    default:
        break;
    }
}