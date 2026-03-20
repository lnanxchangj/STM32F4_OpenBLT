/************************************************************************************//**
* \file         Demo/ARMCM4_STM32F4_Nucleo_F429ZI_Keil/Boot/hooks.c
* \brief        Bootloader callback source file.
* \ingroup      Boot_ARMCM4_STM32F4_Nucleo_F429ZI_Keil
* \internal
*----------------------------------------------------------------------------------------
*                          C O P Y R I G H T
*----------------------------------------------------------------------------------------
*   Copyright (c) 2021  by Feaser    http://www.feaser.com    All rights reserved
*
*----------------------------------------------------------------------------------------
*                            L I C E N S E
*----------------------------------------------------------------------------------------
* This file is part of OpenBLT. OpenBLT is free software: you can redistribute it and/or
* modify it under the terms of the GNU General Public License as published by the Free
* Software Foundation, either version 3 of the License, or (at your option) any later
* version.
*
* OpenBLT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
* PURPOSE. See the GNU General Public License for more details.
*
* You have received a copy of the GNU General Public License along with OpenBLT. It
* should be located in ".\Doc\license.html". If not, contact Feaser to obtain a copy.
*
* \endinternal
****************************************************************************************/

/****************************************************************************************
* Include files
****************************************************************************************/
#include "boot.h"                                /* bootloader generic header          */
#include "can.h"


/****************************************************************************************
*   D E V I C E   I N F O   D E F I N E S
****************************************************************************************/
/** \brief Device info storage addresses (in Sector 4, 0x08010000).
 *         These are located at the end of Sector 4 to avoid conflicting with
 *         bootloader code. Sector 4 is 64KB = 0x10000, ending at 0x0801FFFF.
 */
#define DEVICE_INFO_ADDR           (0x0801FF00)
#define DEVICE_INFO_BOARD_TYPE     (*(volatile uint32_t *)(0x0801FF00))  /* 4 bytes */
#define DEVICE_INFO_FW_VERSION     (*(volatile uint32_t *)(0x0801FF04))  /* 4 bytes: major.minor.revision.build */
#define DEVICE_INFO_SERIAL         (0x0801FF08)                           /* 16 bytes */
#define DEVICE_INFO_NAME           (0x0801FF18)                           /* variable length, null-terminated */

/** \brief RTC Backup Register for upgrade flag (uses RTC_BKP_DR0).
 *         Address: 0x40002850 (STM32F4 RTC_BKP_DR0)
 */
#define RTC_BKP_DR0_ADDR           (0x40002850)
#define UPGRADE_FLAG_VALUE         (0xDEADBEEF)

/** \brief Board Type IDs */
typedef enum {
    BOARD_TYPE_UNKNOWN   = 0x00,
    BOARD_TYPE_MASTER    = 0x01,  /* 主控板 */
    BOARD_TYPE_ARM       = 0x02,  /* 机械臂 */
    BOARD_TYPE_TEST      = 0x03,  /* 测试板 */
    BOARD_TYPE_PREPROCESS = 0x04,  /* 预处理板 */
    BOARD_TYPE_TEMPCTRL  = 0x05,  /* 温控板 */
    BOARD_TYPE_COOLING   = 0x06,  /* 制冷板 */
} BoardType;


/****************************************************************************************
*   L O C A L   F U N C T I O N S
****************************************************************************************/
/************************************************************************************//**
** \brief     Reads a value from RTC Backup Register.
** \param     none.
** \return    The 32-bit value stored in RTC_BKP_DR0.
**
****************************************************************************************/
static uint32_t RTC_BKP_DR0_Read(void)
{
  return *(volatile uint32_t *)RTC_BKP_DR0_ADDR;
} /*** end of RTC_BKP_DR0_Read ***/


/************************************************************************************//**
** \brief     Writes a value to RTC Backup Register 0.
** \param     value   The 32-bit value to write.
** \return    none.
**
****************************************************************************************/
static void RTC_BKP_DR0_Write(uint32_t value)
{
  *(volatile uint32_t *)RTC_BKP_DR0_ADDR = value;
} /*** end of RTC_BKP_DR0_Write ***/


/************************************************************************************//**
** \brief     Enables access to RTC Backup registers.
** \details   Direct register access for STM32F4 Bootloader compatibility.
**            Uses bitband access for reliable register writes.
** \return    none.
**
****************************************************************************************/
static void EnableBackupAccess(void)
{
  /* STM32F4 register addresses */
  volatile uint32_t *pRCC_APB1ENR = (volatile uint32_t *)(0x40023820);  /* RCC APB1 enable */
  volatile uint32_t *pPWR_CR = (volatile uint32_t *)(0x40007000);          /* PWR control */
  volatile uint32_t *pRCC_BDCR = (volatile uint32_t *)(0x40023870);       /* RCC backup domain control */

  /* 1. Enable PWR clock (RCC_APB1ENR bit 28 - PWREN) */
  *pRCC_APB1ENR = *pRCC_APB1ENR | (1U << 28);

  /* 2. Set DBP bit in PWR_CR (bit 8) to enable backup domain write access */
  *pPWR_CR = *pPWR_CR | (1U << 8);

  /* 3. Enable RTC clock (RCC_BDCR bit 15 - RTCEN) */
  *pRCC_BDCR = *pRCC_BDCR | (1U << 15);
} /*** end of EnableBackupAccess ***/


/****************************************************************************************
*   B A C K D O O R   E N T R Y   H O O K   F U N C T I O N S
****************************************************************************************/

#if (BOOT_BACKDOOR_HOOKS_ENABLE > 0)
/************************************************************************************//**
** \brief     Initializes the backdoor entry option.
** \return    none.
**
****************************************************************************************/
void BackDoorInitHook(void)
{
  /* Enable backup domain access for RTC registers */
  EnableBackupAccess();
} /*** end of BackDoorInitHook ***/


/************************************************************************************//**
** \brief     Checks if a backdoor entry is requested.
** \return    BLT_TRUE if the backdoor entry is requested, BLT_FALSE otherwise.
**
** \details   Implements seamless jump using RTC Backup Register:
**            - Normal power-on/reset: RTC_BKP_DR0 = 0, verify APP checksum and jump (0 delay)
**            - APP requests upgrade: Set flag 0xDEADBEEF + soft reset, BL stays in upgrade mode
**            - If upgrade interrupted by power loss: flag lost, BL verifies APP checksum
**
****************************************************************************************/
blt_bool BackDoorEntryHook(void)
{
  blt_bool result = BLT_TRUE;

  /* Check if upgrade flag is set in RTC Backup Register 0 */
  if (RTC_BKP_DR0_Read() == UPGRADE_FLAG_VALUE)
  {
    /* Upgrade was requested - clear flag and stay in bootloader */
    RTC_BKP_DR0_Write(0);
    result = BLT_TRUE;  /* Stay in bootloader for upgrade */
  }
  else
  {
    /* No upgrade flag - verify APP checksum and jump immediately (0 delay) */
    if (NvmVerifyChecksum() == BLT_TRUE)
    {
      /* Valid APP found - return BLT_FALSE to skip backdoor timeout
       * and let CpuStartUserProgram() handle the jump immediately */
      result = BLT_FALSE;
    }
    else
    {
      /* No valid APP - stay in bootloader */
      result = BLT_TRUE;
    }
  }

  return result;
} /*** end of BackDoorEntryHook ***/


/************************************************************************************//**
** \brief     Reads device information from flash.
** \param     type    Pointer to store board type ID.
** \param     hwVer   Pointer to store hardware version.
** \param     blVer   Pointer to store bootloader version.
** \return    none.
**
****************************************************************************************/
void DeviceInfoRead(uint8_t *type, uint16_t *hwVer, uint16_t *blVer)
{
  if (type != BLT_NULL)
  {
    *type = (uint8_t)(DEVICE_INFO_BOARD_TYPE & 0xFF);
  }
  if (hwVer != BLT_NULL)
  {
    /* firmware version v1.0.0.0: store major.minor in hwVer for compatibility */
    uint8_t fw_major = (uint8_t)((DEVICE_INFO_FW_VERSION >> 24) & 0xFF);
    uint8_t fw_minor = (uint8_t)((DEVICE_INFO_FW_VERSION >> 16) & 0xFF);
    *hwVer = (fw_major << 8) | fw_minor;
  }
  if (blVer != BLT_NULL)
  {
    /* bootloader version not stored, return 0 */
    *blVer = 0;
  }
} /*** end of DeviceInfoRead ***/


/************************************************************************************//**
** \brief     Sets the upgrade flag and triggers a soft reset.
** \details   Called by APP when it wants to enter upgrade mode.
**            This sets the RTC backup flag and resets the MCU.
** \return    none. (Does not return - triggers reset)
**
****************************************************************************************/
void RequestEnterUpgradeMode(void)
{
  /* Set upgrade flag in RTC Backup Register 0 */
  RTC_BKP_DR0_Write(UPGRADE_FLAG_VALUE);

  /* Trigger software reset */
  NVIC_SystemReset();
} /*** end of RequestEnterUpgradeMode ***/
#endif /* BOOT_BACKDOOR_HOOKS_ENABLE > 0 */


/****************************************************************************************
*   C P U   D R I V E R   H O O K   F U N C T I O N S
****************************************************************************************/

#if (BOOT_CPU_USER_PROGRAM_START_HOOK > 0)
/************************************************************************************//**
** \brief     Callback that gets called when the bootloader is about to exit and
**            hand over control to the user program. This is the last moment that
**            some final checking can be performed and if necessary prevent the
**            bootloader from activiting the user program.
** \return    BLT_TRUE if it is okay to start the user program, BLT_FALSE to keep
**            keep the bootloader active.
**
****************************************************************************************/
blt_bool CpuUserProgramStartHook(void)
{
  /* Direct register access for USART1 deinit */
  volatile uint32_t *pUSART1_CR1 = (volatile uint32_t *)(0x40011000);  /* USART1 base */
  *pUSART1_CR1 = 0;  /* Disable USART1 */

  /* Direct register access for CAN1 deinit */
  volatile uint32_t *pCAN1_MCR = (volatile uint32_t *)(0x40006400);  /* CAN1 base */
  *pCAN1_MCR = (1 << 1);  /* INRQ=1 to enter init mode */

  return BLT_TRUE;
} /*** end of CpuUserProgramStartHook ***/
#endif /* BOOT_CPU_USER_PROGRAM_START_HOOK > 0 */


/****************************************************************************************
*   W A T C H D O G   D R I V E R   H O O K   F U N C T I O N S
****************************************************************************************/

#if (BOOT_COP_HOOKS_ENABLE > 0)
/************************************************************************************//**
** \brief     Callback that gets called at the end of the internal COP driver
**            initialization routine. It can be used to configure and enable the
**            watchdog.
** \return    none.
**
****************************************************************************************/
void CopInitHook(void)
{
  /* this function is called upon initialization. might as well use it to initialize
   * the LED driver. It is kind of a visual watchdog anyways.
   */

} /*** end of CopInitHook ***/


/************************************************************************************//**
** \brief     Callback that gets called at the end of the internal COP driver
**            service routine. This gets called upon initialization and during
**            potential long lasting loops and routine. It can be used to service
**            the watchdog to prevent a watchdog reset.
** \return    none.
**
****************************************************************************************/
void CopServiceHook(void)
{
  /* run the LED blink task. this is a better place to do it than in the main() program
   * loop. certain operations such as flash erase can take a long time, which would cause
   * a blink interval to be skipped. this function is also called during such operations,
   * so no blink intervals will be skipped when calling the LED blink task here.
   */

} /*** end of CopServiceHook ***/
#endif /* BOOT_COP_HOOKS_ENABLE > 0 */


/****************************************************************************************
*   U S B   C O M M U N I C A T I O N   I N T E R F A C E   H O O K   F U N C T I O N S
****************************************************************************************/

#if (BOOT_COM_USB_ENABLE > 0)
/************************************************************************************//**
** \brief     Callback that gets called whenever the USB device should be connected
**            to the USB bus.
** \param     connect BLT_TRUE to connect and BLT_FALSE to disconnect.
** \return    none.
**
****************************************************************************************/
void UsbConnectHook(blt_bool connect)
{
  /* Note that this is handled automatically by the OTG peripheral. */
} /*** end of UsbConnect ***/


/************************************************************************************//**
** \brief     Callback that gets called whenever the USB host requests the device
**            to enter a low power mode.
** \return    none.
**
****************************************************************************************/
void UsbEnterLowPowerModeHook(void)
{
  /* support to enter a low power mode can be implemented here */
} /*** end of UsbEnterLowPowerMode ***/


/************************************************************************************//**
** \brief     Callback that gets called whenever the USB host requests the device to
**            exit low power mode.
** \return    none.
**
****************************************************************************************/
void UsbLeaveLowPowerModeHook(void)
{
  /* support to leave a low power mode can be implemented here */
} /*** end of UsbLeaveLowPowerMode ***/
#endif /* BOOT_COM_USB_ENABLE > 0 */


/****************************************************************************************
*   N O N - V O L A T I L E   M E M O R Y   D R I V E R   H O O K   F U N C T I O N S
****************************************************************************************/

#if (BOOT_NVM_HOOKS_ENABLE > 0)
/************************************************************************************//**
** \brief     Callback that gets called at the start of the internal NVM driver
**            initialization routine.
** \return    none.
**
****************************************************************************************/
void NvmInitHook(void)
{
} /*** end of NvmInitHook ***/


/************************************************************************************//**
** \brief     Callback that gets called at the start of the NVM driver write
**            routine. It allows additional memory to be operated on. If the address
**            is not within the range of the additional memory, then
**            BLT_NVM_NOT_IN_RANGE must be returned to indicate that the data hasn't
**            been written yet.
** \param     addr Start address.
** \param     len  Length in bytes.
** \param     data Pointer to the data buffer.
** \return    BLT_NVM_OKAY if successful, BLT_NVM_NOT_IN_RANGE if the address is
**            not within the supported memory range, or BLT_NVM_ERROR is the write
**            operation failed.
**
****************************************************************************************/
blt_int8u NvmWriteHook(blt_addr addr, blt_int32u len, blt_int8u *data)
{
  return BLT_NVM_NOT_IN_RANGE;
} /*** end of NvmWriteHook ***/


/************************************************************************************//**
** \brief     Callback that gets called at the start of the NVM driver erase
**            routine. It allows additional memory to be operated on. If the address
**            is not within the range of the additional memory, then
**            BLT_NVM_NOT_IN_RANGE must be returned to indicate that the memory
**            hasn't been erased yet.
** \param     addr Start address.
** \param     len  Length in bytes.
** \return    BLT_NVM_OKAY if successful, BLT_NVM_NOT_IN_RANGE if the address is
**            not within the supported memory range, or BLT_NVM_ERROR is the erase
**            operation failed.
**
****************************************************************************************/
blt_int8u NvmEraseHook(blt_addr addr, blt_int32u len)
{
  return BLT_NVM_NOT_IN_RANGE;
} /*** end of NvmEraseHook ***/


/************************************************************************************//**
** \brief     Callback that gets called at the end of the NVM programming session.
** \return    BLT_TRUE is successful, BLT_FALSE otherwise.
**
****************************************************************************************/
blt_bool NvmDoneHook(void)
{
  return BLT_TRUE;
} /*** end of NvmDoneHook ***/
#endif /* BOOT_NVM_HOOKS_ENABLE > 0 */


#if (BOOT_NVM_CHECKSUM_HOOKS_ENABLE > 0)
/************************************************************************************//**
** \brief     Verifies the checksum, which indicates that a valid user program is
**            present and can be started.
** \return    BLT_TRUE if successful, BLT_FALSE otherwise.
**
****************************************************************************************/
blt_bool NvmVerifyChecksumHook(void)
{
  return BLT_TRUE;
} /*** end of NvmVerifyChecksum ***/


/************************************************************************************//**
** \brief     Writes a checksum of the user program to non-volatile memory. This is
**            performed once the entire user program has been programmed. Through
**            the checksum, the bootloader can check if a valid user programming is
**            present and can be started.
** \return    BLT_TRUE if successful, BLT_FALSE otherwise.
**
****************************************************************************************/
blt_bool NvmWriteChecksumHook(void)
{
  return BLT_TRUE;
}
#endif /* BOOT_NVM_CHECKSUM_HOOKS_ENABLE > 0 */


/****************************************************************************************
*   S E E D / K E Y   S E C U R I T Y   H O O K   F U N C T I O N S
****************************************************************************************/

#if (BOOT_XCP_SEED_KEY_ENABLE > 0)
/************************************************************************************//**
** \brief     Provides a seed to the XCP master that will be used for the key
**            generation when the master attempts to unlock the specified resource.
**            Called by the GET_SEED command.
** \param     resource  Resource that the seed if requested for (XCP_RES_XXX).
** \param     seed      Pointer to byte buffer wher the seed will be stored.
** \return    Length of the seed in bytes.
**
****************************************************************************************/
blt_int8u XcpGetSeedHook(blt_int8u resource, blt_int8u *seed)
{
  /* request seed for unlocking ProGraMming resource */
  if ((resource & XCP_RES_PGM) != 0)
  {
    seed[0] = 0x55;
  }

  /* return seed length */
  return 1;
} /*** end of XcpGetSeedHook ***/


/************************************************************************************//**
** \brief     Called by the UNLOCK command and checks if the key to unlock the
**            specified resource was correct. If so, then the resource protection
**            will be removed.
** \param     resource  resource to unlock (XCP_RES_XXX).
** \param     key       pointer to the byte buffer holding the key.
** \param     len       length of the key in bytes.
** \return    1 if the key was correct, 0 otherwise.
**
****************************************************************************************/
blt_int8u XcpVerifyKeyHook(blt_int8u resource, blt_int8u *key, blt_int8u len)
{
  /* suppress compiler warning for unused parameter */
  len = len;

  /* the example key algorithm in "libseednkey.dll" works as follows:
   *  - PGM will be unlocked if key = seed - 1
   */

  /* check key for unlocking ProGraMming resource */
  if ((resource == XCP_RES_PGM) && (key[0] == (0x55-1)))
  {
    /* correct key received for unlocking PGM resource */
    return 1;
  }

  /* still here so key incorrect */
  return 0;
} /*** end of XcpVerifyKeyHook ***/
#endif /* BOOT_XCP_SEED_KEY_ENABLE > 0 */


/*********************************** end of hooks.c ************************************/
