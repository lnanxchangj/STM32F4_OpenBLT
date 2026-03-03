# OpenBLT Bootloader 工程 — STM32F407

## 概述

基于 OpenBLT 开源 Bootloader 移植到 STM32F407 平台，支持 **USART1（115200）** 和 **CAN1（500Kbps）** 双通道升级。上位机通过 XCP 协议与 Bootloader 通信，完成固件下载。

## Flash 分区

| 区域       | 起始地址   | 大小              | 说明                   |
| ---------- | ---------- | ----------------- | ---------------------- |
| Bootloader | 0x08000000 | 32KB (Sector 0-1) | 固定不可被用户程序覆盖 |
| APP        | 0x08008000 | 剩余空间          | 用户应用程序区         |

> **注意**：F407VET6 = 512KB，F407VGT6/ZGT6 = 1024KB，请根据实际型号调整 `BOOT_NVM_SIZE_KB`。

## Keil 工程配置

**Options → Target**：

- IROM1: Start = `0x08000000`, Size = `0x00008000`（32KB）
- IRAM1: 根据型号配置（F407 通常 128KB）

## 关键配置文件 blt_conf.h

### 从 F429 例程移植到 F407 的必改项

| 配置项                              | F429 原值  | F407 修改值       | 说明                   |
| ----------------------------------- | ---------- | ----------------- | ---------------------- |
| `BOOT_NVM_SIZE_KB`                  | 2048       | **1024**（或512） | 按芯片型号设置         |
| `BOOT_FLASH_VECTOR_TABLE_CS_OFFSET` | 0x1AC      | **0x188**         | F407中断数少于F429     |
| `BOOT_COM_NET_ENABLE`               | 1          | **0**             | F407无以太网需求则关闭 |
| `BOOT_COM_USB_ENABLE`               | 1          | **0**             | 不用USB升级则关闭      |
| `BOOT_COM_RS232_BAUDRATE`           | 57600      | **115200**        | 按需调整               |
| `BOOT_COM_RS232_CHANNEL_INDEX`      | 2 (USART3) | **0 (USART1)**    | 按硬件选择             |

### 通信接口配置

```c
/* 串口 */
#define BOOT_COM_RS232_ENABLE            (1)
#define BOOT_COM_RS232_BAUDRATE          (115200)
#define BOOT_COM_RS232_CHANNEL_INDEX     (0)        /* USART1 */

/* CAN */
#define BOOT_COM_CAN_ENABLE             (1)
#define BOOT_COM_CAN_BAUDRATE           (500000)
#define BOOT_COM_CAN_TX_MSG_ID          (0x7E1)
#define BOOT_COM_CAN_RX_MSG_ID          (0x667)
#define BOOT_COM_CAN_CHANNEL_INDEX      (0)         /* CAN1 */
```

> 两个通道同时使能时，OpenBLT 会同时监听，哪个先收到有效数据就用哪个。

### 其他默认参数说明

`blt_conf.h` 中未出现的参数（如 backdoor 超时时间、APP 起始地址等），在 OpenBLT 内核源码中有默认值，无需手动配置。`blt_conf.h` 的作用是**覆盖**默认值。

## CubeMX 配置要点

### 时钟

- HSE = 8MHz，SYSCLK = 168MHz
- APB1 = 42MHz（CAN 波特率计算依赖此值）
- APB2 = 84MHz（USART1 波特率计算依赖此值）

### 外设

- **USART1**: PB6(TX) / PB7(RX)，在 Advanced Settings 中选择 **LL 驱动**
- **CAN1**: 按硬件原理图配置引脚，保持 **HAL 驱动**

### ⚠️ 重要：USART 必须选 LL 驱动

在 CubeMX → **Project Manager → Advanced Settings** 中，将 USART1 的驱动从 HAL 改为 LL。

原因：OpenBLT 的 `rs232.c` 使用 LL 库操作串口，需要 `USE_FULL_LL_DRIVER` 宏定义。选择 LL 驱动后 CubeMX 会自动添加此宏，每次重新生成工程都不会丢失。

## main.c 初始化顺序

```c
HAL_Init();
SystemClock_Config();
MX_GPIO_Init();
MX_USART1_UART_Init();   /* 提供 USART1 时钟 + GPIO 初始化（LL版） */
                          /* OpenBLT 的 Rs232Init() 会再配置一次寄存器，无害 */
/* 不要调用 MX_CAN1_Init()！ */
/* OpenBLT 内部 CanInit() 会调用 HAL_CAN_Init()，自动触发 HAL_CAN_MspInit() */
BootInit();

while (1)
{
    BootTask();
}
```

**为什么不调用 MX_CAN1_Init()**：OpenBLT 的 `can.c` 内部会调用 `HAL_CAN_Init()`，该函数会自动回调 `HAL_CAN_MspInit()` 完成时钟和 GPIO 初始化。外部再调一次会导致重复初始化冲突。

**为什么要调用 MX_USART1_UART_Init()**：OpenBLT 的 `rs232.c` 使用 `LL_USART_Init()`，该函数**不会**触发任何 MspInit 回调，需要外部预先初始化好时钟和 GPIO。

## hooks.c 关键实现

### 跳转前反初始化（CpuUserProgramStartHook）

**必须**在跳转 APP 前清理外设状态，否则 APP 重新初始化可能失败：

```c
blt_bool CpuUserProgramStartHook(void)
{
  /* 关闭 USART1 */
  LL_USART_Disable(USART1);
  LL_APB2_GRP1_DisableClock(LL_APB2_GRP1_PERIPH_USART1);
  HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6 | GPIO_PIN_7);

  /* 关闭 CAN1 */
  HAL_CAN_Stop(&hcan1);
  HAL_CAN_DeInit(&hcan1);
  HAL_CAN_MspDeInit(&hcan1);

  return BLT_TRUE;
}
```

> 需要在 hooks.c 中 `#include "can.h"` 以访问 CubeMX 生成的 `hcan1` 句柄。

---

## ⚠️ 踩坑记录

### 坑1：BOOT_FLASH_VECTOR_TABLE_CS_OFFSET 必须匹配芯片

F429 的中断数多于 F407，向量表更长。如果沿用 F429 的 `0x1AC`，校验和位置错误，导致：

- 升级后校验和验证失败
- Bootloader 无法跳转到 APP

**F407 正确值为 `0x188`**。

### 坑2：APP 启动文件必须为校验和预留空间

校验和写入位置 = APP起始地址 + `0x188`。F407 的向量表恰好在 `0x188` 处结束，如果不预留空间，**校验和会覆盖 APP 代码的第一个字节**。

**解决**：在 APP 工程的 `startup_stm32f407xx.s` 向量表末尾添加保留字：

```asm
              DCD     FPU_IRQHandler            ; FPU
              DCD     0                         ; Reserved for OpenBLT checksum
__Vectors_End
```

### 坑3：CubeMX 重新生成会丢失 USE_FULL_LL_DRIVER

在 CubeMX 的 Advanced Settings 中将 USART 切换为 LL 驱动即可永久解决。

### 坑4：Keil 直烧 APP 无法跳转

Keil 直烧不会写入 OpenBLT 校验和。**开发调试**阶段可临时启用 `BOOT_NVM_CHECKSUM_HOOKS_ENABLE = 1` 并在 hooks 中返回 `BLT_TRUE` 跳过校验。**正式发布前必须改回 0**。

### 坑5：跳转前未反初始化外设

如果 `CpuUserProgramStartHook()` 不做任何清理就返回 `BLT_TRUE`，APP 重新初始化串口/CAN 时可能进入异常状态（外设已处于使能状态、GPIO 复用冲突等）。

### 坑6：NET_DEFERRED_INIT_ENABLE 残留

关闭 NET 后建议将 `BOOT_COM_NET_DEFERRED_INIT_ENABLE` 也改为 0，避免混淆。

---

## 量产烧录

### 方案一：两步烧录（推荐）

1. 编程器烧录 Bootloader 到 0x08000000
2. 用 BootCommander 命令行工具通过串口/CAN 刷入 APP：

```bash
BootCommander -s=xcp -t=xcp_rs232 -d=COM3 -b=115200 app_firmware.srec
```

### 方案二：合并一次性烧录

1. 用 `SRecChecksumTool` 给 APP 固件写入校验和
2. 用 `srec_cat` 合并 Bootloader + APP
3. 编程器一次性烧录合并文件

### 方案三：开发调试专用

启用 `BOOT_NVM_CHECKSUM_HOOKS_ENABLE = 1`，Keil 分别直烧两个工程。**仅限开发阶段**。

---

## 文件结构

```
Bootloader/
├── Core/
│   ├── Src/
│   │   ├── main.c          # 入口，初始化 + BootInit/BootTask
│   │   └── hooks.c         # Bootloader 回调（deinit、backdoor等）
│   └── Inc/
│       └── blt_conf.h      # Bootloader 配置（通信、Flash、看门狗等）
├── Lib/
│   └── OpenBLT/Source/      # OpenBLT 内核源码（不要修改）
│       ├── ARMCM4_STM32F4/
│       │   ├── rs232.c     # 串口驱动（LL库）
│       │   ├── can.c       # CAN驱动（HAL库）
│       │   ├── cpu.c       # CPU控制 + APP跳转逻辑
│       │   └── flash.c     # Flash擦写 + 校验和
│       └── ...
└── STM32F407.ioc            # CubeMX 工程文件
```