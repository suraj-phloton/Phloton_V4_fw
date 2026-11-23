#include "phloton_swo.h"
#include "stm32l451xx.h"
#include <stddef.h>

#ifndef PH_SWO_READY
#define PH_SWO_READY() (ITM->PORT[0].u32 != 0U)
#endif
#ifndef PH_SWO_BYTE_HOOK
#define PH_SWO_BYTE_HOOK(byte) ((void)0)
#endif

extern uint32_t SystemCoreClock;
volatile uint32_t ph_swo_initialized, ph_swo_bytes_sent;
volatile uint32_t ph_swo_bytes_dropped, ph_swo_writes_dropped;
volatile uint32_t ph_swo_queue_high_water;
static uint8_t queue[PH_SWO_QUEUE_BYTES];
static uint32_t queue_head, queue_count;

static uint8_t trace_active(void)
{
    return (uint8_t)(ph_swo_initialized &&
        (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) &&
        (CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) &&
        (ITM->TCR & ITM_TCR_ITMENA_Msk) && (ITM->TER & 1U));
}

static void discard_queue(void)
{
    ph_swo_bytes_dropped += queue_count;
    queue_head = queue_count = 0U;
}

void ph_swo_init(void)
{
    discard_queue();
    ph_swo_initialized = 0U;
#if PH_SWO_PIN_APPROVED
    if (SystemCoreClock != 80000000U)
        return;
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    (void)RCC->AHB2ENR;
    GPIOB->MODER = (GPIOB->MODER & ~(3U << 6U)) | (2U << 6U);
    GPIOB->OTYPER &= ~(1U << 3U);
    GPIOB->OSPEEDR |= 3U << 6U;
    GPIOB->PUPDR &= ~(3U << 6U);
    GPIOB->AFR[0] &= ~(15U << 12U); /* PB3 AF0 = TRACESWO. */
    DBGMCU->CR = (DBGMCU->CR & ~DBGMCU_CR_TRACE_MODE) | DBGMCU_CR_TRACE_IOEN;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    TPI->ACPR = (80000000U / PH_SWO_HZ) - 1U;
    TPI->SPPR = 2U; /* Asynchronous NRZ. */
    TPI->FFCR = 0x100U;
    ITM->LAR = 0xC5ACCE55U;
    ITM->TCR = (1U << ITM_TCR_TraceBusID_Pos) | ITM_TCR_SWOENA_Msk |
               ITM_TCR_SYNCENA_Msk | ITM_TCR_ITMENA_Msk;
    ITM->TER |= 1U;
    ph_swo_initialized = 1U;
#endif
}

uint8_t ph_swo_write(const char *data, uint32_t length)
{
    if (length == 0U)
        return 1U;
    if (!trace_active())
        discard_queue();
    if (data == NULL || length > PH_SWO_QUEUE_BYTES || !trace_active() ||
        length > PH_SWO_QUEUE_BYTES - queue_count)
    {
        ph_swo_bytes_dropped += length;
        ph_swo_writes_dropped++;
        return 0U;
    }
    /* Reserve the complete message before copying; overflow drops no prefix. */
    for (uint32_t i = 0U; i < length; ++i)
        queue[(queue_head + queue_count + i) % PH_SWO_QUEUE_BYTES] = (uint8_t)data[i];
    queue_count += length;
    if (queue_count > ph_swo_queue_high_water)
        ph_swo_queue_high_water = queue_count;
    return 1U;
}

void ph_swo_service(void)
{
    if (!trace_active())
    {
        discard_queue();
        return;
    }
    /* No readiness wait, timeouts, interrupt masking, or transport retries. */
    for (uint32_t sent = 0U; sent < PH_SWO_SERVICE_BYTES && queue_count; ++sent)
    {
        if (!trace_active())
        {
            discard_queue();
            return;
        }
        if (!PH_SWO_READY())
            return;
        uint8_t byte = queue[queue_head];
        ITM->PORT[0].u8 = byte;
        PH_SWO_BYTE_HOOK(byte);
        queue_head = (queue_head + 1U) % PH_SWO_QUEUE_BYTES;
        queue_count--;
        ph_swo_bytes_sent++;
    }
}

int __io_putchar(int character)
{
    const char byte = (char)character;
    return ph_swo_write(&byte, 1U) ? (unsigned char)byte : -1;
}
