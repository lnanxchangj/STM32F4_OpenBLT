/************************************************************************************//**
* \file         hooks.c
* \brief        Bootloader callback hook functions.
****************************************************************************************/
#include "boot.h"
#include "usart.h"
#include "can.h"

/************************************************************************************//**
** \brief     Called right before the user program is started. Use it to deinit
**            hardware that was used by the bootloader.
** \return    BLT_TRUE if it is okay to start the user program, BLT_FALSE to keep
**            the bootloader active.
****************************************************************************************/
#if (BOOT_CPU_USER_PROGRAM_START_HOOK > 0)
blt_bool CpuUserProgramStartHook(void)
{
    /* 在跳转到用户 App 之前，可在此去初始化外设 */
    /* 例如: HAL_CAN_DeInit(&hcan1); HAL_DeInit(); */
    /* 也可以什么都不做，OpenBLT 的 cpu.c 会处理基本的跳转 */
    /* 1. 关闭全局中断，防止跳转瞬间触发中断导致跑飞 */
    HAL_UART_MspDeInit(&huart1);
    HAL_CAN_MspDeInit(&hcan1);
    __disable_irq();

    /* 2. 停止 SysTick 定时器（HAL 库默认开启，不关掉会进 App 报错） */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* 3. 如果你刚才用了串口或CAN，建议在这里 DeInit */
    // extern UART_HandleTypeDef huart1;
    // HAL_UART_DeInit(&huart1);

    /* 4. 清除所有待处理的中断标志 */
    for (uint8_t i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    return BLT_TRUE;
}
#endif


/************************************************************************************//**
** \brief     Called when an assertion is triggered. Useful for debugging.
****************************************************************************************/
#if (BOOT_ASSERT_ENABLE > 0)
void AssertFailureHook(const blt_char *file, blt_int32u line)
{
    (void)file;
    (void)line;
    /* 在调试器中设断点在此处可查看 file 和 line 值 */
    for (;;)
    {
    }
}
#endif