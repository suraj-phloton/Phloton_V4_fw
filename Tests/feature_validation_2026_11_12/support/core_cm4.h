#ifndef TEST_CORE_CM4_H
#define TEST_CORE_CM4_H
#include <stdint.h>
#define __I volatile const
#define __O volatile
#define __IO volatile
#define __IM volatile const
#define __OM volatile
#define __IOM volatile
typedef struct { volatile uint32_t CTRL, LOAD, VAL, CALIB; } SysTick_Type;
typedef struct { volatile uint32_t CPACR; } SCB_Type;
typedef struct { volatile uint32_t CTRL, CYCCNT; } DWT_Type;
typedef struct { volatile uint32_t DHCSR,DCRSR,DCRDR,DEMCR; } CoreDebug_Type;
#define SysTick ((SysTick_Type *)0xE000E010UL)
#define SCB ((SCB_Type *)0xE000ED00UL)
#define DWT ((DWT_Type *)0xE0001000UL)
#define CoreDebug ((CoreDebug_Type *)0xE000EDF0UL)
#define CoreDebug_DEMCR_TRCENA_Msk (1UL<<24)
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define SysTick_CTRL_CLKSOURCE_Msk 4U
#define SysTick_CTRL_TICKINT_Msk 2U
#define SysTick_CTRL_ENABLE_Msk 1U
extern uint32_t test_primask;
static inline uint32_t __get_PRIMASK(void){return test_primask;}
static inline void __disable_irq(void){test_primask=1;}
static inline void __enable_irq(void){test_primask=0;}
static inline void __NOP(void){}
static inline void __DMB(void){}
static inline void __DSB(void){}
static inline void __ISB(void){}
#endif
