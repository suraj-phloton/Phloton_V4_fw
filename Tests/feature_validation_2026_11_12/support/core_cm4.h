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
/* Trace register offsets match the real CMSIS Cortex-M4 definitions. */
typedef union { volatile uint8_t u8; volatile uint16_t u16; volatile uint32_t u32; } TestITM_Port;
typedef struct {
    TestITM_Port PORT[32]; uint32_t reserved0[864];
    volatile uint32_t TER; uint32_t reserved1[15];
    volatile uint32_t TPR; uint32_t reserved2[15];
    volatile uint32_t TCR; uint32_t reserved3[75];
    volatile uint32_t LAR;
} ITM_Type;
typedef struct {
    uint32_t reserved0[4]; volatile uint32_t ACPR;
    uint32_t reserved1[55]; volatile uint32_t SPPR;
    uint32_t reserved2[132]; volatile uint32_t FFCR;
} TPI_Type;
#define ITM ((ITM_Type *)0xE0000000UL)
#define TPI ((TPI_Type *)0xE0040000UL)
#define CoreDebug_DHCSR_C_DEBUGEN_Msk 1U
#define ITM_TCR_TraceBusID_Pos 16U
#define ITM_TCR_SWOENA_Msk (1U << 4)
#define ITM_TCR_SYNCENA_Msk (1U << 2)
#define ITM_TCR_ITMENA_Msk 1U
extern uint32_t test_primask;
static inline uint32_t __get_PRIMASK(void){return test_primask;}
static inline void __disable_irq(void){test_primask=1;}
static inline void __enable_irq(void){test_primask=0;}
static inline void __NOP(void){}
static inline void __DMB(void){}
static inline void __DSB(void){}
static inline void __ISB(void){}
#endif
