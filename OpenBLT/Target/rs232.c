/************************************************************************************//**
* \file         Source/ARMCM4_STM32F4/rs232.c
* \brief        Bootloader RS232 communication interface source file.
* \ingroup      Target_ARMCM4_STM32F4
* \internal
*----------------------------------------------------------------------------------------
*                          C O P Y R I G H T
*----------------------------------------------------------------------------------------
*   Copyright (c) 2013  by Feaser    http://www.feaser.com    All rights reserved
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
#if (BOOT_COM_RS232_ENABLE > 0)
#include "stm32f4xx.h"                           /* STM32 CPU and HAL header           */
#include "stm32f4xx_ll_usart.h"                  /* STM32 LL USART header              */


/****************************************************************************************
* Macro definitions
****************************************************************************************/
/** \brief Timeout time for the reception of a CTO packet. The timer is started upon
 *         reception of the first packet byte.
 */
#define RS232_CTO_RX_PACKET_TIMEOUT_MS (200u)
/** \brief Timeout for transmitting a byte in milliseconds. */
#define RS232_BYTE_TX_TIMEOUT_MS       (10u)

/* map the configured UART channel index to the STM32's USART peripheral */
#if (BOOT_COM_RS232_CHANNEL_INDEX == 0)
/** \brief Set UART base address to USART1. */
#define USART_CHANNEL   USART1
#elif (BOOT_COM_RS232_CHANNEL_INDEX == 1)
/** \brief Set UART base address to USART2. */
#define USART_CHANNEL   USART2
#elif (BOOT_COM_RS232_CHANNEL_INDEX == 2)
/** \brief Set UART base address to USART3. */
#define USART_CHANNEL   USART3
#elif (BOOT_COM_RS232_CHANNEL_INDEX == 3)
/** \brief Set UART base address to UART4. */
#define USART_CHANNEL   UART4
#elif (BOOT_COM_RS232_CHANNEL_INDEX == 4)
/** \brief Set UART base address to UART5. */
#define USART_CHANNEL   UART5
#elif (BOOT_COM_RS232_CHANNEL_INDEX == 5)
/** \brief Set UART base address to UART6. */
#define USART_CHANNEL   USART6
#elif (BOOT_COM_RS232_CHANNEL_INDEX == 6)
/** \brief Set UART base address to UART7. */
#define USART_CHANNEL   UART7
#elif (BOOT_COM_RS232_CHANNEL_INDEX == 7)
/** \brief Set UART base address to UART8. */
#define USART_CHANNEL   UART8
#endif


/****************************************************************************************
* BLE transparent-mode module support (optional)
*
* Set BLE_MODULE_ENABLE to 1 if the RS232 channel is connected to a BLE module that
* requires a handshake to enter transparent (pass-through) mode before XCP communication
* can begin. The handshake sequence is:
*
*   1. BLE module sends "BLE_CONNECT_SUCCESS" upon external device connection.
*   2. MCU responds with "AT+BLUFISEND=1\r\n" to request transparent mode.
*   3. BLE module replies "OK" to confirm.
*   4. All subsequent bytes are passed directly to the XCP protocol layer.
*
* Set BLE_MODULE_ENABLE to 0 for a standard wired UART connection (original behavior,
* no handshake overhead, no additional code size).
****************************************************************************************/
/** \brief Enable(1)/disable(0) BLE module transparent-mode handshake on the RS232
 *         channel. Set to 0 for plain UART upgrade without BLE.
 */
#define BLE_MODULE_ENABLE  1


#if (BLE_MODULE_ENABLE == 1)
/****************************************************************************************
* BLE state machine type definitions and variables
****************************************************************************************/
/** \brief BLE module handshake state machine states. */
typedef enum
{
  BLE_IDLE = 0,        /**< waiting for BLE_CONNECT_SUCCESS                            */
  BLE_WAIT_OK,         /**< AT+BLUFISEND=1 sent, waiting for OK response               */
  BLE_TRANSPARENT      /**< transparent mode active, all bytes pass through to XCP      */
} tBleState;

/** \brief Size of the line buffer for BLE text reception during handshake. */
#define BLE_LINE_BUF_SIZE  32

/** \brief Current BLE handshake state. */
static tBleState  bleState = BLE_IDLE;
/** \brief Line buffer for accumulating BLE module text responses. */
static blt_char   bleLineBuf[BLE_LINE_BUF_SIZE];
/** \brief Current write index in the line buffer. */
static blt_int8u  bleLineIdx = 0;
#endif /* BLE_MODULE_ENABLE == 1 */


/****************************************************************************************
* Function prototypes
****************************************************************************************/
static blt_bool Rs232ReceiveByte(blt_int8u *data);
static void     Rs232TransmitByte(blt_int8u data);
#if (BLE_MODULE_ENABLE == 1)
static void     BleSendString(const blt_char *str);
static blt_bool BleStrContains(const blt_char *haystack, const blt_char *needle);
#endif


/************************************************************************************//**
** \brief     Initializes the RS232 communication interface.
** \return    none.
**
****************************************************************************************/
void Rs232Init(void)
{
  LL_USART_InitTypeDef USART_InitStruct;

  /* the current implementation supports USART1 - UART8. throw an assertion error in
   * case a different UART channel is configured.
   */
  ASSERT_CT((BOOT_COM_RS232_CHANNEL_INDEX == 0) ||
            (BOOT_COM_RS232_CHANNEL_INDEX == 1) ||
            (BOOT_COM_RS232_CHANNEL_INDEX == 2) ||
            (BOOT_COM_RS232_CHANNEL_INDEX == 3) ||
            (BOOT_COM_RS232_CHANNEL_INDEX == 4) ||
            (BOOT_COM_RS232_CHANNEL_INDEX == 5) ||
            (BOOT_COM_RS232_CHANNEL_INDEX == 6) ||
            (BOOT_COM_RS232_CHANNEL_INDEX == 7));

  /* disable the UART peripheral */
  LL_USART_Disable(USART_CHANNEL);
  /* configure UART peripheral */
  USART_InitStruct.BaudRate = BOOT_COM_RS232_BAUDRATE;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  /* initialize the UART peripheral */
  LL_USART_Init(USART_CHANNEL, &USART_InitStruct);
  LL_USART_Enable(USART_CHANNEL);

  #if (BLE_MODULE_ENABLE == 1)
  /* initialize the BLE handshake state machine */
  bleState   = BLE_IDLE;
  bleLineIdx = 0;
  #endif
} /*** end of Rs232Init ***/


/************************************************************************************//**
** \brief     Transmits a packet formatted for the communication interface.
** \param     data Pointer to byte array with data that it to be transmitted.
** \param     len  Number of bytes that are to be transmitted.
** \return    none.
**
****************************************************************************************/
void Rs232TransmitPacket(blt_int8u *data, blt_int8u len)
{
  blt_int16u data_index;
  #if (BOOT_COM_RS232_CS_TYPE == 1)
  blt_int8u csByte = len;
  #endif

  /* verify validity of the len-paramenter */
  ASSERT_RT(len <= BOOT_COM_RS232_TX_MAX_DATA);

  /* first transmit the length of the packet */
  Rs232TransmitByte(len);

  /* transmit all the packet bytes one-by-one */
  for (data_index = 0; data_index < len; data_index++)
  {
    /* keep the watchdog happy */
    CopService();
    /* write byte */
    Rs232TransmitByte(data[data_index]);
    #if (BOOT_COM_RS232_CS_TYPE == 1)
    csByte += data[data_index];
    #endif
  }
  #if (BOOT_COM_RS232_CS_TYPE == 1)
  /* write checksum byte */
  Rs232TransmitByte(csByte);
  #endif
} /*** end of Rs232TransmitPacket ***/


/************************************************************************************//**
** \brief     Receives a communication interface packet if one is present.
** \param     data Pointer to byte array where the data is to be stored.
** \param     len Pointer where the length of the packet is to be stored.
** \return    BLT_TRUE if a packet was received, BLT_FALSE otherwise.
**
****************************************************************************************/
blt_bool Rs232ReceivePacket(blt_int8u *data, blt_int8u *len)
{
  /* one extra for length and two extra for possibly configured checksum byte(s). */
  static blt_int8u xcpCtoReqPacket[BOOT_COM_RS232_RX_MAX_DATA+3];
  static blt_int8u xcpCtoRxLength;
  static blt_bool  xcpCtoRxInProgress = BLT_FALSE;
  static blt_int32u xcpCtoRxStartTime = 0;
  #if (BOOT_COM_RS232_CS_TYPE == 1)
  blt_int8u  csLen = 1;
  blt_int8u  csByte;
  blt_int16u csIdx;
  #else
  blt_int8u  csLen = 0;
  #endif

  /* start of cto packet received? */
  if (xcpCtoRxInProgress == BLT_FALSE)
  {
    /* store the message length when received */
    if (Rs232ReceiveByte(&xcpCtoReqPacket[0]) == BLT_TRUE)
    {
      if ( (xcpCtoReqPacket[0] > 0) &&
           (xcpCtoReqPacket[0] <= BOOT_COM_RS232_RX_MAX_DATA) )
      {
        /* store the start time */
        xcpCtoRxStartTime = TimerGet();
        /* reset packet data count */
        xcpCtoRxLength = 0;
        /* indicate that a cto packet is being received */
        xcpCtoRxInProgress = BLT_TRUE;
      }
    }
  }
  else
  {
    /* store the next packet byte */
    if (Rs232ReceiveByte(&xcpCtoReqPacket[xcpCtoRxLength+1]) == BLT_TRUE)
    {
      /* increment the packet data count */
      xcpCtoRxLength++;

      /* check to see if the entire packet was received. */
      if (xcpCtoRxLength == (xcpCtoReqPacket[0] + csLen))
      {
        #if (BOOT_COM_RS232_CS_TYPE == 1)
        /* calculate the byte checksum. */
        csByte = 0;
        for (csIdx = 0; csIdx < xcpCtoRxLength; csIdx++)
        {
          csByte += xcpCtoReqPacket[csIdx];
        }
        /* verify the checksum. */
        if (csByte != xcpCtoReqPacket[xcpCtoRxLength])
        {
          /* cancel the packet reception due to invalid checksum. */
          xcpCtoRxInProgress = BLT_FALSE;
          return BLT_FALSE;
        }
        #endif
        /* subtract the checksum from the packet length. */
        xcpCtoRxLength -= csLen;
        /* copy the packet data */
        CpuMemCopy((blt_int32u)data, (blt_int32u)&xcpCtoReqPacket[1], xcpCtoRxLength);
        /* done with cto packet reception */
        xcpCtoRxInProgress = BLT_FALSE;
        /* set the packet length */
        *len = xcpCtoRxLength;
        /* packet reception complete */
        return BLT_TRUE;
      }
    }
    else
    {
      /* check packet reception timeout */
      if (TimerGet() > (xcpCtoRxStartTime + RS232_CTO_RX_PACKET_TIMEOUT_MS))
      {
        /* cancel cto packet reception due to timeout. note that this automatically
         * discards the already received packet bytes, allowing the host to retry.
         */
        xcpCtoRxInProgress = BLT_FALSE;
      }
    }
  }
  /* packet reception not yet complete */
  return BLT_FALSE;
} /*** end of Rs232ReceivePacket ***/


/************************************************************************************//**
** \brief     Receives a communication interface byte if one is present.
** \param     data Pointer to byte where the data is to be stored.
** \return    BLT_TRUE if a byte was received, BLT_FALSE otherwise.
**
** \details   When BLE_MODULE_ENABLE is 0, this function returns the raw received byte
**            directly (original OpenBLT behavior).
**
**            When BLE_MODULE_ENABLE is 1, a handshake state machine is embedded:
**            - BLE_TRANSPARENT : returns the byte directly (no overhead).
**            - BLE_IDLE        : buffers text lines and matches "BLE_CONNECT_SUCCESS".
**            - BLE_WAIT_OK     : buffers text lines and matches "OK".
**            - A non-printable (binary) byte during handshake enters transparent mode
**              immediately, providing compatibility with direct wired UART connections.
**
****************************************************************************************/
static blt_bool Rs232ReceiveByte(blt_int8u *data)
{
  #if (BLE_MODULE_ENABLE == 1)
  blt_int8u rxByte;
  blt_bool  isTextChar;
  #endif

  /* check if a new byte was received on the configured channel */
  if (LL_USART_IsActiveFlag_RXNE(USART_CHANNEL) == 0)
  {
    return BLT_FALSE;
  }

  #if (BLE_MODULE_ENABLE == 0)
  /* ----------------------------------------------------------------
   * Standard UART mode: return the byte directly (original behavior).
   * ---------------------------------------------------------------- */
  *data = LL_USART_ReceiveData8(USART_CHANNEL);
  return BLT_TRUE;

  #else
  /* ----------------------------------------------------------------
   * BLE module mode: handshake state machine.
   * ---------------------------------------------------------------- */

  /* retrieve the newly received byte */
  rxByte = LL_USART_ReceiveData8(USART_CHANNEL);

  /* transparent mode: all bytes pass directly to the XCP protocol layer */
  if (bleState == BLE_TRANSPARENT)
  {
    *data = rxByte;
    return BLT_TRUE;
  }

  /* classify byte as text (BLE module response) or binary (XCP packet).
   * BLE text: CR(0x0D), LF(0x0A), printable ASCII (0x20..0x7E).
   * XCP length byte is typically < 0x20 or > 0x7E -> non-text -> binary.
   */
  isTextChar = (blt_bool)(rxByte == '\r' ||
                          rxByte == '\n' ||
                          (rxByte >= 0x20u && rxByte <= 0x7Eu));

  /* non-text byte received: must be XCP data, enter transparent mode */
  if (!isTextChar)
  {
    bleState = BLE_TRANSPARENT;
    *data = rxByte;
    return BLT_TRUE;
  }

  /* text line processing: accumulate characters and match on line terminator */
  if (rxByte == '\r' || rxByte == '\n')
  {
    /* process the buffered line if it contains any characters */
    if (bleLineIdx > 0)
    {
      /* null-terminate the line */
      bleLineBuf[bleLineIdx] = '\0';

      switch (bleState)
      {
        case BLE_IDLE:
          /* check for BLE connection notification */
          if (BleStrContains(bleLineBuf, "BLE_CONNECT_SUCCESS"))
          {
            BleSendString("AT+BLUFISEND=1\r\n");
            bleState = BLE_WAIT_OK;
          }
          break;

        case BLE_WAIT_OK:
          /* check for AT command acknowledgement */
          if (BleStrContains(bleLineBuf, "OK"))
          {
            bleState = BLE_TRANSPARENT;
          }
          break;

        default:
          break;
      }

      /* reset the line buffer for the next line */
      bleLineIdx = 0;
    }
    /* text bytes during handshake are not passed to the XCP layer */
    return BLT_FALSE;
  }

  /* printable character: append to the line buffer */
  if (bleLineIdx < (BLE_LINE_BUF_SIZE - 1u))
  {
    bleLineBuf[bleLineIdx] = (blt_char)rxByte;
    bleLineIdx++;
  }
  else
  {
    /* buffer overflow: discard current line to prevent corruption */
    bleLineIdx = 0;
  }

  /* text bytes during handshake are not passed to the XCP layer */
  return BLT_FALSE;
  #endif /* BLE_MODULE_ENABLE */
} /*** end of Rs232ReceiveByte ***/


/************************************************************************************//**
** \brief     Transmits a communication interface byte.
** \param     data Value of byte that is to be transmitted.
** \return    none.
**
****************************************************************************************/
static void Rs232TransmitByte(blt_int8u data)
{
  blt_int32u timeout;

  /* write byte to transmit holding register */
  LL_USART_TransmitData8(USART_CHANNEL, data);
  /* set timeout time to wait for transmit completion. */
  timeout = TimerGet() + RS232_BYTE_TX_TIMEOUT_MS;
  /* wait for tx holding register to be empty */
  while (LL_USART_IsActiveFlag_TXE(USART_CHANNEL) == 0)
  {
    /* keep the watchdog happy */
    CopService();
    /* break loop upon timeout. this would indicate a hardware failure. */
    if (TimerGet() > timeout)
    {
      break;
    }
  }
} /*** end of Rs232TransmitByte ***/


#if (BLE_MODULE_ENABLE == 1)
/************************************************************************************//**
** \brief     Transmits a null-terminated string on the RS232 channel. Used to send
**            AT commands to the BLE module during the handshake phase.
** \param     str Pointer to the null-terminated string to transmit.
** \return    none.
**
****************************************************************************************/
static void BleSendString(const blt_char *str)
{
  while (*str != '\0')
  {
    Rs232TransmitByte((blt_int8u)*str);
    str++;
  }
} /*** end of BleSendString ***/


/************************************************************************************//**
** \brief     Checks if a string contains a given substring. This function does not
**            depend on string.h to keep the bootloader footprint minimal.
** \param     haystack Pointer to the string to search in.
** \param     needle   Pointer to the substring to search for.
** \return    BLT_TRUE if the substring was found, BLT_FALSE otherwise.
**
****************************************************************************************/
static blt_bool BleStrContains(const blt_char *haystack, const blt_char *needle)
{
  blt_int8u hlen = 0;
  blt_int8u nlen = 0;
  blt_int8u i, j;

  /* determine string lengths */
  while (haystack[hlen] != '\0') { hlen++; }
  while (needle[nlen]   != '\0') { nlen++; }

  /* early exit if needle is empty or longer than haystack */
  if ((nlen == 0u) || (nlen > hlen))
  {
    return BLT_FALSE;
  }

  /* brute-force substring search */
  for (i = 0u; i <= (hlen - nlen); i++)
  {
    for (j = 0u; j < nlen; j++)
    {
      if (haystack[i + j] != needle[j])
      {
        break;
      }
    }
    if (j == nlen)
    {
      return BLT_TRUE;
    }
  }

  return BLT_FALSE;
} /*** end of BleStrContains ***/
#endif /* BLE_MODULE_ENABLE == 1 */


#endif /* BOOT_COM_RS232_ENABLE > 0 */


/*********************************** end of rs232.c ************************************/