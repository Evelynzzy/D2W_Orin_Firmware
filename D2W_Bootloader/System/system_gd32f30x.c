/*!
    \file  system_gd32f30x.c
    \brief CMSIS Cortex-M4 Device Peripheral Access Layer Source File for
           GD32F30x Device Series
*/

/* Copyright (c) 2012 ARM LIMITED

   All rights reserved.
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:
   - Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
   - Neither the name of ARM nor the names of its contributors may be used
     to endorse or promote products derived from this software without
     specific prior written permission.
   *
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS AND CONTRIBUTORS BE
   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
   ---------------------------------------------------------------------------*/
/* This file refers the CMSIS standard, some adjustments are made according to GigaDevice chips */

#include "gd32f30x.h"

/*
 * Cold-start clock diagnostics.
 *
 * BKP_DATA37 records the HXTAL wait time in milliseconds.
 * BKP_DATA38 identifies the image that wrote the record.
 * BKP_DATA39 records the last completed or failed clock stage.
 *
 * The bootloader uses 0xB007 as its image identifier.  Registers 37 to 39
 * are intentionally separate from the boot-request registers 40 and 41.
 */
#define CLOCK_DIAG_IMAGE_ID                    ((uint16_t)0xB007U)
#define CLOCK_DIAG_HXTAL_TIMEOUT_MS            ((uint32_t)3000U)
#define CLOCK_DIAG_FLAG_WAIT_LOOPS             ((uint32_t)12000000U)

#define CLOCK_DIAG_STAGE_SYSTEM_INIT_ENTER     ((uint16_t)0x0001U)
#define CLOCK_DIAG_STAGE_IRC8M_WAIT            ((uint16_t)0x0010U)
#define CLOCK_DIAG_STAGE_IRC8M_READY           ((uint16_t)0x0011U)
#define CLOCK_DIAG_STAGE_IRC8M_TIMEOUT         ((uint16_t)0x00E1U)
#define CLOCK_DIAG_STAGE_CLOCK_CONFIG_ENTER    ((uint16_t)0x0100U)
#define CLOCK_DIAG_STAGE_HXTAL_WAIT            ((uint16_t)0x0200U)
#define CLOCK_DIAG_STAGE_HXTAL_READY           ((uint16_t)0x0201U)
#define CLOCK_DIAG_STAGE_HXTAL_TIMEOUT         ((uint16_t)0x02E1U)
#define CLOCK_DIAG_STAGE_PLL_WAIT              ((uint16_t)0x0300U)
#define CLOCK_DIAG_STAGE_PLL_READY             ((uint16_t)0x0301U)
#define CLOCK_DIAG_STAGE_PLL_TIMEOUT           ((uint16_t)0x03E1U)
#define CLOCK_DIAG_STAGE_HIGH_DRIVE_WAIT       ((uint16_t)0x0400U)
#define CLOCK_DIAG_STAGE_HIGH_DRIVE_READY      ((uint16_t)0x0401U)
#define CLOCK_DIAG_STAGE_HIGH_DRIVE_TIMEOUT    ((uint16_t)0x04E1U)
#define CLOCK_DIAG_STAGE_HIGH_SWITCH_WAIT      ((uint16_t)0x0500U)
#define CLOCK_DIAG_STAGE_HIGH_SWITCH_READY     ((uint16_t)0x0501U)
#define CLOCK_DIAG_STAGE_HIGH_SWITCH_TIMEOUT   ((uint16_t)0x05E1U)
#define CLOCK_DIAG_STAGE_SYSCLK_SWITCH_WAIT    ((uint16_t)0x0600U)
#define CLOCK_DIAG_STAGE_SYSCLK_SWITCH_READY   ((uint16_t)0x0601U)
#define CLOCK_DIAG_STAGE_SYSCLK_SWITCH_TIMEOUT ((uint16_t)0x06E1U)
#define CLOCK_DIAG_STAGE_COMPLETE              ((uint16_t)0x0700U)

/* system frequency define */
#define __IRC8M           (IRC8M_VALUE)            /* internal 8 MHz RC oscillator frequency */
#define __HXTAL           (HXTAL_VALUE)            /* high speed crystal oscillator frequency */
#define __SYS_OSC_CLK     (__IRC8M)                /* main oscillator frequency */

/* select a system clock by uncommenting the following line */
/* use IRC8M */
//#define __SYSTEM_CLOCK_IRC8M                    (uint32_t)(__IRC8M) 
//#define __SYSTEM_CLOCK_48M_PLL_IRC8M            (uint32_t)(48000000)
//#define __SYSTEM_CLOCK_72M_PLL_IRC8M            (uint32_t)(72000000)
//#define __SYSTEM_CLOCK_108M_PLL_IRC8M           (uint32_t)(108000000)
//#define __SYSTEM_CLOCK_120M_PLL_IRC8M           (uint32_t)(120000000)

/* use HXTAL(XD series CK_HXTAL = 8M, CL series CK_HXTAL = 25M) */
//#define __SYSTEM_CLOCK_HXTAL                    (uint32_t)(__HXTAL)
//#define __SYSTEM_CLOCK_48M_PLL_HXTAL            (uint32_t)(48000000)
//#define __SYSTEM_CLOCK_72M_PLL_HXTAL            (uint32_t)(72000000)
//#define __SYSTEM_CLOCK_108M_PLL_HXTAL           (uint32_t)(108000000)
#define __SYSTEM_CLOCK_120M_PLL_HXTAL           (uint32_t)(120000000)

#define RCU_MODIFY(__delay)     do{                                     \
                                    volatile uint32_t i;                \
                                    if(0 != __delay){                   \
                                        RCU_CFG0 |= RCU_AHB_CKSYS_DIV2; \
                                        for(i=0; i<__delay; i++){       \
                                        }                               \
                                        RCU_CFG0 |= RCU_AHB_CKSYS_DIV4; \
                                        for(i=0; i<__delay; i++){       \
                                        }                               \
                                    }                                   \
                                }while(0)

#define SEL_IRC8M       0x00U
#define SEL_HXTAL       0x01U
#define SEL_PLL         0x02U

/* set the system clock frequency and declare the system clock configuration function */
#ifdef __SYSTEM_CLOCK_IRC8M
uint32_t SystemCoreClock = __SYSTEM_CLOCK_IRC8M;
static void system_clock_8m_irc8m(void);
#elif defined (__SYSTEM_CLOCK_48M_PLL_IRC8M)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_48M_PLL_IRC8M;
static void system_clock_48m_irc8m(void);
#elif defined (__SYSTEM_CLOCK_72M_PLL_IRC8M)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_72M_PLL_IRC8M;
static void system_clock_72m_irc8m(void);
#elif defined (__SYSTEM_CLOCK_108M_PLL_IRC8M)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_108M_PLL_IRC8M;
static void system_clock_108m_irc8m(void);
#elif defined (__SYSTEM_CLOCK_120M_PLL_IRC8M)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_120M_PLL_IRC8M;
static void system_clock_120m_irc8m(void);

#elif defined (__SYSTEM_CLOCK_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_HXTAL;
static void system_clock_hxtal(void);
#elif defined (__SYSTEM_CLOCK_48M_PLL_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_48M_PLL_HXTAL;
static void system_clock_48m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_72M_PLL_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_72M_PLL_HXTAL;
static void system_clock_72m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_108M_PLL_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_108M_PLL_HXTAL;
static void system_clock_108m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_120M_PLL_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_120M_PLL_HXTAL;
static void system_clock_120m_hxtal(void);
#endif /* __SYSTEM_CLOCK_IRC8M */

/* configure the system clock */
static void system_clock_config(void);

/* Store diagnostics in backup registers so they survive a debugger attach. */
static void clock_diag_record(uint16_t stage, uint16_t elapsed_ms)
{
    RCU_APB1EN |= (RCU_APB1EN_PMUEN | RCU_APB1EN_BKPIEN);
    PMU_CTL |= PMU_CTL_BKPWEN;

    BKP_DATA37 = elapsed_ms;
    BKP_DATA38 = CLOCK_DIAG_IMAGE_ID;
    BKP_DATA39 = stage;

    __DSB();
}

/*
 * Measure HXTAL startup with SysTick while CK_SYS is still IRC8M.
 * This replaces the vendor loop-count timeout with a real millisecond timeout.
 */
static uint8_t clock_diag_wait_hxtal(uint32_t timeout_ms,
                                     uint16_t *elapsed_ms)
{
    uint32_t elapsed = 0U;
    uint32_t reload = (__IRC8M / 1000U) - 1U;
    uint8_t ready = 0U;

    SysTick->CTRL = 0U;
    SysTick->LOAD = reload;
    SysTick->VAL = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_ENABLE_Msk;

    while(elapsed < timeout_ms){
        if(0U != (RCU_CTL & RCU_CTL_HXTALSTB)){
            ready = 1U;
            break;
        }

        while(0U == (SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)){
            if(0U != (RCU_CTL & RCU_CTL_HXTALSTB)){
                ready = 1U;
                break;
            }
        }

        if(0U != ready){
            break;
        }

        elapsed++;
    }

    if(0U != (RCU_CTL & RCU_CTL_HXTALSTB)){
        ready = 1U;
    }

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    if(elapsed > 0xFFFFU){
        elapsed = 0xFFFFU;
    }

    if((uint16_t *)0 != elapsed_ms){
        *elapsed_ms = (uint16_t)elapsed;
    }

    return ready;
}

/*!
    \brief      setup the microcontroller system, initialize the system
    \param[in]  none
    \param[out] none
    \retval     none
*/
void SystemInit (void)
{
    uint32_t diag_timeout;

    clock_diag_record(CLOCK_DIAG_STAGE_SYSTEM_INIT_ENTER, 0U);

  /* FPU settings */
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= ((3UL << 10*2)|(3UL << 11*2));  /* set CP10 and CP11 Full Access */
#endif
    /* reset the RCU clock configuration to the default reset state */
    /* Set IRC8MEN bit */
    RCU_CTL |= RCU_CTL_IRC8MEN;
    clock_diag_record(CLOCK_DIAG_STAGE_IRC8M_WAIT, 0U);
    diag_timeout = CLOCK_DIAG_FLAG_WAIT_LOOPS;
    while((0U == (RCU_CTL & RCU_CTL_IRC8MSTB)) &&
          (diag_timeout > 0U)){
        diag_timeout--;
    }
    if(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)){
        clock_diag_record(CLOCK_DIAG_STAGE_IRC8M_TIMEOUT, 0U);
        while(1){
            __NOP();
        }
    }
    clock_diag_record(CLOCK_DIAG_STAGE_IRC8M_READY, 0U);
    RCU_MODIFY(0x50);
    
    RCU_CFG0 &= ~RCU_CFG0_SCS;

#if (defined(GD32F30X_HD) || defined(GD32F30X_XD))
    /* reset HXTALEN, CKMEN and PLLEN bits */
    RCU_CTL &= ~(RCU_CTL_PLLEN | RCU_CTL_CKMEN | RCU_CTL_HXTALEN);
    /* disable all interrupts */
    RCU_INT = 0x009f0000U;
#elif defined(GD32F30X_CL)
    /* Reset HXTALEN, CKMEN, PLLEN, PLL1EN and PLL2EN bits */
    RCU_CTL &= ~(RCU_CTL_PLLEN |RCU_CTL_PLL1EN | RCU_CTL_PLL2EN | RCU_CTL_CKMEN | RCU_CTL_HXTALEN);
    /* disable all interrupts */
    RCU_INT = 0x00ff0000U;
#endif

    /* reset HXTALBPS bit */
    RCU_CTL &= ~(RCU_CTL_HXTALBPS);
    
    /* Reset CFG0 and CFG1 registers */
    RCU_CFG0 = 0x00000000U;
    RCU_CFG1 = 0x00000000U;

    /* configure the system clock source, PLL Multiplier, AHB/APBx prescalers and Flash settings */
    system_clock_config();
}
/*!
    \brief      configure the system clock
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_config(void)
{
#ifdef __SYSTEM_CLOCK_IRC8M
    system_clock_8m_irc8m();
#elif defined (__SYSTEM_CLOCK_48M_PLL_IRC8M)
    system_clock_48m_irc8m();
#elif defined (__SYSTEM_CLOCK_72M_PLL_IRC8M)
    system_clock_72m_irc8m();
#elif defined (__SYSTEM_CLOCK_108M_PLL_IRC8M)
    system_clock_108m_irc8m();
#elif defined (__SYSTEM_CLOCK_120M_PLL_IRC8M)
    system_clock_120m_irc8m();

#elif defined (__SYSTEM_CLOCK_HXTAL)
    system_clock_hxtal();
#elif defined (__SYSTEM_CLOCK_48M_PLL_HXTAL)
    system_clock_48m_hxtal();
#elif defined (__SYSTEM_CLOCK_72M_PLL_HXTAL)
    system_clock_72m_hxtal();
#elif defined (__SYSTEM_CLOCK_108M_PLL_HXTAL)
    system_clock_108m_hxtal();
#elif defined (__SYSTEM_CLOCK_120M_PLL_HXTAL)
    system_clock_120m_hxtal();
#endif /* __SYSTEM_CLOCK_IRC8M */
}

#ifdef __SYSTEM_CLOCK_IRC8M
/*!
    \brief      configure the system clock to 8M by IRC8M
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_8m_irc8m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;
    
    /* wait until IRC8M is stable or the startup time is longer than IRC8M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC8MSTB);
    }
    while((0U == stab_flag) && (IRC8M_STARTUP_TIMEOUT != timeout));
    
    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)){
        while(1){
        }
    }
    
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;
    
    /* select IRC8M as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_IRC8M;
    
    /* wait until IRC8M is selected as system clock */
    while(0U != (RCU_CFG0 & RCU_SCSS_IRC8M)){
    }
}

#elif defined (__SYSTEM_CLOCK_48M_PLL_IRC8M)
/*!
    \brief      configure the system clock to 48M by PLL which selects IRC8M as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_48m_irc8m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;

    /* wait until IRC8M is stable or the startup time is longer than IRC8M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC8MSTB);
    }
    while((0U == stab_flag) && (IRC8M_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)){
      while(1){
      }
    }

    /* LDO output voltage high mode */
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* IRC8M is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

    /* CK_PLL = (CK_IRC8M/2) * 12 = 48 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5);
    RCU_CFG0 |= RCU_PLL_MUL12;

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* enable the high-drive to extend the clock frequency to 120 MHz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    } 
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_72M_PLL_IRC8M)
/*!
    \brief      configure the system clock to 72M by PLL which selects IRC8M as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_72m_irc8m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;

    /* wait until IRC8M is stable or the startup time is longer than IRC8M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC8MSTB);
    }while((0U == stab_flag) && (IRC8M_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)){
        while(1){
        }
    }

    /* LDO output voltage high mode */
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* IRC8M is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

    /* CK_PLL = (CK_IRC8M/2) * 18 = 72 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5);
    RCU_CFG0 |= RCU_PLL_MUL18;

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* enable the high-drive to extend the clock frequency to 120 MHz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    }
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_108M_PLL_IRC8M)
/*!
    \brief      configure the system clock to 108M by PLL which selects IRC8M as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_108m_irc8m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;

    /* wait until IRC8M is stable or the startup time is longer than IRC8M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC8MSTB);
    }while((0U == stab_flag) && (IRC8M_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)){
        while(1){
        }
    }

    /* LDO output voltage high mode */
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* IRC8M is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

    /* CK_PLL = (CK_IRC8M/2) * 27 = 108 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5);
    RCU_CFG0 |= RCU_PLL_MUL27;

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* enable the high-drive to extend the clock frequency to 120 MHz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    }
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_120M_PLL_IRC8M)
/*!
    \brief      configure the system clock to 120M by PLL which selects IRC8M as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_120m_irc8m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;

    /* wait until IRC8M is stable or the startup time is longer than IRC8M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC8MSTB);
    }while((0U == stab_flag) && (IRC8M_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)){
        while(1){
        }
    }

    /* LDO output voltage high mode */
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* IRC8M is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

    /* CK_PLL = (CK_IRC8M/2) * 30 = 120 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5);
    RCU_CFG0 |= RCU_PLL_MUL30;

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }
    
    /* enable the high-drive to extend the clock frequency to 120 MHz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }
    
    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    }
    
    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_HXTAL)
/*!
    \brief      configure the system clock to HXTAL
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;
    
    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));
    
    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }
    
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;
    
    /* select HXTAL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_HXTAL;
    
    /* wait until HXTAL is selected as system clock */
    while(0 == (RCU_CFG0 & RCU_SCSS_HXTAL)){
    }
}

#elif defined (__SYSTEM_CLOCK_48M_PLL_HXTAL)
/*!
    \brief      configure the system clock to 48M by PLL which selects HXTAL(8M) as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_48m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;

    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }

    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

#if (defined(GD32F30X_HD) || defined(GD32F30X_XD))
    /* select HXTAL/2 as clock source */
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL_IRC48M | RCU_CFG0_PREDV0);

    /* CK_PLL = (CK_HXTAL/2) * 12 = 48 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5);
    RCU_CFG0 |= RCU_PLL_MUL12;

#elif defined(GD32F30X_CL)
    /* CK_PLL = (CK_PREDIV0) * 12 = 48 MHz */ 
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL_IRC48M | RCU_PLL_MUL12);

    /* CK_PREDIV0 = (CK_HXTAL)/5 *8 /10 = 4 MHz */ 
    RCU_CFG1 &= ~(RCU_CFG1_PLLPRESEL | RCU_CFG1_PREDV0SEL | RCU_CFG1_PLL1MF | RCU_CFG1_PREDV1 | RCU_CFG1_PREDV0);
    RCU_CFG1 |= (RCU_PLLPRESRC_HXTAL | RCU_PREDV0SRC_CKPLL1 | RCU_PLL1_MUL8 | RCU_PREDV1_DIV5 | RCU_PREDV0_DIV10);

    /* enable PLL1 */
    RCU_CTL |= RCU_CTL_PLL1EN;
    /* wait till PLL1 is ready */
    while((RCU_CTL & RCU_CTL_PLL1STB) == 0){
    }
#endif /* GD32F30X_HD and GD32F30X_XD */

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* enable the high-drive to extend the clock frequency to 120 MHz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }

    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    }

    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}
#elif defined (__SYSTEM_CLOCK_72M_PLL_HXTAL)
/*!
    \brief      configure the system clock to 72M by PLL which selects HXTAL(8M) as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_72m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;

    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }

    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

#if (defined(GD32F30X_HD) || defined(GD32F30X_XD))
    /* select HXTAL/2 as clock source */
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL_IRC48M | RCU_CFG0_PREDV0);

    /* CK_PLL = (CK_HXTAL/2) * 18 = 72 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5);
    RCU_CFG0 |= RCU_PLL_MUL18;

#elif defined(GD32F30X_CL)
    /* CK_PLL = (CK_PREDIV0) * 18 = 72 MHz */ 
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL_IRC48M | RCU_PLL_MUL18);

    /* CK_PREDIV0 = (CK_HXTAL)/5 *8 /10 = 4 MHz */ 
    RCU_CFG1 &= ~(RCU_CFG1_PLLPRESEL | RCU_CFG1_PREDV0SEL | RCU_CFG1_PLL1MF | RCU_CFG1_PREDV1 | RCU_CFG1_PREDV0);
    RCU_CFG1 |= (RCU_PLLPRESRC_HXTAL | RCU_PREDV0SRC_CKPLL1 | RCU_PLL1_MUL8 | RCU_PREDV1_DIV5 | RCU_PREDV0_DIV10);

    /* enable PLL1 */
    RCU_CTL |= RCU_CTL_PLL1EN;
    /* wait till PLL1 is ready */
    while((RCU_CTL & RCU_CTL_PLL1STB) == 0){
    }
#endif /* GD32F30X_HD and GD32F30X_XD */

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* enable the high-drive to extend the clock frequency to 120 MHz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }

    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    }

    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_108M_PLL_HXTAL)
/*!
    \brief      configure the system clock to 108M by PLL which selects HXTAL(8M) as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_108m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;

    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }

    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

#if (defined(GD32F30X_HD) || defined(GD32F30X_XD))
    /* select HXTAL/2 as clock source */
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL_IRC48M | RCU_CFG0_PREDV0);

    /* CK_PLL = (CK_HXTAL/2) * 27 = 108 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5);
    RCU_CFG0 |= RCU_PLL_MUL27;

#elif defined(GD32F30X_CL)
    /* CK_PLL = (CK_PREDIV0) * 27 = 108 MHz */ 
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL_IRC48M | RCU_PLL_MUL27);

    /* CK_PREDIV0 = (CK_HXTAL)/5 *8 /10 = 4 MHz */ 
    RCU_CFG1 &= ~(RCU_CFG1_PLLPRESEL | RCU_CFG1_PREDV0SEL | RCU_CFG1_PLL1MF | RCU_CFG1_PREDV1 | RCU_CFG1_PREDV0);
    RCU_CFG1 |= (RCU_PLLPRESRC_HXTAL | RCU_PREDV0SRC_CKPLL1 | RCU_PLL1_MUL8 | RCU_PREDV1_DIV5 | RCU_PREDV0_DIV10);

    /* enable PLL1 */
    RCU_CTL |= RCU_CTL_PLL1EN;
    /* wait till PLL1 is ready */
    while((RCU_CTL & RCU_CTL_PLL1STB) == 0){
    }
#endif /* GD32F30X_HD and GD32F30X_XD */

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* enable the high-drive to extend the clock frequency to 120 MHz */
    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)){
    }

    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)){
    }

    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_120M_PLL_HXTAL)
/*!
    \brief      configure the system clock to 120M by PLL which selects HXTAL(8M) as its clock source
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void system_clock_120m_hxtal(void)
{
    uint32_t diag_timeout;
    uint16_t hxtal_elapsed_ms = 0U;

    clock_diag_record(CLOCK_DIAG_STAGE_CLOCK_CONFIG_ENTER, 0U);

    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;
    clock_diag_record(CLOCK_DIAG_STAGE_HXTAL_WAIT, 0U);

    /* Wait up to 3000 ms so a slow cold-start crystal can be distinguished. */
    if(0U == clock_diag_wait_hxtal(CLOCK_DIAG_HXTAL_TIMEOUT_MS,
                                   &hxtal_elapsed_ms)){
        clock_diag_record(CLOCK_DIAG_STAGE_HXTAL_TIMEOUT,
                          hxtal_elapsed_ms);
        while(1){
            __NOP();
        }
    }

    clock_diag_record(CLOCK_DIAG_STAGE_HXTAL_READY,
                      hxtal_elapsed_ms);

    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

#if (defined(GD32F30X_HD) || defined(GD32F30X_XD))
    /* select HXTAL/2 as clock source */
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL_IRC48M | RCU_CFG0_PREDV0);

    /* CK_PLL = (CK_HXTAL/2) * 30 = 120 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5);
    RCU_CFG0 |= RCU_PLL_MUL30;

#elif defined(GD32F30X_CL)
    /* CK_PLL = (CK_PREDIV0) * 30 = 120 MHz */ 
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL_IRC48M | RCU_PLL_MUL30);

    /* CK_PREDIV0 = (CK_HXTAL)/5 *8 /10 = 4 MHz */ 
    RCU_CFG1 &= ~(RCU_CFG1_PLLPRESEL | RCU_CFG1_PREDV0SEL | RCU_CFG1_PLL1MF | RCU_CFG1_PREDV1 | RCU_CFG1_PREDV0);
    RCU_CFG1 |= (RCU_PLLPRESRC_HXTAL | RCU_PREDV0SRC_CKPLL1 | RCU_PLL1_MUL8 | RCU_PREDV1_DIV5 | RCU_PREDV0_DIV10);

    /* enable PLL1 */
    RCU_CTL |= RCU_CTL_PLL1EN;
    /* wait till PLL1 is ready */
    diag_timeout = CLOCK_DIAG_FLAG_WAIT_LOOPS;
    while(((RCU_CTL & RCU_CTL_PLL1STB) == 0U) &&
          (diag_timeout > 0U)){
        diag_timeout--;
    }
    if(0U == (RCU_CTL & RCU_CTL_PLL1STB)){
        clock_diag_record(CLOCK_DIAG_STAGE_PLL_TIMEOUT,
                          hxtal_elapsed_ms);
        while(1){
            __NOP();
        }
    }
#endif /* GD32F30X_HD and GD32F30X_XD */

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;
    clock_diag_record(CLOCK_DIAG_STAGE_PLL_WAIT,
                      hxtal_elapsed_ms);

    /* wait until PLL is stable */
    diag_timeout = CLOCK_DIAG_FLAG_WAIT_LOOPS;
    while((0U == (RCU_CTL & RCU_CTL_PLLSTB)) &&
          (diag_timeout > 0U)){
        diag_timeout--;
    }
    if(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
        clock_diag_record(CLOCK_DIAG_STAGE_PLL_TIMEOUT,
                          hxtal_elapsed_ms);
        while(1){
            __NOP();
        }
    }
    clock_diag_record(CLOCK_DIAG_STAGE_PLL_READY,
                      hxtal_elapsed_ms);

    /* enable the high-drive to extend the clock frequency to 120 MHz */
    PMU_CTL |= PMU_CTL_HDEN;
    clock_diag_record(CLOCK_DIAG_STAGE_HIGH_DRIVE_WAIT,
                      hxtal_elapsed_ms);
    diag_timeout = CLOCK_DIAG_FLAG_WAIT_LOOPS;
    while((0U == (PMU_CS & PMU_CS_HDRF)) &&
          (diag_timeout > 0U)){
        diag_timeout--;
    }
    if(0U == (PMU_CS & PMU_CS_HDRF)){
        clock_diag_record(CLOCK_DIAG_STAGE_HIGH_DRIVE_TIMEOUT,
                          hxtal_elapsed_ms);
        while(1){
            __NOP();
        }
    }
    clock_diag_record(CLOCK_DIAG_STAGE_HIGH_DRIVE_READY,
                      hxtal_elapsed_ms);

    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    clock_diag_record(CLOCK_DIAG_STAGE_HIGH_SWITCH_WAIT,
                      hxtal_elapsed_ms);
    diag_timeout = CLOCK_DIAG_FLAG_WAIT_LOOPS;
    while((0U == (PMU_CS & PMU_CS_HDSRF)) &&
          (diag_timeout > 0U)){
        diag_timeout--;
    }
    if(0U == (PMU_CS & PMU_CS_HDSRF)){
        clock_diag_record(CLOCK_DIAG_STAGE_HIGH_SWITCH_TIMEOUT,
                          hxtal_elapsed_ms);
        while(1){
            __NOP();
        }
    }
    clock_diag_record(CLOCK_DIAG_STAGE_HIGH_SWITCH_READY,
                      hxtal_elapsed_ms);

    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;
    clock_diag_record(CLOCK_DIAG_STAGE_SYSCLK_SWITCH_WAIT,
                      hxtal_elapsed_ms);

    /* wait until PLL is selected as system clock */
    diag_timeout = CLOCK_DIAG_FLAG_WAIT_LOOPS;
    while(((RCU_CFG0 & RCU_CFG0_SCSS) != RCU_SCSS_PLL) &&
          (diag_timeout > 0U)){
        diag_timeout--;
    }
    if((RCU_CFG0 & RCU_CFG0_SCSS) != RCU_SCSS_PLL){
        clock_diag_record(CLOCK_DIAG_STAGE_SYSCLK_SWITCH_TIMEOUT,
                          hxtal_elapsed_ms);
        while(1){
            __NOP();
        }
    }
    clock_diag_record(CLOCK_DIAG_STAGE_SYSCLK_SWITCH_READY,
                      hxtal_elapsed_ms);
    clock_diag_record(CLOCK_DIAG_STAGE_COMPLETE,
                      hxtal_elapsed_ms);
}
#endif /* __SYSTEM_CLOCK_IRC8M */

/*!
    \brief      update the SystemCoreClock with current core clock retrieved from cpu registers
    \param[in]  none
    \param[out] none
    \retval     none
*/
void SystemCoreClockUpdate(void)
{
    uint32_t sws;
    uint32_t pllsel, pllpresel, predv0sel, pllmf, ck_src, idx, clk_exp;
    /* exponent of AHB, APB1 and APB2 clock divider */
    const uint8_t ahb_exp[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
#ifdef GD32F30X_CL
    uint32_t predv0, predv1, pll1mf;
#endif /* GD32F30X_CL */

    sws = GET_BITS(RCU_CFG0, 2, 3);
    switch(sws){
    /* IRC8M is selected as CK_SYS */
    case SEL_IRC8M:
        SystemCoreClock = IRC8M_VALUE;
        break;
    /* HXTAL is selected as CK_SYS */
    case SEL_HXTAL:
        SystemCoreClock = HXTAL_VALUE;
        break;
    /* PLL is selected as CK_SYS */
    case SEL_PLL:
        /* PLL clock source selection, HXTAL, IRC48M or IRC8M/2 */
        pllsel = (RCU_CFG0 & RCU_CFG0_PLLSEL);

        if(RCU_PLLSRC_HXTAL_IRC48M == pllsel){
            /* PLL clock source is HXTAL or IRC48M */
            pllpresel = (RCU_CFG1 & RCU_CFG1_PLLPRESEL);
            
            if(RCU_PLLPRESRC_HXTAL == pllpresel){
                /* PLL clock source is HXTAL */
                ck_src = HXTAL_VALUE;
            }else{
                /* PLL clock source is IRC48 */
                ck_src = IRC48M_VALUE;
            }

#if (defined(GD32F30X_HD) || defined(GD32F30X_XD))
            predv0sel = (RCU_CFG0 & RCU_CFG0_PREDV0);
            /* PREDV0 input source clock divided by 2 */
            if(RCU_CFG0_PREDV0 == predv0sel){
                ck_src /= 2U;
            }
#elif defined(GD32F30X_CL)
            predv0sel = (RCU_CFG1 & RCU_CFG1_PREDV0SEL);
            /* source clock use PLL1 */
            if(RCU_PREDV0SRC_CKPLL1 == predv0sel){
                predv1 = ((RCU_CFG1 & RCU_CFG1_PREDV1) >> 4) + 1U;
                pll1mf = ((RCU_CFG1 & RCU_CFG1_PLL1MF) >> 8) + 2U;
                if(17U == pll1mf){
                    pll1mf = 20U;
                }
                ck_src = (ck_src / predv1) * pll1mf;
            }
            predv0 = (RCU_CFG1 & RCU_CFG1_PREDV0) + 1U;
            ck_src /= predv0;
#endif /* GD32F30X_HD and GD32F30X_XD */
        }else{
            /* PLL clock source is IRC8M/2 */
            ck_src = IRC8M_VALUE / 2U;
        }

        /* PLL multiplication factor */
        pllmf = GET_BITS(RCU_CFG0, 18, 21);

        if((RCU_CFG0 & RCU_CFG0_PLLMF_4)){
            pllmf |= 0x10U;
        }
        if((RCU_CFG0 & RCU_CFG0_PLLMF_5)){
            pllmf |= 0x20U;
        }

        if( pllmf >= 15U){
            pllmf += 1U;
        }else{
            pllmf += 2U;
        }
        if(pllmf > 61U){
            pllmf = 63U;
        }
        SystemCoreClock = ck_src * pllmf;
    #ifdef GD32F30X_CL
        if(15U == pllmf){
            SystemCoreClock = (ck_src * 6U) + (ck_src / 2U);
        }
    #endif /* GD32F30X_CL */
        break;
    /* IRC8M is selected as CK_SYS */
    default:
        SystemCoreClock = IRC8M_VALUE;
        break;
    }

    /* calculate AHB clock frequency */
    idx = GET_BITS(RCU_CFG0, 4, 7);
    clk_exp = ahb_exp[idx];
    SystemCoreClock = SystemCoreClock >> clk_exp;
}
