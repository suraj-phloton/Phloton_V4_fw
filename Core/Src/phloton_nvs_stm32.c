#include "phloton_nvs.h"
#include "stm32l451xx.h"
#include <string.h>

#define NVS_ERRORS (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                    FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | FLASH_SR_RDERR)
static volatile uint8_t nvs_reading, nvs_ecc_error;

/* NMI may recover only a data ECC error in the reserved calibration region. */
int ph_nvs_ecc_nmi(void)
{
    uint32_t ecc = FLASH->ECCR;
    uint32_t address = FLASH_BASE + (ecc & FLASH_ECCR_ADDR_ECC);
    if (!nvs_reading || !(ecc & FLASH_ECCR_ECCD) || address < PH_NVS_BASE ||
        address >= PH_NVS_BASE + 2U * PH_NVS_PAGE_BYTES) return 0;
    nvs_ecc_error = 1U;
    FLASH->ECCR |= FLASH_ECCR_ECCD | FLASH_ECCR_ECCC;
    return 1;
}

static int nvs_read(void *context, unsigned slot, unsigned offset, void *out, size_t count)
{
    uint32_t saved;
    volatile const uint32_t *src;
    uint32_t *dst = out;
    (void)context;
    if (slot > 1U || offset > PH_NVS_PAGE_BYTES || count > PH_NVS_PAGE_BYTES - offset ||
        offset % 4U || count % 4U || !out || FLASH_SIZE < 512U * 1024U) return 0;
    saved = __get_PRIMASK();
    __disable_irq();
    src = (volatile const uint32_t *)(uintptr_t)(PH_NVS_BASE + slot * PH_NVS_PAGE_BYTES + offset);
    nvs_ecc_error = 0U;
    nvs_reading = 1U;
    __DMB();
    for (size_t i = 0; i < count / 4U; ++i) dst[i] = src[i];
    __DMB();
    nvs_reading = 0U;
    if (!saved) __enable_irq();
    return !nvs_ecc_error;
}

static __attribute__((section(".RamFunc"), noinline)) int flash_wait(void)
{
    uint32_t start = DWT->CYCCNT, guard = 4000000U;
    while (FLASH->SR & FLASH_SR_BSY)
        if ((uint32_t)(DWT->CYCCNT - start) >= 8000000U || --guard == 0U) return 0;
    return (FLASH->SR & NVS_ERRORS) == 0U;
}

static int flash_open(void)
{
    if (FLASH_SIZE < 512U * 1024U || !flash_wait()) return 0;
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123U;
        FLASH->KEYR = 0xCDEF89ABU;
    }
    FLASH->SR = NVS_ERRORS | FLASH_SR_EOP;
    return !(FLASH->CR & FLASH_CR_LOCK);
}

static int nvs_erase(void *context, unsigned slot)
{
    int ok;
    uint32_t saved;
    (void)context;
    if (slot > 1U || !flash_open()) return 0;
    saved = __get_PRIMASK(); __disable_irq();
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PNB) | FLASH_CR_PER |
                ((254U + slot) << FLASH_CR_PNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    ok = flash_wait();
    FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_PNB);
    FLASH->CR |= FLASH_CR_LOCK;
    if (!saved) __enable_irq();
    return ok;
}

static int nvs_program(void *context, unsigned slot, unsigned offset, const void *data)
{
    uint32_t words[2], saved, cache;
    volatile uint32_t *dst;
    int ok;
    (void)context;
    if (slot > 1U || offset > PH_NVS_PAGE_BYTES - 8U || offset % 8U || !data || !flash_open()) return 0;
    memcpy(words, data, sizeof words);
    dst = (volatile uint32_t *)(uintptr_t)(PH_NVS_BASE + slot * PH_NVS_PAGE_BYTES + offset);
    saved = __get_PRIMASK(); __disable_irq();
    FLASH->CR |= FLASH_CR_PG;
    dst[0] = words[0];
    __ISB();
    dst[1] = words[1];
    ok = flash_wait();
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;
    /* Flush read caches before verifying a newly programmed record. */
    cache = FLASH->ACR & (FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    FLASH->ACR &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    FLASH->ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    FLASH->ACR |= cache;
    if (!saved) __enable_irq();
    return ok;
}

const ph_nvs_io_t ph_nvs_stm32 = {0, nvs_read, nvs_erase, nvs_program};
