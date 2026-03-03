/************************************************************************************//**
* \file         blt_conf.h
* \brief        Bootloader configuration header file.
* \details      STM32F407ZGT6 + CAN (500kbps) configuration
****************************************************************************************/
#ifndef BLT_CONF_H
#define BLT_CONF_H

/****************************************************************************************
*   C P U   D R I V E R   C O N F I G U R A T I O N
****************************************************************************************/
/* 外部高速晶振频率 (kHz) — 根据你的硬件修改！ */
/* 8MHz 晶振 = 8000, 25MHz 晶振 = 25000           */
#define BOOT_CPU_XTAL_SPEED_KHZ          (8000)

/* 系统时钟频率 (kHz)，即 CubeMX 配置的 SYSCLK */
#define BOOT_CPU_SYSTEM_SPEED_KHZ        (168000)

/* 启用用户程序启动前回调钩子 */
#define BOOT_CPU_USER_PROGRAM_START_HOOK  (1)

#define BOOT_CPU_BYTE_ORDER_MOTOROLA (0)

#define BOOT_FLASH_VECTORTABLE_OFFSET       (0x08008000U)
/****************************************************************************************
*   C O M M U N I C A T I O N   I N T E R F A C E   C O N F I G U R A T I O N
****************************************************************************************/
/* ── CAN 接口 ── */
#define BOOT_COM_CAN_ENABLE              (0)
#define BOOT_COM_CAN_CHANNEL_INDEX       (0)       /* 0=CAN1, 1=CAN2 */
#define BOOT_COM_CAN_BAUDRATE            (500000)  /* 500 kbps */
#define BOOT_COM_CAN_TX_MSG_ID           (0x667u)  /* 上位机→目标板 (XCP命令) */
#define BOOT_COM_CAN_RX_MSG_ID           (0x7E1u)  /* 目标板→上位机 (XCP响应) */
#define BOOT_COM_CAN_TX_MAX_DATA         (8)
#define BOOT_COM_CAN_RX_MAX_DATA         (8)

#define BOOT_COM_RS232_ENABLE            (1)
/** \brief UART 通道索引
 *  0=USART1, 1=USART2, 2=USART3, 3=UART4, 4=UART5, 5=USART6
 *  ★ 根据你实际使用的串口修改！
 */
#define BOOT_COM_RS232_CHANNEL_INDEX     (0)       /* 0=USART1 */

/** \brief 串口波特率 */
#define BOOT_COM_RS232_BAUDRATE          (115200)

/** \brief 发送/接收缓冲区大小 (XCP on UART 固定为 64+1) */
#define BOOT_COM_RS232_TX_MAX_DATA       (64)
#define BOOT_COM_RS232_RX_MAX_DATA       (64)

/* ── 禁用其它接口 ── */
#define BOOT_COM_USB_ENABLE              (0)
#define BOOT_COM_NET_ENABLE              (0)
#define BOOT_COM_MBRTU_ENABLE            (0)


/****************************************************************************************
*   B A C K D O O R   E N T R Y   C O N F I G U R A T I O N
****************************************************************************************/
/* 上电后等待主机连接的超时时间 (ms)，超时后跳转 App */
#define BOOT_BACKDOOR_ENTRY_TIMEOUT_MS   (500)


/****************************************************************************************
*   N O N - V O L A T I L E   M E M O R Y   D R I V E R
****************************************************************************************/
/* STM32F407ZGT6 Flash 总大小 = 1024 KB */
#define BOOT_NVM_SIZE_KB                 (1024)
#define BOOT_NVM_CHECKSUM_HOOKS_ENABLE   (0)


/****************************************************************************************
*   F L A S H   V E C T O R   T A B L E   C H E C K S U M
****************************************************************************************/
/* 用户 App 向量表偏移 (字节) = Bootloader 大小 = 32KB = 0x8000 */
// #define BOOT_FLASH_VECTOR_TABLE_CS_OFFSET (0x8000)


/****************************************************************************************
*   W A T C H D O G   /   S E C U R I T Y   /   F I L E S Y S
****************************************************************************************/
#define BOOT_COP_ENABLE                  (0)
#define BOOT_COP_WATCHDOG_ENABLE         (0)
#define BOOT_XCP_SEED_KEY_ENABLE         (0)
#define BOOT_FILE_SYS_ENABLE             (0)


/****************************************************************************************
*   D E B U G
****************************************************************************************/
#define BOOT_ASSERT_ENABLE               (1)   /* 开发阶段启用，量产可设0 */


#endif /* BLT_CONF_H */