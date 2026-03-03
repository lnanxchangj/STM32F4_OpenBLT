# STM32F407ZGT6 OpenBLT Bootloader — CAN OTA 固件升级系统

<p align="center">
  <strong>基于 OpenBLT 的工业级 CAN 总线在线固件升级（OTA）解决方案</strong><br/>
  <em>STM32F407ZGT6 · Keil MDK-ARM AC6 · STM32CubeMX · XCP on CAN</em>
</p>

---

|  项目属性  |  说明  |
|-----------|--------|
| **MCU** | STM32F407ZGT6 (Cortex-M4, 168 MHz, 1 MB Flash, 192 KB SRAM) |
| **Bootloader** | OpenBLT v1.21.x (feaser/openblt) |
| **通信接口** | CAN 2.0B (bxCAN 外设) |
| **协议** | ASAM MCD-1 XCP v1.0 on CAN |
| **工具链** | Keil MDK-ARM v5.x + ARMCLANG (AC6) |
| **HAL 生成** | STM32CubeMX 6.x |
| **上位机** | MicroBoot (GUI) / BootCommander (CLI) |
| **许可证** | GPLv3 (OpenBLT) / 可获取商业许可 |

---

## 目录

1. [项目概览](#1-项目概览)
2. [OpenBLT 深度介绍](#2-openblt-深度介绍)
3. [系统架构](#3-系统架构)
4. [Flash 内存分区规划](#4-flash-内存分区规划)
5. [开发环境搭建](#5-开发环境搭建)
6. [CubeMX 工程配置](#6-cubemx-工程配置)
7. [OpenBLT 移植详解](#7-openblt-移植详解)
8. [用户应用程序适配](#8-用户应用程序适配)
9. [CAN 总线 OTA 升级流程](#9-can-总线-ota-升级流程)
10. [上位机工具使用](#10-上位机工具使用)
11. [生产与部署](#11-生产与部署)
12. [故障排除](#12-故障排除)
13. [参考资源](#13-参考资源)

---

## 1. 项目概览

本项目为 STM32F407ZGT6 微控制器提供一套完整的、可量产部署的 CAN 总线 Bootloader 解决方案。基于业界广泛验证的开源项目 OpenBLT，实现通过 CAN 接口进行固件的远程在线升级（OTA），适用于汽车电子、工业控制、智能传感器等对现场升级有刚性需求的嵌入式产品。

**核心能力：**

- 通过 CAN 2.0B 总线实现固件安全升级，无需拆机、无需调试器
- Bootloader 占用 Flash ≤ 32 KB（两个 16 KB 扇区），对应用程序空间影响极小
- 支持 Backdoor 超时机制，上电后自动检测升级请求或跳转至用户应用
- 基于 XCP 标准协议，兼容 MicroBoot / BootCommander / 自定义上位机
- 支持 Seed/Key 安全认证，可防止未授权固件写入
- 看门狗安全设计，升级过程异常可自动恢复
- 完整支持 S-record (S19/S28/S37) 固件文件格式

---

## 2. OpenBLT 深度介绍

### 2.1 什么是 OpenBLT

OpenBLT（Open source BootLoader Tool）是由荷兰 Feaser 公司开发维护的开源微控制器 Bootloader。自 2011 年首次发布以来，经过十多年持续迭代，已成为 STM32 平台上最受欢迎的第三方 Bootloader 之一。它采用 C 语言编写，通过模块化架构将协议层、传输层与硬件驱动层完全解耦，使得移植到新平台的工作量极小。

### 2.2 核心架构

OpenBLT 的软件架构分为四个主要层次：

```
┌─────────────────────────────────────────────┐
│              App 层（应用配置）                │
│   blt_conf.h  ·  hooks.c  ·  main.c  · led.c │
├─────────────────────────────────────────────┤
│              Core 层（平台无关）               │
│   boot.c  ·  xcp.c  ·  com.c  ·  file.c      │
│   backdoor.c  ·  cop.c  ·  assert.c          │
├─────────────────────────────────────────────┤
│            Target 层（硬件驱动）               │
│   can.c · rs232.c · usb.c · flash.c · timer.c │
│   nvm.c · cpu.c                               │
├─────────────────────────────────────────────┤
│            Comp 层（编译器相关）               │
│   启动文件 · 链接脚本 · 中断向量              │
└─────────────────────────────────────────────┘
```

**各层职责：**

- **App 层** — 开发者直接接触的配置层。`blt_conf.h` 是总配置开关，控制启用哪些通信接口、Flash 大小、Backdoor 超时等。`hooks.c` 提供回调钩子函数，可自定义 LED 指示、按键检测、安全校验等行为。
- **Core 层** — 平台无关的协议引擎。XCP 协议状态机、通信管理器、Flash 编程抽象、Backdoor 入口逻辑等。移植时无需修改此层代码。
- **Target 层** — 硬件外设驱动。每个支持的 MCU 家族有独立目录（如 `ARMCM4_STM32F4/`），包含该芯片的 CAN 驱动、Flash 编程、定时器、CPU 控制等底层实现。
- **Comp 层** — 编译器相关的启动代码与链接脚本。不同工具链（GCC / IAR / Keil）有各自的实现。

### 2.3 通信协议 — XCP on CAN

OpenBLT 使用 ASAM MCD-1 XCP（Universal Measurement and Calibration Protocol）v1.0 协议。XCP 是汽车行业广泛使用的标定协议标准，其主从通信模型天然适合 Bootloader 场景：

- **Master（主机端）**：上位机工具（MicroBoot / BootCommander / LibOpenBLT 自定义工具）
- **Slave（从机端）**：目标板上运行的 OpenBLT Bootloader

XCP on CAN 的通信使用两个 CAN 报文 ID：一个用于主机→从机的命令帧（Command），一个用于从机→主机的响应帧（Response）。默认配置下，命令帧 ID = 0x667，响应帧 ID = 0x7E1。在多节点 CAN 网络中，可为每个节点分配不同的 CAN ID 以实现单独寻址升级。

### 2.4 Backdoor 入口机制

OpenBLT 的 Backdoor 机制是实现"上电自动运行用户程序、需要时才进入 Bootloader"的关键：

1. **MCU 复位** → 执行 Bootloader 代码
2. **Backdoor 窗口打开**（默认约 500 ms）→ 在此窗口期内，如果主机发来 XCP CONNECT 命令，则停留在 Bootloader 模式
3. **窗口超时** → 未收到连接请求，自动跳转到用户应用程序

此机制也可通过 `hooks.c` 中的 `BackDoorEntryHook()` 自定义为 GPIO 按键触发等方式。

### 2.5 安全特性

- **Seed/Key 认证** — 主机连接时需通过挑战-应答认证，可在 `hooks.c` 中实现自定义算法
- **Flash 校验** — 写入完成后通过 CRC32 校验固件完整性
- **内存保护** — Bootloader 所在扇区可配置为写保护
- **看门狗支持** — 升级过程中定期喂狗，避免意外复位导致砖机

### 2.6 上位机工具

- **MicroBoot** — 跨平台 GUI 工具，支持拖放固件文件、选择通信接口、实时进度显示
- **BootCommander** — 命令行工具，适合集成到 CI/CD 流水线或批处理脚本
- **LibOpenBLT** — C 语言共享库，提供完整 API，可基于此开发自定义升级工具（支持 Python、C#、Java 等语言绑定）

---

## 3. 系统架构

### 3.1 项目目录结构

```
STM32F407ZGT6_OpenBLT_CAN/
│
├── Boot/                           # ===== Bootloader 工程 =====
│   ├── Core/
│   │   ├── Inc/                    # CubeMX 生成的头文件
│   │   │   ├── main.h
│   │   │   ├── stm32f4xx_hal_conf.h
│   │   │   └── stm32f4xx_it.h
│   │   └── Src/                    # CubeMX 生成的源文件 + 用户代码
│   │       ├── main.c              # Bootloader 主入口
│   │       ├── stm32f4xx_hal_msp.c
│   │       ├── stm32f4xx_it.c
│   │       └── system_stm32f4xx.c
│   │
│   ├── OpenBLT/                    # OpenBLT 源码（从仓库提取）
│   │   ├── App/                    # 应用配置层
│   │   │   ├── blt_conf.h          # ★ 核心配置文件
│   │   │   ├── hooks.c             # ★ 用户回调钩子
│   │   │   └── led.c / led.h       # LED 状态指示（可选）
│   │   ├── Core/                   # 平台无关核心
│   │   │   ├── boot.c / boot.h
│   │   │   ├── xcp.c / xcp.h
│   │   │   ├── com.c / com.h
│   │   │   ├── backdoor.c / backdoor.h
│   │   │   ├── cop.c / cop.h
│   │   │   └── assert.c / assert.h
│   │   └── Target/                 # STM32F4 硬件驱动
│   │       ├── can.c / can.h       # CAN 外设驱动
│   │       ├── flash.c / flash.h   # Flash 编程驱动
│   │       ├── timer.c / timer.h   # 系统定时器
│   │       ├── cpu.c / cpu.h       # CPU 控制（复位/跳转）
│   │       └── nvm.c / nvm.h       # 非易失存储抽象
│   │
│   ├── Drivers/                    # STM32 HAL 驱动库
│   │   ├── CMSIS/
│   │   └── STM32F4xx_HAL_Driver/
│   │
│   ├── MDK-ARM/                    # Keil MDK 工程文件
│   │   ├── Boot.uvprojx            # Keil 工程
│   │   ├── Boot.uvoptx
│   │   └── startup_stm32f407xx.s   # AC6 启动文件
│   │
│   ├── Boot.ioc                    # CubeMX 工程文件
│   └── STM32F407ZGTx_FLASH.ld      # 链接脚本（Bootloader 用）
│
├── App/                            # ===== 用户应用程序工程 =====
│   ├── Core/
│   │   ├── Inc/
│   │   └── Src/
│   │       └── main.c              # 用户应用主入口
│   ├── Drivers/
│   ├── MDK-ARM/
│   │   └── App.uvprojx
│   ├── App.ioc
│   └── STM32F407ZGTx_FLASH.ld      # 链接脚本（App 用，起始地址偏移）
│
├── Tools/                          # ===== 上位机 & 辅助工具 =====
│   ├── MicroBoot/                  # GUI 升级工具
│   ├── BootCommander/              # CLI 升级工具
│   └── SrecConverter/              # 固件格式转换
│
├── Docs/                           # ===== 技术文档 =====
│   ├── architecture.md             # 架构设计文档
│   ├── porting_guide.md            # 移植指南
│   ├── flash_map.svg               # Flash 分区图
│   └── can_protocol.md             # CAN 协议说明
│
├── Scripts/                        # ===== 构建 & 部署脚本 =====
│   ├── build_all.bat               # 一键编译
│   ├── flash_boot.bat              # 烧录 Bootloader
│   └── ota_update.bat              # OTA 升级脚本
│
├── README.md                       # 本文件
├── CHANGELOG.md
└── LICENSE
```

### 3.2 系统启动流程

```
                    ┌──────────────┐
                    │   上电复位    │
                    └──────┬───────┘
                           │
                           ▼
                ┌─────────────────────┐
                │  Bootloader 初始化   │
                │  时钟/GPIO/CAN/定时器 │
                └──────────┬──────────┘
                           │
                           ▼
                ┌─────────────────────┐
                │  Backdoor 窗口打开   │
                │  等待 XCP CONNECT   │
                │  (默认 500ms)       │
                └──────────┬──────────┘
                           │
              ┌────────────┴────────────┐
              │                         │
         收到 CONNECT              超时/无请求
              │                         │
              ▼                         ▼
    ┌──────────────────┐     ┌──────────────────┐
    │ 进入 Bootloader  │     │ 校验用户程序有效性 │
    │ 固件升级模式     │     │ (检查栈指针)      │
    │                  │     └────────┬─────────┘
    │  ┌─ 擦除 Flash   │              │
    │  ├─ 接收数据     │        ┌─────┴─────┐
    │  ├─ 编程 Flash   │        │           │
    │  ├─ 校验 CRC32   │      有效        无效
    │  └─ 复位/跳转    │        │           │
    └──────────────────┘        ▼           ▼
                         ┌──────────┐  ┌──────────┐
                         │ 跳转到   │  │ 停留在   │
                         │ 用户 App │  │ Bootloader│
                         └──────────┘  └──────────┘
```

### 3.3 CAN OTA 数据流

```
  ┌──────────────┐    CAN Bus (500 kbps)    ┌──────────────┐
  │   上位机 PC   │ ◄─────────────────────► │  STM32F407   │
  │              │                          │              │
  │  MicroBoot   │   TX ID: 0x667 ────────► │  OpenBLT     │
  │     or       │   (XCP Command)          │  Bootloader  │
  │ BootCommander│                          │              │
  │              │ ◄──────── RX ID: 0x7E1   │              │
  │              │   (XCP Response)          │              │
  └──────┬───────┘                          └──────┬───────┘
         │                                         │
    USB-to-CAN                              CAN Transceiver
    适配器                                  (TJA1050/SN65HVD230)
  (PEAK/Kvaser/                             │
   SocketCAN)                          ┌────┴────┐
                                       │ CAN_H   │
                                       │ CAN_L   │
                                       │ (双绞线) │
                                       └─────────┘
```

---

## 4. Flash 内存分区规划

STM32F407ZGT6 拥有 1024 KB（1 MB）片上 Flash，扇区结构如下：

```
Flash 地址空间 (1024 KB Total)
═══════════════════════════════════════════════════════════════

0x0800 0000 ┌───────────────────────────┐ ─┐
            │      Sector 0 (16 KB)     │  │
0x0800 4000 ├───────────────────────────┤  ├── Bootloader 区
            │      Sector 1 (16 KB)     │  │   (32 KB)
0x0800 8000 ├───────────────────────────┤ ─┘
            │      Sector 2 (16 KB)     │ ─── 保留/参数区 (可选)
0x0800 C000 ├───────────────────────────┤
            │      Sector 3 (16 KB)     │ ─── 共享 RAM 标志区 (可选)
0x0801 0000 ├───────────────────────────┤ ─┐
            │      Sector 4 (64 KB)     │  │
0x0802 0000 ├───────────────────────────┤  │
            │      Sector 5 (128 KB)    │  │
0x0804 0000 ├───────────────────────────┤  │
            │      Sector 6 (128 KB)    │  │
0x0806 0000 ├───────────────────────────┤  ├── 用户应用程序区
            │      Sector 7 (128 KB)    │  │   (960 KB / 可调)
0x0808 0000 ├───────────────────────────┤  │
            │      Sector 8 (128 KB)    │  │
0x080A 0000 ├───────────────────────────┤  │
            │      Sector 9 (128 KB)    │  │
0x080C 0000 ├───────────────────────────┤  │
            │      Sector 10 (128 KB)   │  │
0x080E 0000 ├───────────────────────────┤  │
            │      Sector 11 (128 KB)   │  │
0x0810 0000 └───────────────────────────┘ ─┘

═══════════════════════════════════════════════════════════════

Bootloader 入口地址:  0x0800 0000
用户 App 入口地址:    0x0800 8000  (推荐，32KB 偏移)
```

**关键设计决策：**

- Bootloader 占用 Sector 0 + Sector 1（共 32 KB），这是 STM32F4 的最小擦除粒度的整数倍，足以容纳 CAN 通信的 OpenBLT
- 用户应用程序从 Sector 2（0x08008000）开始。如果不需要参数区，也可从 Sector 4（0x08010000）开始以获得更清晰的扇区边界
- STM32F4 的扇区大小不均匀（16K/64K/128K），这直接影响 `blt_conf.h` 中的 `BOOT_FLASH_VECTOR_TABLE_CS_OFFSET` 配置

---

## 5. 开发环境搭建

### 5.1 软件需求

| 工具 | 版本要求 | 用途 |
|------|---------|------|
| Keil MDK-ARM | v5.38+ | 编译 Bootloader 与 App |
| ARMCLANG (AC6) | v6.19+ | AC6 编译器（MDK 内置） |
| STM32CubeMX | v6.9+ | HAL 代码生成与外设配置 |
| OpenBLT | v1.21.x | Bootloader 源码 |
| MicroBoot | 随 OpenBLT 发布 | GUI 固件升级工具 |
| BootCommander | 随 OpenBLT 发布 | CLI 固件升级工具 |
| ST-Link Utility / STM32CubeProgrammer | 最新版 | 首次烧录 Bootloader |

### 5.2 硬件需求

| 组件 | 规格说明 |
|------|---------|
| MCU 板 | STM32F407ZGT6 核心板 / 自定义 PCB |
| CAN 收发器 | TJA1050 / SN65HVD230 / MCP2551 |
| USB-to-CAN 适配器 | PEAK PCAN-USB / Kvaser Leaf / CANable (SocketCAN) |
| 终端电阻 | CAN 总线两端各 120Ω |
| 调试器 | ST-Link V2/V3 或 J-Link（仅首次烧录需要） |

### 5.3 获取 OpenBLT 源码

```bash
# 方式一：从 GitHub 克隆
git clone https://github.com/feaser/openblt.git

# 方式二：从 SourceForge 下载发布包
# https://sourceforge.net/projects/openblt/
```

克隆完成后，关注以下目录：

```
openblt/
├── Target/
│   ├── Source/
│   │   ├── ARMCM4_STM32F4/      ← STM32F4 硬件驱动（直接使用）
│   │   ├── boot.c, xcp.c ...    ← Core 层源码
│   │   └── boot.h, xcp.h ...
│   └── Demo/
│       ├── ARMCM4_STM32F4_Olimex_STM32E407_IAR/     ← 参考工程
│       └── ARMCM4_STM32F4_Nucleo_F446RE_CubeIDE/    ← 参考工程
└── Host/
    ├── Source/MicroBoot/         ← GUI 上位机源码
    └── Source/BootCommander/     ← CLI 工具源码
```

---

## 6. CubeMX 工程配置

### 6.1 创建 Bootloader 工程

**Step 1 — 新建工程**

打开 STM32CubeMX，选择芯片型号 `STM32F407ZGTx`。

**Step 2 — 时钟配置（RCC）**

- HSE → Crystal/Ceramic Resonator（外部晶振，通常 8 MHz 或 25 MHz，视硬件而定）
- 配置 PLL 使 SYSCLK = 168 MHz
- APB1 Prescaler = /4 → APB1 = 42 MHz（CAN 外设挂载在 APB1 总线上）
- APB2 Prescaler = /2 → APB2 = 84 MHz

**Step 3 — CAN 外设配置**

- 启用 CAN1（如果使用 CAN2 则需同时启用 CAN1，因为 STM32F4 的 CAN2 依赖 CAN1 的时钟和过滤器）
- 参数配置（以 500 kbps 为例，APB1 = 42 MHz）：

```
Prescaler           = 6
Time Quanta in BS1  = 11
Time Quanta in BS2  = 2
Baud Rate           = 42MHz / 6 / (1+11+2) = 500 kbps
SJW                 = 1
Mode                = Normal
```

- GPIO 引脚：CAN1_RX → PD0，CAN1_TX → PD1（根据实际硬件调整，常见还有 PB8/PB9, PA11/PA12）

**Step 4 — GPIO 配置（可选 LED 指示）**

- 配置一个 LED GPIO 为推挽输出（如 PF9），用于 Bootloader 运行状态指示

**Step 5 — 禁用不需要的外设**

Bootloader 应尽量精简，仅启用必要外设：RCC、CAN、GPIO、可选 IWDG。

**Step 6 — 工程生成设置**

- Project Manager → Toolchain/IDE → MDK-ARM V5
- Code Generator → 勾选"Generate peripheral initialization as a pair of .c/.h files"
- 生成工程

### 6.2 CubeMX 生成后的调整

CubeMX 生成的工程需要做以下调整以集成 OpenBLT：

1. **修改 Keil 工程中的编译器为 AC6**：Target Options → ARM Compiler → Use default compiler version 6
2. **调整 Flash 起始地址和大小**：Target Options → Target → IROM1 → Start: 0x08000000, Size: 0x8000 (32KB)
3. **调整 RAM 配置**：IRAM1 → Start: 0x20000000, Size: 0x20000 (128KB)

---

## 7. OpenBLT 移植详解

### 7.1 移植步骤总览

```
Step 1:  从 OpenBLT 仓库复制必要源码到工程
Step 2:  编写 blt_conf.h 配置文件
Step 3:  实现 hooks.c 回调函数
Step 4:  编写 Bootloader 主函数 main.c
Step 5:  配置 Keil 链接脚本（限定 Flash 范围）
Step 6:  添加源文件到 Keil 工程并配置 Include 路径
Step 7:  编译、烧录、验证
```

### 7.2 Step 1 — 复制 OpenBLT 源码

从 `openblt/Target/Source/` 目录复制以下文件到工程的 `Boot/OpenBLT/` 目录：

**Core 层（平台无关，直接复制）：**

```
boot.c / boot.h
xcp.c / xcp.h
com.c / com.h
backdoor.c / backdoor.h
cop.c / cop.h
assert.c / assert.h
plausibility.h
```

**Target 层（STM32F4 专用）：**

从 `openblt/Target/Source/ARMCM4_STM32F4/` 复制：

```
can.c / can.h        ← CAN 驱动
flash.c / flash.h    ← Flash 编程驱动
timer.c / timer.h    ← 定时器驱动
cpu.c / cpu.h        ← CPU 控制（跳转/复位）
nvm.c / nvm.h        ← NVM 抽象层
```

### 7.3 Step 2 — 配置 blt_conf.h

这是整个移植工作的核心。以下是针对 STM32F407ZGT6 + CAN 的完整配置：

```c
/**
 * @file    blt_conf.h
 * @brief   OpenBLT Bootloader 配置文件
 * @target  STM32F407ZGT6
 * @comm    CAN 2.0B @ 500kbps
 */

#ifndef BLT_CONF_H
#define BLT_CONF_H

/* ==================== 系统时钟配置 ==================== */

/* 外部晶振频率 (kHz) */
#define BOOT_CPU_XTAL_SPEED_KHZ         (8000)

/* 系统时钟频率 (kHz) — PLL 配置后的 SYSCLK */
#define BOOT_CPU_SYSTEM_SPEED_KHZ       (168000)

/* ==================== CAN 通信配置 ==================== */

/* 启用 CAN 通信接口 */
#define BOOT_COM_CAN_ENABLE             (1)

/* CAN 外设通道选择：0 = CAN1, 1 = CAN2 */
#define BOOT_COM_CAN_CHANNEL_INDEX      (0)

/* CAN 波特率 (bps) */
#define BOOT_COM_CAN_BAUDRATE           (500000)

/* XCP 命令报文 ID（主机 → 从机），标准帧 11-bit */
#define BOOT_COM_CAN_TX_MSG_ID          (0x667u)

/* XCP 响应报文 ID（从机 → 主机），标准帧 11-bit */
#define BOOT_COM_CAN_RX_MSG_ID          (0x7E1u)

/* CAN 报文最大数据长度 (CAN 2.0 = 8 bytes) */
#define BOOT_COM_CAN_TX_MAX_DATA        (8)
#define BOOT_COM_CAN_RX_MAX_DATA        (8)

/*
 * 若使用扩展帧 (29-bit ID)，将 ID 最高位置 1：
 * #define BOOT_COM_CAN_TX_MSG_ID  (0x80000667u)
 */

/* ==================== Flash 配置 ==================== */

/* Flash 总大小 (KB) */
#define BOOT_NVM_SIZE_KB                (1024)

/* 用户程序起始偏移 (Bootloader 占用 32KB = 0x8000) */
/* 该值对应 flash.c 中 flashLayout[] 表的配置 */
/* 从 Sector 2 开始分配给用户程序 */

/* 用户程序校验用的 checksum 偏移量 (字节) */
/* 通常放在用户中断向量表起始位置 */
#define BOOT_FLASH_VECTOR_TABLE_CS_OFFSET  (0x8000u)

/* ==================== Backdoor 入口配置 ==================== */

/* 启用 Backdoor 机制 */
#define BOOT_BACKDOOR_ENTRY_TIMEOUT_MS  (500)

/* ==================== 看门狗配置 ==================== */

/* 启用内部看门狗支持 (0=禁用, 1=启用) */
#define BOOT_COP_ENABLE                 (0)

/* ==================== Seed/Key 安全配置 ==================== */

/* 启用 XCP 安全认证 (0=禁用, 1=启用) */
#define BOOT_XCP_SEED_KEY_ENABLE        (0)

/* ==================== 文件系统 (SD卡) 配置 ==================== */

/* 禁用文件系统升级（仅使用 CAN） */
#define BOOT_FILE_SYS_ENABLE            (0)

/* ==================== 调试与断言 ==================== */

/* 启用断言 (开发阶段启用，量产时禁用) */
#define BOOT_ASSERT_ENABLE              (1)

/* 禁用未使用的通信接口 */
#define BOOT_COM_RS232_ENABLE           (0)
#define BOOT_COM_USB_ENABLE             (0)
#define BOOT_COM_NET_ENABLE             (0)
#define BOOT_COM_MBRTU_ENABLE           (0)

#endif /* BLT_CONF_H */
```

### 7.4 Step 3 — 实现 hooks.c

```c
/**
 * @file    hooks.c
 * @brief   OpenBLT 用户回调函数实现
 */

#include "boot.h"
#include "stm32f4xx_hal.h"

/* ==================== LED 指示（可选） ==================== */

/* 前向声明：CubeMX 生成的初始化函数 */
extern void SystemClock_Config(void);
extern void MX_GPIO_Init(void);
extern void MX_CAN1_Init(void);

/**
 * @brief  Bootloader 断言失败回调
 */
#if (BOOT_ASSERT_ENABLE > 0)
void AssertFailureHook(const blt_char *file, blt_int32u line)
{
    /* 进入死循环，便于调试器捕获 */
    for (;;) { }
}
#endif

/**
 * @brief  Backdoor 入口检测回调
 * @retval BLT_TRUE  = 强制进入 Bootloader
 * @retval BLT_FALSE = 使用默认超时逻辑
 *
 * 可在此检测 GPIO 按键状态，实现按键强制进入 Bootloader
 */
blt_bool BackDoorEntryHook(void)
{
    /* 示例：检测某个 GPIO 引脚是否拉低（按键按下） */
    /*
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET)
    {
        return BLT_TRUE;  // 强制进入 Bootloader
    }
    */
    return BLT_FALSE;  /* 使用默认超时逻辑 */
}

/**
 * @brief  固件升级完成回调
 * @note   可在此添加 LED 熄灭、外设去初始化等操作
 */
#if (BOOT_COM_ENABLE > 0)
void ComFreeHook(void)
{
    /* 去初始化 CAN 外设，确保跳转到 App 前外设状态干净 */
    HAL_CAN_DeInit(&hcan1);
    HAL_DeInit();
}
#endif
```

### 7.5 Step 4 — 编写 main.c

```c
/**
 * @file    main.c
 * @brief   Bootloader 主入口
 */

#include "stm32f4xx_hal.h"
#include "boot.h"

/* CubeMX 生成的外设句柄（外部声明） */
extern CAN_HandleTypeDef hcan1;

/* CubeMX 生成的初始化函数声明 */
void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_CAN1_Init(void);

int main(void)
{
    /* STM32 HAL 初始化 */
    HAL_Init();

    /* 系统时钟配置 (CubeMX 生成) */
    SystemClock_Config();

    /* GPIO 初始化 (LED 等) */
    MX_GPIO_Init();

    /* CAN 外设初始化 (CubeMX 生成) */
    MX_CAN1_Init();

    /* ===== OpenBLT Bootloader 初始化 ===== */
    BootInit();

    /* ===== Bootloader 主循环 ===== */
    while (1)
    {
        BootTask();
    }
}
```

> **关键说明：** `BootInit()` 会初始化 XCP 协议栈和 Backdoor 定时器。`BootTask()` 在主循环中持续调用，处理 CAN 报文接收、Backdoor 超时检测、Flash 编程操作。当 Backdoor 超时且用户程序有效时，`BootTask()` 内部会自动跳转到用户程序，`while(1)` 不会被执行到。

### 7.6 Step 5 — Keil 链接配置

在 Keil MDK 中配置 Bootloader 的 Flash 地址范围：

```
Target Options → Target 选项卡:

  IROM1:
    Start:  0x08000000
    Size:   0x00008000    (32 KB — Bootloader 区)

  IRAM1:
    Start:  0x20000000
    Size:   0x00020000    (128 KB)

如果使用 scatter file (.sct)：
───────────────────────────────────
LR_IROM1 0x08000000 0x00008000  {
  ER_IROM1 0x08000000 0x00008000  {
    *.o (RESET, +First)
    *(InRoot$$Sections)
    .ANY (+RO)
  }
  RW_IRAM1 0x20000000 0x00020000  {
    .ANY (+RW +ZI)
  }
}
───────────────────────────────────
```

### 7.7 Step 6 — Keil 工程配置

**添加源文件分组：**

```
Project 面板中创建如下 Groups:

├── Application/User
│   └── main.c, stm32f4xx_it.c, stm32f4xx_hal_msp.c
├── OpenBLT/App
│   └── hooks.c, led.c (可选)
├── OpenBLT/Core
│   └── boot.c, xcp.c, com.c, backdoor.c, cop.c, assert.c
├── OpenBLT/Target
│   └── can.c, flash.c, timer.c, cpu.c, nvm.c
├── Drivers/CMSIS
│   └── system_stm32f4xx.c, startup_stm32f407xx.s
└── Drivers/STM32F4xx_HAL
    └── stm32f4xx_hal.c, stm32f4xx_hal_can.c,
        stm32f4xx_hal_cortex.c, stm32f4xx_hal_rcc.c,
        stm32f4xx_hal_gpio.c, stm32f4xx_hal_flash.c,
        stm32f4xx_hal_flash_ex.c, stm32f4xx_hal_pwr.c
```

**添加 Include 路径：**

```
C/C++ → Include Paths:

../Core/Inc
../OpenBLT/App
../OpenBLT/Core        (或 OpenBLT 源码的 Target/Source 目录)
../OpenBLT/Target
../Drivers/CMSIS/Include
../Drivers/CMSIS/Device/ST/STM32F4xx/Include
../Drivers/STM32F4xx_HAL_Driver/Inc
```

**预处理宏定义：**

```
C/C++ → Preprocessor Symbols → Define:

USE_HAL_DRIVER,STM32F407xx
```

**AC6 编译器设置：**

```
C/C++ → Language / Code Generation:
  - Language C: C11 (gnu11)
  - Optimization: -Oz (Size) 或 -O2
  - Warnings: AC5-like Warnings
```

### 7.8 Step 7 — flash.c 扇区表核实

确认 `flash.c` 中的 `flashLayout[]` 数组正确反映 STM32F407ZGT6 的扇区布局，且 Bootloader 占用的扇区已被排除（不可被用户程序擦写）：

```c
/* OpenBLT flash.c 中的扇区表（示意）*/
/* 注意：Sector 0 和 Sector 1 属于 Bootloader，不在此表中 */
static const tFlashSector flashLayout[] =
{
    /* { 扇区起始地址,    扇区大小,  扇区编号 } */
    { 0x08008000,  0x04000,   2 },   /* Sector 2  -  16 KB */
    { 0x0800C000,  0x04000,   3 },   /* Sector 3  -  16 KB */
    { 0x08010000,  0x10000,   4 },   /* Sector 4  -  64 KB */
    { 0x08020000,  0x20000,   5 },   /* Sector 5  - 128 KB */
    { 0x08040000,  0x20000,   6 },   /* Sector 6  - 128 KB */
    { 0x08060000,  0x20000,   7 },   /* Sector 7  - 128 KB */
    { 0x08080000,  0x20000,   8 },   /* Sector 8  - 128 KB */
    { 0x080A0000,  0x20000,   9 },   /* Sector 9  - 128 KB */
    { 0x080C0000,  0x20000,  10 },   /* Sector 10 - 128 KB */
    { 0x080E0000,  0x20000,  11 },   /* Sector 11 - 128 KB */
};
```

---

## 8. 用户应用程序适配

用户应用程序需要做以下适配才能与 Bootloader 协同工作。

### 8.1 中断向量表偏移

在用户应用程序的 `main()` 函数最开始处（在任何外设初始化之前），设置中断向量表偏移：

```c
int main(void)
{
    /* ★ 必须第一行执行 — 重定位中断向量表 */
    SCB->VTOR = 0x08008000U;

    /* 后续正常初始化 */
    HAL_Init();
    SystemClock_Config();
    // ...
}
```

也可以在 `system_stm32f4xx.c` 中修改宏定义：

```c
#define VECT_TAB_OFFSET  0x8000U  /* 偏移 32KB */
```

### 8.2 Keil 链接配置（用户 App）

```
Target Options → Target 选项卡:

  IROM1:
    Start:  0x08008000           ← 偏移到 Bootloader 之后
    Size:   0x000F8000           ← 1024KB - 32KB = 992KB

  IRAM1:
    Start:  0x20000000
    Size:   0x00020000           ← 128 KB
```

### 8.3 生成 S-record 固件文件

OpenBLT 上位机工具使用 Motorola S-record (.srec) 格式的固件文件。在 Keil 中配置：

```
Target Options → User 选项卡 → After Build/Rebuild:

  Run #1:  fromelf --m32 --output=@L.srec !L

  或使用 S-record 格式:
  Run #1:  fromelf --vhx --8x1 --output=@L.srec !L
```

更推荐的方式是使用 OpenBLT 自带的 `srec_cat` 工具或直接在 Keil 的 Output 设置中勾选 "Create HEX File"，然后使用 SrecConverter 转换。

### 8.4 触发固件升级

用户应用程序可通过以下方式触发 Bootloader：

**方式一：软件复位**

```c
/* 在用户 App 中收到升级命令后 */
NVIC_SystemReset();  /* 触发系统复位，重新进入 Bootloader */
```

**方式二：共享 RAM 标志**

```c
/* 用户 App：在固定 RAM 地址写入标志后复位 */
*((volatile uint32_t *)0x20000000) = 0xDEADBEEF;
NVIC_SystemReset();

/* Bootloader hooks.c：检测标志 */
blt_bool BackDoorEntryHook(void)
{
    if (*((volatile uint32_t *)0x20000000) == 0xDEADBEEF)
    {
        *((volatile uint32_t *)0x20000000) = 0;  /* 清除标志 */
        return BLT_TRUE;  /* 强制进入 Bootloader */
    }
    return BLT_FALSE;
}
```

---

## 9. CAN 总线 OTA 升级流程

### 9.1 物理连接

```
PC (USB-to-CAN 适配器) ──── CAN_H ──── [120Ω] ──── CAN_H (STM32 板)
                        ──── CAN_L ──── [120Ω] ──── CAN_L (STM32 板)
                        ──── GND   ──────────────── GND

CAN 总线参数:
  - 波特率: 500 kbps
  - 终端电阻: 总线两端各 120Ω
  - 线缆: 双绞线，长度 < 40m (500kbps 时)
```

### 9.2 升级步骤

1. **准备固件文件** — 将用户应用程序编译生成 `.srec` 或 `.hex` 文件
2. **连接 CAN 适配器** — USB-to-CAN 适配器连接 PC 与目标板
3. **复位目标板** — 使目标板进入 Bootloader 的 Backdoor 窗口
4. **启动上位机** — 打开 MicroBoot 或执行 BootCommander 命令
5. **选择固件文件** — 在工具中选择 `.srec` 文件
6. **开始升级** — 工具自动完成：连接 → 擦除 → 编程 → 校验 → 复位
7. **验证** — 目标板自动启动新固件

### 9.3 BootCommander 命令示例

```bash
# Windows — 使用 PEAK PCAN-USB 适配器
BootCommander.exe -s=xcp_can -d=peak_pcanusb -b=500000 \
    -tid=0x667 -rid=0x7E1 firmware.srec

# Linux — 使用 SocketCAN
BootCommander -s=xcp_can -d=socketcan -b=500000 \
    -tid=0x667 -rid=0x7E1 -n=can0 firmware.srec
```

---

## 10. 上位机工具使用

### 10.1 MicroBoot (GUI)

1. 启动 MicroBoot.exe
2. Settings → Communication Interface → XCP on CAN
3. 配置 CAN 适配器类型（PCAN-USB / Kvaser / SocketCAN）
4. 设置波特率 500000，TX ID = 0x667，RX ID = 0x7E1
5. 拖放固件 `.srec` 文件到界面，或点击 Browse 选择
6. 点击 Start 开始升级，进度条实时显示

### 10.2 LibOpenBLT 自定义工具

如果需要开发自定义上位机（例如集成到产线工具中），可使用 LibOpenBLT 库：

```c
#include "openblt.h"

/* 初始化 */
BltSessionSettingsXcpV10 xcpSettings = { .timeoutT1 = 1000, .timeoutT3 = 2000 };
BltTransportSettingsXcpV10Can canSettings = {
    .deviceName = "peak_pcanusb",
    .baudrate = 500000,
    .transmitId = 0x667,
    .receiveId = 0x7E1
};

BltSessionInit(BLT_SESSION_XCP_V10, &xcpSettings,
               BLT_TRANSPORT_XCP_V10_CAN, &canSettings);

/* 执行固件升级 */
BltFirmwareInit(BLT_FIRMWARE_SRECORD);
BltFirmwareLoadFromFile("firmware.srec");

// ... 擦除、编程、校验 ...

BltSessionStop();
BltSessionCleanup();
```

---

## 11. 生产与部署

### 11.1 首次烧录流程

量产时 Bootloader 仅需通过 SWD/JTAG 烧录一次，后续固件均通过 CAN OTA 升级。

```
Step 1:  通过 ST-Link 烧录 Bootloader (boot.hex → 0x08000000)
Step 2:  通过 CAN OTA 写入首版用户固件 (app.srec → 0x08008000)
Step 3:  验证功能正常
Step 4:  （可选）锁定 Bootloader 扇区写保护
```

### 11.2 合并固件映像

如果需要一次性烧录 Bootloader + App 的合并映像：

```bash
# 使用 srec_cat 合并
srec_cat boot.hex -Intel app.srec -Motorola -o combined.hex -Intel
```

### 11.3 Flash 写保护

建议对 Bootloader 所在扇区（Sector 0-1）启用写保护，防止用户程序误擦除 Bootloader：

```c
/* 通过 Option Bytes 配置（CubeProgrammer 或代码中） */
FLASH_OBProgramInitTypeDef ob;
ob.OptionType = OPTIONBYTE_WRP;
ob.WRPSector  = OB_WRP_SECTOR_0 | OB_WRP_SECTOR_1;
ob.WRPState   = OB_WRPSTATE_ENABLE;
HAL_FLASHEx_OBProgram(&ob);
HAL_FLASH_OB_Launch();
```

---

## 12. 故障排除

| 现象 | 可能原因 | 解决方案 |
|------|---------|---------|
| 上位机无法连接 | CAN 波特率不匹配 | 确认两端波特率一致（500kbps） |
| 上位机无法连接 | CAN ID 配置错误 | 检查 TX/RX ID 与 `blt_conf.h` 一致 |
| 上位机无法连接 | Backdoor 窗口已关闭 | 复位目标板后立即连接 |
| CAN 无通信 | 缺少终端电阻 | CAN 总线两端各加 120Ω |
| CAN 无通信 | CAN_H/CAN_L 接反 | 检查收发器接线 |
| 升级后 App 不运行 | 中断向量表未偏移 | 在 App 的 main() 首行设置 SCB->VTOR |
| 升级后 App 不运行 | App 链接地址错误 | 确认 App IROM1 Start = 0x08008000 |
| 编程失败 | Flash 扇区表不匹配 | 核实 flash.c 中 flashLayout[] 与实际芯片一致 |
| Bootloader 不跳转 | 用户程序区为空或校验失败 | 确认已正确烧录用户程序 |
| 看门狗复位 | COP 配置但未喂狗 | 禁用 BOOT_COP_ENABLE 或确保喂狗逻辑正确 |

---

## 13. 参考资源

| 资源 | 链接 |
|------|------|
| OpenBLT 官方网站 | https://www.feaser.com/en/openblt.php |
| OpenBLT Wiki（详细文档） | https://www.feaser.com/openblt/doku.php |
| OpenBLT GitHub 镜像 | https://github.com/feaser/openblt |
| STM32F4 端口说明 | https://www.feaser.com/openblt/doku.php?id=manual:ports:armcm4_stm32 |
| STM32F407 参考手册 (RM0090) | ST 官网 |
| STM32F407 数据手册 | ST 官网 |
| XCP 协议规范 (ASAM MCD-1) | https://www.asam.net |
| Keil MDK AC6 迁移指南 | Keil 官网 |

---

<p align="center">
  <sub>
    本项目 Bootloader 部分基于 OpenBLT，遵循 GPLv3 许可证。<br/>
    如需用于闭源商业产品，请从 Feaser 获取商业许可。<br/>
    © 2026 · Industrial Embedded Solutions
  </sub>
</p>
