/**
 ******************************************************************************
 * @file    EXTI/EXTI_Config/stm32f10x_it.c
 * @author  MCD Application Team
 * @version V3.5.0
 * @date    08-April-2011
 * @brief   Main Interrupt Service Routines.
 *          This file provides template for all exceptions handler and peripherals
 *          interrupt service routine.
 ******************************************************************************
 * @attention
 *
 * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
 * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
 * TIME. AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY
 * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
 * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
 * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
 *
 * <h2><center>&copy; COPYRIGHT 2011 STMicroelectronics</center></h2>
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x_it.h"
#include "main.h" //在it的头文件可能会导致别的地方也能调用main的东西，不符合安全规范
#include "FaultSnapshot.h"

// #include "stm32_eval.h"

/** @addtogroup STM32F10x_StdPeriph_Examples
 * @{
 */

/** @addtogroup EXTI_Config
 * @{
 */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/******************************************************************************/
/*            Cortex-M3 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
 * @brief  This function handles NMI exception.
 * @param  None
 * @retval None
 */
void NMI_Handler(void)
{
}

__asm void wait()
{
  BX lr
}

static void Fault_SaveReason(UINT16 reason)
{
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
  PWR_BackupAccessCmd(ENABLE);
  BKP_WriteBackupRegister(FAULT_BKP_REASON_REG, reason);
  BKP_WriteBackupRegister(FAULT_BKP_REASON_INV_REG, (UINT16)(~reason));
}

static void Fault_ResetOrHold(UINT16 reason)
{
  Fault_SaveReason(reason);
#ifdef _DEBUG_
  while (1)
  {
    wait();
  }
#else
  NVIC_SystemReset();
  while (1)
  {
  }
#endif
}

/**
 * @brief  This function handles Hard Fault exception.
 * @param  None
 * @retval None
 */
void HardFault_Handler(void)
{
  Fault_ResetOrHold(FAULT_REASON_HARD);
}

/**
 * @brief  This function handles Memory Manage exception.
 * @param  None
 * @retval None
 */
void MemManage_Handler(void)
{
  Fault_ResetOrHold(FAULT_REASON_MEM);
}

/**
 * @brief  This function handles Bus Fault exception.
 * @param  None
 * @retval None
 */
void BusFault_Handler(void)
{
  Fault_ResetOrHold(FAULT_REASON_BUS);
}

/**
 * @brief  This function handles Usage Fault exception.
 * @param  None
 * @retval None
 */
void UsageFault_Handler(void)
{
  Fault_ResetOrHold(FAULT_REASON_USAGE);
}

/**
 * @brief  This function handles SVCall exception.
 * @param  None
 * @retval None
 */
void SVC_Handler(void)
{
}

/**
 * @brief  This function handles Debug Monitor exception.
 * @param  None
 * @retval None
 */
void DebugMon_Handler(void)
{
}

/**
 * @brief  This function handles PendSV_Handler exception.
 * @param  None
 * @retval None
 */
void PendSV_Handler(void)
{
}

/**
 * @brief  This function handles SysTick Handler.
 * @param  None
 * @retval None
 */
void SysTick_Handler(void)
{
}

/******************************************************************************/
/*            STM32F10x Peripherals Interrupt Handlers                        */
/******************************************************************************/

/**
 * @brief  This function handles External line 0 interrupt request.
 * @param  None
 * @retval Nonehttps://www.cingta.com/static/image/20190816/bb94f7d7c1f94c4daf38e66b8aaa80cd.png
 */
void EXTI0_IRQHandler(void)
{
  UINT8 handled = 0U;

  if (EXTI_GetITStatus(EXTI_Line0) != RESET)
  {
    sys_time.cnt_PA0_irq++;
    handled = 1U;
    EXTI_ClearITPendingBit(EXTI_Line0);
  }
  if (handled == 0U)
  {
  }
}

void EXTI2_IRQHandler(void)
{
  UINT8 handled = 0U;

  if (EXTI_GetITStatus(EXTI_Line2) != RESET)
  {
    handled = 1U;
    EXTI_ClearITPendingBit(EXTI_Line2);
  }
  if (handled == 0U)
  {
  }
}

void EXTI3_IRQHandler(void)
{
  UINT8 handled = 0U;

  if (EXTI_GetITStatus(EXTI_Line3) != RESET)
  {
    handled = 1U;
    EXTI_ClearITPendingBit(EXTI_Line3);
  }
  if (handled == 0U)
  {
  }
}

void EXTI15_10_IRQHandler(void)
{
  UINT8 handled = 0U;

  if (EXTI_GetITStatus(EXTI_Line12) != RESET)
  {
    handled = 1U;
    EXTI_ClearITPendingBit(EXTI_Line12);
  }
  if (EXTI_GetITStatus(EXTI_Line13) != RESET)
  {
    handled = 1U;
    EXTI_ClearITPendingBit(EXTI_Line13);
  }
  if (handled == 0U)
  {
  }
}

/**
 * @brief  This function handles External lines 9 to 5 interrupt request.
 * @param  None
 * @retval None
 */
void EXTI9_5_IRQHandler(void)
{
  UINT8 handled = 0U;

  if (EXTI_GetITStatus(EXTI_Line5) != RESET)
  {
    handled = 1U;
    EXTI_ClearITPendingBit(EXTI_Line5);
  }
  if (EXTI_GetITStatus(EXTI_Line6) != RESET)
  {
    handled = 1U;
    EXTI_ClearITPendingBit(EXTI_Line6);
  }
  if (EXTI_GetITStatus(EXTI_Line7) != RESET)
  {
    handled = 1U;
    EXTI_ClearITPendingBit(EXTI_Line7);
  }
  if (EXTI_GetITStatus(EXTI_Line9) != RESET)
  {
    sys_time.cnt_bms1_keyirq++;
    handled = 1U;
    g_irq_t = soc_key;
    EXTI_ClearITPendingBit(EXTI_Line9);
  }
  if (handled == 0U)
  {
  }
}

/******************************************************************************/
/*                 STM32F10x Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f10x_xx.s).                                            */
/******************************************************************************/

/**
 * @brief  This function handles PPP interrupt request.
 * @param  None
 * @retval None
 */
/*void PPP_IRQHandler(void)
{
}*/

/**
 * @}
 */

/**
 * @}
 */

// 以下的是非共有的，如果多处使用到，则移动到it.c文件
void USART1_IRQHandler(void)
{
  sys_time.sci1_irq_cnt++;
#ifdef _COMMOM_UPPER_SCI1
  Sci1_CommonUpper_IRQHandler();
#endif
}
void USART2_IRQHandler(void)
{
  sys_time.sci2_irq_cnt++;
#ifdef _COMMOM_UPPER_SCI2
  Sci2_CommonUpper_IRQHandler();
#endif
}
#ifdef _COMMOM_UPPER_SCI3
void USART3_IRQHandler(void)
{
  sys_time.sci3_irq_cnt++;
  Sci3_CommonUpper_IRQHandler();
}
#endif

/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
