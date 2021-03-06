/*************************************************************************
*   版权所有(C) 1987-2020, 深圳华为技术有限公司.
*
*   文 件 名 :  hsuart.h
*
*   作    者 :  wuzechun
*
*   描    述 :  hsuart.c 的头文件
*
*   修改记录 :  2011年6月13日  v1.00  wuzechun  创建
*
*************************************************************************/

#ifndef __HSUART_H__
#define __HSUART_H__

#include "types.h"
#include "OnChipRom.h"

/*--------------------------------------------------------------*
 * 宏定义                                                       *
 *--------------------------------------------------------------*/

/* 高速串口默认波特率115200bps */
#define HSUART_BAUT_RATE_DEFAULT BAUDRATE_115200

#define HS_UART_RECV_TIMEOUT_MS 10000   /* 10s */

#define HSUART_BASE_ADDR  0x900A8000

/* FCR相关 */
/* 设置发送FIFO水线 */
#define UART_DEF_TET_NULL     0x00 /* FIFO 完全空*/
#define UART_DEF_TET_2CHAR    0x10 /* FIFO 中有2 个字符*/
#define UART_DEF_TET_ONEFOUR  0x20 /* FIFO 四分之一满*/
#define UART_DEF_TET_ONETWO   0x30 /* FIFO 二分之一满*/

/* 设置接收FIFO水线 */
#define UART_DEF_RT_1CHAR     0x00 /* FIFO 中有1 个字符*/
#define UART_DEF_RT_2CHAR     0xc0 /* FIFO 差2 个字符就满*/
#define UART_DEF_RT_ONEFOUR   0x40 /* FIFO 四分之一满*/
#define UART_DEF_RT_ONETWO    0x80 /* FIFO 二分之一满*/

/* DMA Mode */
#define UART_DEF_NO_DMA_MODE  0x00   /* Mode 0 */
#define UART_DEF_DMA_MODE     0x08   /* Mode 1 */

/* 复位发送接收FIFO */
#define UART_RESET_TX_FIFO    0x04
#define UART_RESET_RCV_FIFO   0x02

/* 设置FIFO使能 */
#define UART_DEF_FIFOE_ENABLE   0x1 /* FIFO 使能*/
#define UART_DEF_FIFOE_DISABLE  0x0 /* FIFO 使能*/


/* 设置LCR */
/* 停止位设置 */
#define UART_DEF_LCR_STP2   0x04  /*在LCR bit[1:0]=0 的条件下为1.5 个停止位，否则为2 个停止位*/
#define UART_DEF_LCR_STP1   0x00  /*1个停止位*/

#define UART_DEF_LCR_PEN    0x08 /* 奇偶校验使能*/
#define UART_DEF_LCR_EPS    0x10 /* 奇偶校验选择*/

#define UART_DEF_PARITY_NONE 0x00 /* 无奇偶校验 */

#define UART_DEF_LCR_DIVENABLE  0x80  /* 分频寄存器使能 */

/* 设置数据流中的每个字符的个数*/
#define UART_DEF_LCR_CS8    0x03 /* 8bit*/
#define UART_DEF_LCR_CS7    0x02 /* 7bit*/
#define UART_DEF_LCR_CS6    0x01 /* 6bit*/
#define UART_DEF_LCR_CS5    0x00 /* 5bit*/

#define HSUART_MIN_BAUT_RATE  61
#define HSUART_MAX_BAUT_RATE  4000000

/* 瓯?�?�?� */
#if PLATFORM==PLATFORM_V7R2_PORTING
#define HSUART_CLOCK_FREQ   48000000
#else
#define HSUART_CLOCK_FREQ   64000000
#endif


#define HSUUART_DELAY_TIMES 1000


#define HSUART_BUFFER_SIZE   1024

#define HSUART_MAKE_WORD(_h , _l ) ( ((UINT16)(_l) & 0xFF) | (((UINT16)(_h) << 8) & 0xFF00) )

#define HSUART_MAKE_LONG( _c4, _c3, _c2, _c1 )  \
    ( ((UINT32)(_c1) & 0xFF) | (((UINT32)(_c2) << 8) & 0xFF00) | (((UINT32)(_c3) << 16) & 0xFF0000) | (((UINT32)(_c4) << 24) & 0xFF000000) )


/*--------------------------------------------------------------*
 * 数据结构                                                     *
 *--------------------------------------------------------------*/
typedef enum tagHSUART_STATUS
{
    HSUART_STATUS_OK = 0,
    HSUART_STATUS_PARA_ERR,
    HSUART_STATUS_BAUTRATE_ERR,
    HSUART_STATUS_SEND_ERR,
    HSUART_STATUS_BUFFER_FULL_ERR,
    HSUART_RECV_TIMEOUT
}HSUART_STATUS;

typedef enum tagBaudRateE
{
    BAUDRATE_9600   = 9600,
    BAUDRATE_115200 = 115200,
    BAUDRATE_1M     = 1000000,
    BAUDRATE_2M     = 2000000,
    BAUDRATE_3M     = 3000000,
    BAUDRATE_4M     = 4000000
}baudRateE;

typedef struct hsuart_recv_str
{
    UINT32 ulRead;
    UINT32 ulWrite;
    UINT32 ulTotalNum;
    UINT8 ucData[HSUART_BUFFER_SIZE];
}RECV_STR;
/*--------------------------------------------------------------*
 * 函数原型声明                                                 *
 *--------------------------------------------------------------*/

HSUART_STATUS hsUartInit(RECV_STR *pstQue);
HSUART_STATUS hsUartRecv(RECV_STR *pstHsUartRecvData);
HSUART_STATUS hsUartSend(UINT8* pucSrcAddr, UINT32 u32TransLength);
int OutQue(RECV_STR *pstQue, UINT8 *pBuf,UINT32 ulLong);

UINT16 crcChk(UINT8 *buf, UINT32 len, UINT16 crc);
INT32 hsUartRecvData(RECV_STR *pstQue, UINT8 *pBuf, UINT32 ulLong);
INT32 hsUartRecvCrc(RECV_STR *pstQue, UINT8* ucRecvBuf, UINT32 ulLength);
BOOL isHsSptBaud(UINT32 u32BaudRate);
HSUART_STATUS hsUartSetBaud(UINT32 u32BaudRate);

#endif /* hsuart.h */

