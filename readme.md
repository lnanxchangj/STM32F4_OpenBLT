# OpenBLT Bootloader 技术文档

## 项目概述

基于 OpenBLT 开源 Bootloader 框架，为 STM32F407 平台实现多通道固件在线升级（OTA）系统。支持有线串口、CAN 总线、蓝牙 BLE 三种升级通道，搭配自研 Python 上位机工具实现完整的固件升级链路。

**目标芯片：** STM32F407VGT6 (Cortex-M4, 1MB Flash, 192KB RAM)

**OpenBLT 版本：** 基于 Feaser OpenBLT（GPLv3 许可）

---

## 1. Flash 存储器布局

### 1.1 STM32F407 Flash 扇区分布

STM32F407 的 1MB Flash 分为 12 个扇区，前 4 个扇区为 16KB，第 5 个为 64KB，后续均为 128KB。Bootloader 和 APP 固件分别占据不同的扇区区域，互不干扰。

| 扇区 | 起始地址 | 大小 | 用途 |
|------|----------|------|------|
| Sector 0 | 0x08000000 | 16 KB | Bootloader |
| Sector 1 | 0x08004000 | 16 KB | Bootloader |
| Sector 2 | 0x08008000 | 16 KB | Bootloader/设备信息 |
| Sector 3 | 0x0800C000 | 16 KB | Bootloader/设备信息 |
| Sector 4 | 0x08010000 | 64 KB | Bootloader/设备信息/升级标志位 |
| Sector 5 | 0x08020000 | 128 KB | **APP 起始 (向量表)** |
| Sector 6 | 0x08040000 | 128 KB | APP |
| Sector 7 | 0x08060000 | 128 KB | APP |
| Sector 8 | 0x08080000 | 128 KB | APP |
| Sector 9 | 0x080A0000 | 128 KB | APP |
| Sector 10 | 0x080C0000 | 128 KB | APP |
| Sector 11 | 0x080E0000 | 128 KB | APP |

### 1.2 地址空间划分

```
0x08000000 ┌──────────────────────────┐
           │      Bootloader          │
           │      (32KB, Sector 0-1)  │
           │                          │
           │  不可被 OTA 覆盖         │
           │  SWD/JTAG 烧写           │
           ├──────────────────────────┤
           │   Bootloader 扩展区域    │
           │   Sector 2-4 (96KB)      │
           │   + 设备信息 (0x0801FF00)│
           │   + 升级标志位 (RTC_BKP) │
0x0801FF00 │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─│ ← 设备信息区
           │ 0x0801FF00: Board Type  │
           │ 0x0801FF04: HW Version  │
           │ 0x0801FF08: BL Version   │
           │ 0x0801FF0C: Serial#     │
           │ 0x0801FF1C: Device Name │
0x08020000 ├──────────────────────────┤ ← APP_BASE_ADDRESS (Sector 5)
           │      APP 向量表          │
           │  0x08020000: 初始 SP     │
           │  0x08020004: Reset_Handler│
           │  0x08020008: NMI_Handler │
           │  ...                     │
           │  0x08020184: 最后一个 IRQ │
           │  0x08020188: ★ Checksum  │ ← VECTOR_TABLE_CS_OFFSET
           ├─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┤
           │      APP 代码 (.text)    │
           │      APP 常量 (.rodata)  │
           │      APP 初始值 (.data)  │
           │                          │
0x080FFFFF └──────────────────────────┘
```

### 1.3 设备信息存储区

设备信息存储在 Sector 4 末尾 (0x0801FF00)，可通过 XCP UPLOAD 命令直接读取：

| 地址 | 内容 | 大小 | 说明 |
|------|------|------|------|
| 0x0801FF00 | Board Type ID | 4 bytes | 板子型号 (见 BoardType 枚举) |
| 0x0801FF04 | Hardware Version | 4 bytes | 硬件版本 (如 0x0102 = v1.2) |
| 0x0801FF08 | Bootloader Version | 4 bytes | Bootloader版本 (如 0x0100 = v1.0) |
| 0x0801FF0C | Serial Number | 16 bytes | 序列号 |
| 0x0801FF1C | Device Name | 32 bytes | 设备名称 (ASCII) |

**读取方式：** 使用 XCP SET_MTA 命令设置地址为 0x0801FF00，再使用 UPLOAD 命令读取数据。

**板型枚举定义：**
```c
typedef enum {
    BOARD_TYPE_UNKNOWN = 0x00,
    BOARD_TYPE_CTRL    = 0x01,  /* 控制板 */
    BOARD_TYPE_DRIVER  = 0x02,  /* 驱动板 */
    BOARD_TYPE_SENSOR  = 0x03,  /* 传感器板 */
} BoardType;
```

### 1.3 向量表结构

ARM Cortex-M4 的向量表是一个地址数组，存放在 Flash 起始位置。CPU 上电或中断触发时，硬件自动从向量表中读取对应处理函数的地址并跳转执行。

STM32F407 有 16 个系统异常 + 82 个外设中断 = 98 个向量，每个 4 字节，共 392 字节（0x188）。

```
偏移    内容                     说明
────────────────────────────────────────
0x000   初始栈顶指针 (MSP)       系统启动后的栈顶地址
0x004   Reset_Handler            复位入口 (程序起点)
0x008   NMI_Handler              不可屏蔽中断
0x00C   HardFault_Handler        硬件错误
0x010   MemManage_Handler        内存管理错误
0x014   BusFault_Handler         总线错误
0x018   UsageFault_Handler       用法错误
0x01C   保留 (4 × 4 字节)
0x02C   SVC_Handler              系统调用
0x030   DebugMon_Handler         调试监控
0x034   保留
0x038   PendSV_Handler           可挂起系统调用
0x03C   SysTick_Handler          系统滴答定时器
0x040   WWDG_IRQHandler          窗口看门狗
...     (外设中断逐一排列)
0x184   最后一个外设中断
0x188   ★ OpenBLT Checksum       校验和存放位置
```

每个 APP 版本编译后，函数地址不同，向量表内容也随之变化。这是 OpenBLT 选择校验向量表来判断固件完整性的基础。

---

## 2. Bootloader 工作原理

### 2.1 启动流程

```
┌─────────────┐
│   上电/复位   │
└──────┬──────┘
       ▼
┌─────────────┐
│  Bootloader  │
│  BootInit()  │ ← 初始化时钟、外设、通信接口、RTC
└──────┬──────┘
       ▼
┌─────────────────────────┐
│  BackDoorInitHook()      │ ← 使能 RTC 时钟
└──────┬──────────────────┘
       ▼
┌─────────────────────────┐
│  BackDoorEntryHook()     │
│                           │
│  if (RTC_BKP0R ==        │
│      0xDEADBEEF) {       │ ← 升级标志位检查
│    清除标志位             │
│    stay in BL             │ ← 停留 Bootloader 等待升级
│  } else {                 │
│    if (APP checksum OK)   │
│      jump APP (0延迟)     │ ← 无感跳转
│    else                   │
│      stay in BL           │
│  }
└──────┬──────────────────┘
       │ (有升级请求时)
       ▼
┌─────────────────────────┐
│   主循环                 │
│   while(1) {            │
│     ComTask();           │ ← 监听通信接口 (串口/CAN)
│     BackDoor();          │ ← 检查 Backdoor 超时
│   }                      │
└──────┬──────────────────┘
       ▼ (Backdoor 超时 且 未建立连接)
┌─────────────────────────┐
│  CpuStartUserProgram()   │
│    NvmVerifyChecksum()   │
│    → 设置 VTOR → 跳转   │
└─────────────────────────┘
```

### 2.2 无感跳转机制 (RTC 标志位方式)

推荐使用 RTC Backup Register 实现无感跳转：

**原理：**
- RTC Backup Register 在软复位后保持数据，掉电后丢失
- Bootloader 启动时检查 RTC_BKP0R 的值

**跳转逻辑：**

| 场景 | RTC_BKP0R | 行为 |
|------|-----------|------|
| 正常上电/掉电重启 | 0 | 验证 APP checksum，有效则立即跳转 (0延迟) |
| APP 请求升级 | 0xDEADBEEF | 清除标志，停留 Bootloader 等待升级 |
| 升级中断断电 | 0 (丢失) | APP checksum 无效，停留在 Bootloader |

**APP 端请求升级代码示例：**
```c
// 在 APP 中调用此函数进入升级模式
void RequestEnterUpgradeMode(void) {
    // 写升级标志到 RTC Backup Register
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0xDEADBEEF);
    // 触发软复位
    NVIC_SystemReset();
}
```

**优点：**
- 正常上电零延迟跳转到 APP
- APP 可主动请求进入升级模式
- 升级中断断电自动安全恢复
- RTC Backup Register 软复位不丢失，掉电丢失，完美匹配需求

### 2.3 APP 跳转过程

跳转是函数调用方式实现的，不是软复位（不调用 `NVIC_SystemReset`）：

```c
void CpuStartUserProgram(void)
{
    /* 1. 校验 APP checksum */
    if (NvmVerifyChecksum() == BLT_FALSE)
        return;     // 校验失败, 不跳转

    /* 2. 释放通信接口 */
    ComFree();
    HAL_DeInit();
    TimerReset();

    /* 3. 重定位向量表到 APP 区域 */
    SCB->VTOR = NvmGetUserProgBaseAddress() & 0x1FFFFF80;  // 0x08020000

    /* 4. 读取 APP 的 Reset_Handler 地址 */
    pProgResetHandler = (void(*)(void))(*((uint32_t *)(NvmGetUserProgBaseAddress() + 0x04)));

    /* 5. 使能中断 */
    CpuIrqEnable();

    /* 6. 跳转到 APP (不会返回) */
    pProgResetHandler();
}
```

### 2.4 Checksum 校验机制

OpenBLT 使用 32 位累加和的补码作为校验算法。

**写入时机：** PC 发送 `PROGRAM(len=0)` 命令（编程结束标志）时，Bootloader 调用 `FlashWriteChecksum()` 计算并写入。

**计算方法：**

```
1. 从 0x08020000 开始, 每 4 字节读一个 uint32_t
2. 累加到 0x08020184 (共 0x188 / 4 = 98 个字)
3. 结果取补码: checksum = ~sum + 1
4. 将 checksum 写入 0x08020188
```

**验证方法：**

```
1. 从 0x08020000 累加到 0x0802018B (包含 checksum 本身)
2. 因为 X + (~X + 1) = 0 (补码性质)
3. 累加结果为 0 → 校验通过
4. 累加结果非 0 → 数据被篡改或写入不完整
```

**断电保护原理：** 升级过程中先擦除 APP 区域（全部变为 0xFF），再写入数据。如果中途断电，checksum 尚未写入（仍为 0xFF），下次上电校验必然失败，Bootloader 不会跳转到损坏的 APP，而是继续等待新的升级。

---

## 3. XCP 协议

### 3.1 协议概述

XCP（Universal Measurement and Calibration Protocol）是汽车行业标准协议，OpenBLT 使用其中的编程子集实现固件传输。协议基于一问一答的主从模式，PC（主机）发命令，MCU（从机）回应。

### 3.2 传输层封装

不同物理接口的封装方式不同：

**串口（RS232）：** XCP 数据前加 1 字节长度

```
[Length(1B)] [XCP_Data(NB)]
示例: 02 FF 00    → Length=2, XCP_Data={0xFF, 0x00}
```

**CAN 总线：** XCP 数据直接放入 CAN 帧 data 字段，DLC 就是长度

```
CAN_ID=0x667  DLC=2  Data={0xFF, 0x00}
```

### 3.3 升级流程与命令详解

完整的固件升级由以下 XCP 命令序列完成：

**Step 1: CONNECT (0xFF) — 建立连接**

```
PC → MCU:  [0xFF] [0x00]
            │      └─ 连接模式 (0=正常)
            └──────── 命令码 CONNECT

MCU → PC:  [0xFF] [Resource] [CommMode] [MaxCTO] [MaxDTO_H] [MaxDTO_L] [ProtoVer] [TransVer]
            │      │          │           │        │                      │           └─ 传输层版本
            │      │          │           │        └──────────────────────└─ 协议版本
            │      │          │           └─ 每包最大字节数 (通常=8)
            │      │          └─ bit0: 0=小端序, 1=大端序
            │      └─ 支持的资源 (PGM/CAL等)
            └──────── PID=0xFF 表示 OK
```

**Step 2: PROGRAM_START (0xD2) — 进入编程模式**

```
PC → MCU:  [0xD2]
MCU → PC:  [0xFF] [0x00] [0x00] [PGM_MaxCTO] [0x00] [0x00] [0x00]
                                  └─ 编程模式下每包最大字节数
```

**Step 3: SET_MTA (0xF6) — 设置内存地址**

```
PC → MCU:  [0xF6] [0x00] [0x00] [0x00] [Addr_0] [Addr_1] [Addr_2] [Addr_3]
                                         └───────── 目标地址 (小端序) ─────────┘
MCU → PC:  [0xFF]
```

**Step 4: PROGRAM_CLEAR (0xD1) — 擦除 Flash**

```
PC → MCU:  [0xD1] [0x00] [0x00] [0x00] [Size_0] [Size_1] [Size_2] [Size_3]
                                         └───────── 擦除大小 (小端序) ─────────┘
MCU → PC:  [0xFF]    ← 擦除完成 (可能耗时数百毫秒)
```

**Step 5: PROGRAM (0xD0) — 写入数据（循环发送）**

```
PC → MCU:  [0xD0] [DataLen] [Data_0] [Data_1] ... [Data_N]
            │      │         └────────── 最多 MaxCTO-2 字节 ──────┘
            │      └─ 本次写入的数据长度
            └──────── 命令码 PROGRAM
MCU → PC:  [0xFF]    ← 写入成功, MTA 自动后移 DataLen 字节
```

**Step 6: PROGRAM (0xD0, len=0) — 编程结束**

```
PC → MCU:  [0xD0] [0x00]    ← DataLen=0, 表示编程结束
MCU → PC:  [0xFF]            ← Bootloader 计算并写入 Checksum
```

此命令触发 `NvmDone()` → `FlashWriteChecksum()`，将校验和写入 0x08020188。

**Step 7: PROGRAM_RESET (0xCF) — 跳转 APP**

```
PC → MCU:  [0xCF]
MCU → PC:  (两种情况)
    Checksum 校验通过 → 直接跳转 APP, ★不回复★ (PC 等待超时)
    Checksum 校验失败 → 回复 [0xFF], 留在 Bootloader
```

这是整个协议中唯一一个"无回复 = 成功"的命令。

### 3.4 错误响应

所有命令在出错时返回统一格式：

```
MCU → PC:  [0xFE] [ErrorCode]
            │      └─ 错误码
            └──────── PID=0xFE 表示错误
```

常用错误码：

| 代码 | 名称 | 含义 |
|------|------|------|
| 0x10 | CMD_BUSY | 上一条命令尚未处理完 |
| 0x20 | CMD_UNKNOWN | 不支持的命令 |
| 0x22 | OUT_OF_RANGE | 参数越界 |
| 0x25 | ACCESS_LOCKED | 资源已锁定（需 Seed/Key 解锁） |
| 0x31 | GENERIC | 通用错误（Flash 写入失败等） |

---

## 4. 通信接口配置

### 4.1 串口 (UART4)

| 参数 | 值 | 说明 |
|------|-----|------|
| 外设 | UART4 | 通过 `BOOT_COM_RS232_CHANNEL_INDEX` 选择 |
| TX 引脚 | PC10 | AF8_UART4 |
| RX 引脚 | PC11 | AF8_UART4 |
| 波特率 | 115200 | `BOOT_COM_RS232_BAUDRATE` |
| 数据格式 | 8N1 | 8 数据位, 无校验, 1 停止位 |

```c
/* blt_conf.h */
#define BOOT_COM_RS232_ENABLE            (1)
#define BOOT_COM_RS232_BAUDRATE          (115200)
#define BOOT_COM_RS232_CHANNEL_INDEX     (3)       /* 0=USART1, 1=USART2, 2=USART3, 3=UART4 */
```

### 4.2 CAN 总线 (CAN1)

| 参数 | 值 | 说明 |
|------|-----|------|
| 外设 | CAN1 | `BOOT_COM_CAN_CHANNEL_INDEX` = 0 |
| TX 引脚 | PB9 | AF9_CAN1 |
| RX 引脚 | PI9 | AF9_CAN1 |
| 波特率 | 500 kbps | Prescaler=6, BS1=11TQ, BS2=2TQ, APB1=42MHz |
| PC→MCU | 0x667 | `BOOT_COM_CAN_RX_MSG_ID` |
| MCU→PC | 0x7E1 | `BOOT_COM_CAN_TX_MSG_ID` |
| 帧类型 | 标准帧 (11-bit) | |

```c
/* blt_conf.h */
#define BOOT_COM_CAN_ENABLE              (1)
#define BOOT_COM_CAN_BAUDRATE            (500000)
#define BOOT_COM_CAN_TX_MSG_ID           (0x7E1u)  /* MCU → PC */
#define BOOT_COM_CAN_RX_MSG_ID           (0x667u)  /* PC → MCU */
#define BOOT_COM_CAN_CHANNEL_INDEX       (0)        /* 0=CAN1, 1=CAN2 */
```

注意：CAN ID 的方向是以 Bootloader 视角命名的。在 PC 端工具中 TX/RX 方向相反——PC 的 TX_ID = Bootloader 的 RX_MSG_ID。

### 4.3 蓝牙 BLE (通过 UART4 透传)

BLE 模块（nanchang_ble）连接在 UART4 上，与有线串口复用同一物理接口。通过 `BLE_MODULE_ENABLE` 宏控制是否启用 BLE 握手状态机。

| 参数 | 值 |
|------|-----|
| BLE 模块 | nanchang_ble |
| 透传模式 | GATT Notify + Write |
| 握手流程 | MCU 收到 "BLE_CONNECT_SUCCESS" → 发送 "AT+BLUFISEND=1" → 进入透传 |
| 退出透传 | PC 发送 "+++"（前后各 1 秒静默） |

```c
/* rs232.c 顶部宏定义 */
#define BLE_MODULE_ENABLE    (1)    /* 1=启用 BLE 握手, 0=纯串口模式 */
```

BLE 模式下，Bootloader 端负责 AT 命令握手进入透传模式，之后 PC 端通过 GATT 特征值读写即等同于串口通信。

### 4.4 CAN 总线硬件连接

```
    PCAN-USB 适配器              开发板
   ┌──────────────┐         ┌──────────────┐
   │  CAN_H ──────┼─────────┼── CAN_H      │
   │  CAN_L ──────┼─────────┼── CAN_L      │
   │  GND ────────┼─────────┼── GND        │
   └──────────────┘         └──────────────┘
         │                          │
     [120Ω 终端]              [120Ω 终端]
     电阻 (两端各一个)
```

CAN 总线两端各需要一个 120Ω 终端电阻。部分 PCAN-USB 适配器和开发板可能已内置终端电阻，需根据实际硬件确认。

---

## 5. Keil MDK 工程配置

### 5.1 Bootloader 工程

| 配置项 | 值 |
|--------|-----|
| ROM 起始地址 | 0x08000000 |
| ROM 大小 | 0x8000 (32KB) |
| RAM 起始地址 | 0x20000000 |
| RAM 大小 | 根据芯片型号 |

### 5.2 APP 工程

| 配置项 | 值 |
|--------|-----|
| ROM 起始地址 | **0x08020000** |
| ROM 大小 | 0xE0000 (1024KB - 128KB = 896KB) |
| RAM 起始地址 | 0x20000000 |
| RAM 大小 | 根据芯片型号 |

APP 工程还需要修改向量表偏移：

```c
/* system_stm32f4xx.c */
#define VECT_TAB_OFFSET  0x20000    /* 向量表偏移到 0x08020000 */
```

三者必须一致：Bootloader 的 `flashLayout` 第一个 APP 扇区地址、APP 工程的 ROM 起始地址、`VECT_TAB_OFFSET` 的值。任何一个不匹配都会导致升级后 APP 无法运行。

### 5.3 固件输出配置

在 Keil 的 User 选项卡中配置 After Build 命令，生成 SREC 格式固件文件：

```
fromelf --m32 --output="app_firmware.srec" ".\Objects\app.axf"
```

或在 Output 选项卡勾选 "Create HEX File"。

---

## 6. PC 上位机工具

### 6.1 官方工具

OpenBLT 自带两个 PC 工具：

**BootCommander (命令行)：**

```cmd
rem CAN 升级
BootCommander.exe -s=xcp -t=xcp_can -d=peak_pcanusb -b=500000 -tid=0x667 -rid=0x7e1 firmware.srec

rem 串口升级
BootCommander.exe -s=xcp -t=xcp_rs232 -d=COM3 -b=115200 firmware.srec
```

| 参数 | CAN | 串口 |
|------|-----|------|
| -t | xcp_can | xcp_rs232 |
| -d | peak_pcanusb | COMx |
| -b | CAN 波特率 | UART 波特率 |
| -tid | PC 发送 CAN ID | 不需要 |
| -rid | PC 接收 CAN ID | 不需要 |

**MicroBoot (GUI)：** 图形化升级工具，基于 Delphi/Lazarus 开发。

### 6.2 自研 Python 上位机

基于 Python + PyQt5 开发的多通道升级工具，支持有线串口、CAN 总线、BLE 蓝牙三种模式。

**文件结构：**

| 文件 | 功能 | 依赖库 |
|------|------|--------|
| main.py | 程序入口 | PyQt5 |
| main_window.py | GUI 界面与业务逻辑 | PyQt5 |
| xcp_master.py | XCP 协议实现 + 串口传输层 | pyserial |
| can_transport.py | CAN 传输层 | python-can |
| ble_transport.py | BLE 传输层 | bleak |
| firmware_parser.py | SREC/HEX 固件解析 | 无 |

**安装依赖：**

```bash
pip install PyQt5 pyserial python-can bleak
```

**运行：**

```bash
python main.py
```

**架构设计：** 三种传输层（SerialTransport / CanTransport / BleTransport）实现相同的接口（`send_packet` / `receive_packet` / `drain`），XcpMaster 通过统一接口调用，不感知底层物理通道。

---

## 7. 升级操作指南

### 7.1 串口升级

1. 用 USB 转 TTL 连接 PC 的 COM 口到 MCU 的 UART4 (PC10/PC11)
2. 开发板上电（Bootloader 进入 Backdoor 等待）
3. 在 Backdoor 超时前（默认 500ms）执行升级命令或点击上位机「开始升级」

### 7.2 CAN 升级

1. 连接 PCAN-USB 适配器的 CAN_H/CAN_L 到开发板
2. 确认两端终端电阻（120Ω）
3. 安装 PEAK 官方驱动（Windows 需要管理员权限）
4. 开发板上电
5. 在 Backdoor 超时前执行升级

### 7.3 BLE 升级

1. 开发板上电（BLE 模块开始广播）
2. PC 扫描并连接 nanchang_ble 设备
3. BLE 模块发送 "BLE_CONNECT_SUCCESS" → MCU 自动发 AT 命令进入透传
4. PC 通过 GATT 透传通道执行标准 XCP 升级流程
5. 升级完成后 PC 发送 "+++" 退出透传模式

### 7.4 升级速度参考

| 通道 | 实测速度 | 瓶颈 |
|------|----------|------|
| 有线串口 (115200) | ~7 KB/s | XCP 一问一答协议开销 |
| CAN (500kbps) | ~5-8 KB/s | XCP 协议开销 + 8 字节帧限制 |
| BLE 透传 | ~0.9 KB/s | BLE 物理层带宽限制 |

---

## 8. 安全机制

### 8.1 断电保护

升级过程中任意时刻断电，Bootloader 自身不受影响（位于独立的 Flash 扇区，升级过程不会擦写 Bootloader 区域）。重新上电后，Bootloader 检查 APP checksum，因数据不完整校验必然失败，自动留在升级等待模式，可再次执行升级。

### 8.2 刷错固件保护

OpenBLT 本身不校验固件与目标硬件的匹配性。如果将 A 板的固件刷到 B 板上，Bootloader 不会拒绝。写入完成后 checksum 校验通过（因为数据本身是完整的），APP 会被执行但行为异常。

但这不会损坏 Bootloader——异常的 APP 不影响 Bootloader 区域，重新上电仍可再次升级刷入正确的固件。

建议在 APP 固件中加入硬件身份标识（board_id），由上位机在升级前校验。

### 8.3 Seed/Key 保护（可选）

OpenBLT 支持 Seed/Key 认证机制，启用后 PC 必须提供正确的密钥才能执行编程操作。通过 `blt_conf.h` 中 `XCP_SEED_KEY_PROTECTION_EN` 启用。

---

## 9. blt_conf.h 关键配置汇总

```c
/* -------- Backdoor (无感跳转) -------- */
#define BOOT_BACKDOOR_HOOKS_ENABLE       (1)        /* 启用 RTC 标志位跳转 */
#define BOOT_BACKDOOR_ENTRY_TIMEOUT_MS   (10000)    /* 10秒超时 */

/* -------- 串口 (UART4) -------- */
#define BOOT_COM_RS232_ENABLE            (1)
#define BOOT_COM_RS232_BAUDRATE          (115200)
#define BOOT_COM_RS232_CHANNEL_INDEX     (3)       /* UART4 */

/* -------- CAN (CAN1) -------- */
#define BOOT_COM_CAN_ENABLE              (1)
#define BOOT_COM_CAN_BAUDRATE            (500000)
#define BOOT_COM_CAN_TX_MSG_ID           (0x7E1u)  /* MCU→PC */
#define BOOT_COM_CAN_RX_MSG_ID           (0x667u)  /* PC→MCU */
#define BOOT_COM_CAN_CHANNEL_INDEX       (0)        /* CAN1 */

/* -------- Flash -------- */
#define BOOT_FLASH_VECTOR_TABLE_CS_OFFSET (0x188)  /* Checksum 偏移 */
#define BOOT_NVM_SIZE_KB                 (1024)    /* 1MB Flash */

/* -------- XCP -------- */
#define BOOT_XCP_UPLOAD_ENABLE           (1)        /* 启用 UPLOAD 命令读设备信息 */
#define BOOT_COM_RX_MAX_DATA             (8)
#define BOOT_COM_TX_MAX_DATA             (8)
```

## 9.1 设备信息读取方法

使用 XCP 协议读取设备信息（上位机实现）：

```python
# 1. 连接设备
xcp.connect()

# 2. 设置 MTA 到设备信息起始地址
xcp.set_mta(0x0801FF00)

# 3. 上传读取设备信息 (56 bytes total)
data = xcp.upload(56)

# 解析数据
board_type = data[0]      # 0x0801FF00
hw_version = data[1:3]    # 0x0801FF04 (uint16)
bl_version = data[3:5]    # 0x0801FF08 (uint16)
serial = data[5:21]      # 0x0801FF0C (16 bytes)
device_name = data[21:53]# 0x0801FF1C (32 bytes)
```

---

## 10. 故障排查

### 10.1 BootCommander 报"拒绝访问"

Windows 下 PCAN-USB 驱动需要管理员权限。以管理员身份运行 CMD，或检查 BootCommander.exe 文件大小是否为 0（文件损坏）。

### 10.2 升级后 APP 不运行

排查方向（按优先级）：

1. APP 工程的 ROM 起始地址是否为 **0x08020000**
2. `VECT_TAB_OFFSET` 是否设置为 **0x20000**
3. Bootloader 的 `flashLayout` 首个 APP 扇区是否从 **0x08020000** 开始
4. 固件文件是否为 SREC/HEX 格式（不是 bin 或 axf）
5. 使用 ST-LINK 读取 0x08020000 处的内容，确认数据已正确写入

### 10.3 CAN 通信无响应

1. 确认 CAN_H/CAN_L 接线正确（未反接）
2. 确认两端终端电阻（120Ω）已就位
3. 确认波特率一致（PC 端和 blt_conf.h）
4. 确认 CAN ID 方向正确（PC 的 TX = Bootloader 的 RX）
5. 使用 PCAN-View 等工具确认总线上有数据

### 10.4 串口通信无响应

1. 确认 TX/RX 交叉连接（PC 的 TX → MCU 的 RX，反之亦然）
2. 确认波特率一致
3. 确认 `BOOT_COM_RS232_CHANNEL_INDEX` 指向正确的 UART 外设
4. 确认 MspInit 中 GPIO 配置正确

### 10.5 下载固件到错误地址

如果固件 SREC 文件中的地址不在 `flashLayout` 定义的可写区域内，OpenBLT 会拒绝写入并返回 `XCP_ERR_GENERIC` 错误。确保 APP 工程的链接地址与 Bootloader 的 Flash 分区表一致。

---

## 11. 参考资料

| 资源 | 链接 |
|------|------|
| OpenBLT 官网 | https://www.feaser.com/openblt/doku.php |
| OpenBLT 下载 | https://www.feaser.com/openblt/doku.php?id=downloads |
| XCP 协议规范 | ASAM XCP 标准 (www.asam.net) |
| STM32F407 参考手册 | RM0090 (ST 官网) |
| python-can 文档 | https://python-can.readthedocs.io |
| PEAK PCAN 驱动 | https://www.peak-system.com/Downloads.76.0.html |