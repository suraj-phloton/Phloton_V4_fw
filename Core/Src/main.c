
#include "stm32l451xx.h"
#include <stdint.h>
#include <math.h>
#include "phloton_features.h"
#include "phloton_nvs.h"

/* V4 PCB: Q21 open-collector detector, R126 pull-up. Override only after a board measurement. */
#ifndef PH_LID_OPEN_LEVEL
#define PH_LID_OPEN_LEVEL 1U
#endif
static ph_temperatures_t temperatures;
static ph_calibration_t battery_calibration;
static ph_battery_sampler_t battery_sampler;
static ph_lid_t feature_lid;
static ph_lid_t feature_alarm_lid;
static volatile ph_buzzer_t feature_buzzer;
static uint8_t hot_inhibited;
static volatile uint8_t feature_irq_pause, feature_adc_fault, feature_irq_valid, feature_irq_hot = 1U;
static volatile uint32_t feature_irq_sample_ms;
static uint8_t adc_last_ok;
volatile uint32_t adc_timeout_count, calibration_status, calibration_sequence;
volatile uint32_t calibration_save_request;
volatile ph_calibration_t calibration_pending;
volatile float battery_soc_percent;
static void Features_InterruptTick(void);
static void Features_Service(void);
static void Features_Pause(void);
static int Features_Ready(void);
static void Features_BeginStartup(void);
static void Features_Beep(uint8_t kind);
#ifndef PH_ADC_POLL_HOOK
#define PH_ADC_POLL_HOOK(reg, mask, expected) ((void)0)
#endif
#ifndef PH_UART_BYTE_HOOK
#define PH_UART_BYTE_HOOK(byte) ((void)0)
#endif

extern uint32_t SystemCoreClock;

#define LM51772_ADDR             0x6AU

#define TARGET_VOUT_V            13.0f
#define REG_ILIM_THRESHOLD       0x0AU
#define REG_CLEAR_FAULTS         0x03U

#define REG_VOUT_LSB             0x0CU
#define REG_VOUT_MSB             0x0DU
#define REG_STATUS_BYTE          0x78U
#define REG_USB_PD_CONTROL       0x81U
#define REG_MFR_D0               0xD0U
#define REG_MFR_D1               0xD1U
#define REG_MFR_D2               0xD2U
#define REG_MFR_D3               0xD3U
#define REG_MFR_D4               0xD4U
#define REG_MFR_D9               0xD9U

#define ADC_MAX_COUNT          4095.0f
#define ADC_VREF                 3.3f

#define BATT_DIVIDER_RATIO  11.0f
#define TEC_DIVIDER_RATIO   11.0f

#define BATT_VSNS_CHANNEL   5U
#define BATT_ISNS_CHANNEL   6U
#define TEC_ISNS_CHANNEL   15U
#define TEC_VSNS_CHANNEL   16U


/* ACS71240 sensitivity */
#define BATT_CURRENT_ZERO_V  1.65f
#define BATT_CURRENT_SENS_VA 0.132f


/* TEC current sense*/
#define TEC_SHUNT_OHMS       0.005f
#define TEC_AMP_GAIN         50.0f

/* ---------------- FAN CURRENT SENSE ----------------
 * Fill these in from the schematic, then set FAN_ISNS_ENABLE to 1.
 *   *_ISNS_PORT / *_ISNS_PIN : MCU pin the fan current sense output goes to
 *   *_ISNS_CHANNEL           : its ADC1 channel number (STM32L451 pin table)
 *   *_ISNS_ZERO_V            : output at 0 A (0 V for INA181)
 *   *_ISNS_V_PER_A           : 50 mOhm x 100 (INA181A3) = 5.0 V/A per schematic
 */
#define FAN_ISNS_ENABLE       0U   /* Until real pins and calibration are confirmed. */
#if FAN_ISNS_ENABLE
#error "Existing fan placeholders conflict with the five temperature inputs; use verified reworked pins."
#endif
#if PH_LID_OPEN_LEVEL != 0 && PH_LID_OPEN_LEVEL != 1
#error "PH_LID_OPEN_LEVEL must be 0 or 1"
#endif



/* Cold-side fan sense - FILL FROM SCHEMATIC */
#define CSFAN_ISNS_PORT       GPIOC
#define CSFAN_ISNS_PIN        0U
#define CSFAN_ISNS_CHANNEL    1U
#define CSFAN_ISNS_ZERO_V     0.0f
#define CSFAN_ISNS_V_PER_A    1.0f

/* Hot-side fan sense - FILL FROM SCHEMATIC */
#define HSFAN_ISNS_PORT       GPIOC
#define HSFAN_ISNS_PIN        2U
#define HSFAN_ISNS_CHANNEL    3U
#define HSFAN_ISNS_ZERO_V     0.0f
#define HSFAN_ISNS_V_PER_A    1.0f

/* ---------------- GPIO PINS (from LED code) ---------------- */      /* NEW block */

#define LID_OPEN_PIN          7U    /* PC7 */
#define CHARGER_LOCK_PIN      8U    /* PC8 */
#define WAKEUP_SW_PIN         9U    /* PA9 */

/* 74HC595 shift register */
#define SR_SER_PIN            14U   /* PB14 */
#define SR_SRCLK_PIN          15U   /* PB15 */
#define SR_RCLK_PIN            6U   /* PC6 */

/* WS2812B */
#define WS2812B_PIN            8U   /* PA8 */
#define WS2812B_MASK           (1UL << WS2812B_PIN)

#define NEO_BRIGHTNESS        64U   /* 0..255, 255 = full bright */
#define WS2812B_MIN_CPU_MHZ   48U

#define LED_GREEN_TEMP_C      8.0f  /* NEW: red above this, green at or below */
#define BUTTON_DEBOUNCE_MS    30U   /* NEW */

volatile uint32_t tec_hot_low_readback = 0U;

/* Cached ILIM code (0x0A = 0.50 A). Updated by the PI loop. */
volatile uint8_t ILIM_THRESHOLD_VALUE = 0x0AU;

volatile uint32_t firmware_stage = 0;
volatile uint32_t firmware_error = 0;

volatile uint8_t lm51772_ack = 0;
volatile uint8_t lm51772_init_ok = 0;
volatile uint8_t lm51772_converter_enabled = 0;
volatile uint8_t lm51772_nfault_pin = 1;
volatile uint8_t lm51772_reset_pin = 0;

volatile uint16_t batt_vsns_adc_raw = 0U;
volatile uint16_t batt_isns_adc_raw = 0U;
volatile float batt_voltage = 0.0f;
volatile float batt_current = 0.0f;

/* Register readbacks */
volatile uint8_t rb_mfr_d0 = 0;
volatile uint8_t rb_mfr_d1 = 0;
volatile uint8_t rb_mfr_d2 = 0;
volatile uint8_t rb_mfr_d3 = 0;
volatile uint8_t rb_mfr_d4 = 0;
volatile uint8_t rb_mfr_d9 = 0;
volatile uint8_t rb_usb_pd_control = 0;
volatile uint8_t rb_ilim = 0;
volatile uint8_t rb_vout_lsb = 0;
volatile uint8_t rb_vout_msb = 0;
volatile uint8_t rb_status_byte = 0;
volatile uint8_t rb_status_input = 0;
volatile uint16_t rb_status_word = 0;
volatile uint16_t rb_vout_code = 0;
volatile float rb_vout_voltage_v = 0.0f;


/* Battery ADC readbacks */
volatile uint16_t batt_vsns_adc_raw;
volatile uint16_t batt_isns_adc_raw;
volatile float batt_vsns_adc_voltage;
volatile float batt_isns_adc_voltage;
volatile float batt_voltage;
volatile float batt_current;

/* TEC ADC readbacks */
volatile uint16_t tec_vsns_adc_raw;
volatile uint16_t tec_isns_adc_raw;
volatile float tec_vsns_adc_voltage;
volatile float tec_isns_adc_voltage;
volatile float tec_voltage;
volatile float tec_current;

/* Fan ADC readbacks  */
volatile uint16_t cs_fan_isns_adc_raw = 0U;
volatile float cs_fan_isns_adc_voltage = 0.0f;
volatile float cs_fan_current = 0.0f;

volatile uint16_t hs_fan_isns_adc_raw = 0U;
volatile float hs_fan_isns_adc_voltage = 0.0f;
volatile float hs_fan_current = 0.0f;
///////////////////////////////////////////

/* ADC raw measurements */
volatile uint16_t adc_battery_voltage_raw = 0;
volatile uint16_t adc_battery_current_raw = 0;
volatile uint16_t adc_tec_current_raw = 0;
volatile uint16_t adc_tec_voltage_raw = 0;

/* Converted measurements */
volatile float battery_voltage_adc_v = 0.0f;
volatile float battery_current_sensor_v = 0.0f;
volatile float tec_current_sensor_v = 0.0f;
volatile float tec_voltage_adc_v = 0.0f;

volatile float battery_voltage_v = 0.0f;
volatile float battery_current_a = 0.0f;
volatile float tec_voltage_v = 0.0f;
volatile float tec_current_a = 0.0f;


volatile uint16_t tec_vsns_adc_raw = 0;
volatile float tec_vsns_adc_voltage = 0.0f;
volatile float tec_voltage = 0.0f;

/* ---------------- LED / button variables (from LED code) ---------------- */

volatile uint32_t lid_open_pin_state = 0U;
volatile uint32_t charger_lock_pin_state = 0U;
volatile uint32_t wakeup_sw_pin_state = 1U;

volatile uint32_t gpioa_input_snapshot = 0U;
volatile uint32_t gpioc_input_snapshot = 0U;

volatile uint32_t led_is_on = 0U;
volatile uint8_t  led_pattern = 0U;

volatile uint32_t ws2812b_is_on = 0U;
volatile uint32_t ws2812b_blink_count = 0U;
volatile uint32_t ws2812b_clock_too_slow = 0U;  /* 1 = CPU clock too low, LED not driven */

volatile uint32_t ledState = 0U;                /* CHANGED meaning: 1 = system ON, 0 = system OFF */

/* neoColorIndex now holds the colour currently shown: 0 = Red, 1 = Green */
volatile uint8_t neoColorIndex = 0U;

volatile uint32_t button_previous_state = 1U;
volatile uint32_t button_current_state = 1U;

volatile uint32_t button_raw_last = 1U;         /* NEW: debounce */
volatile uint32_t button_raw_change_ms = 0U;    /* NEW: debounce */

/* ---- BUTTON DIAGNOSTICS (watch these in Live Expressions) ----
 * wakeup_sw_pin_state    raw PA9 level right now: 1 = released, 0 = pressed
 * btn_pin_low_seen       becomes 1 the first time PA9 is ever read LOW (pin sees the button)
 * btn_raw_transitions    every raw level change seen on PA9 (counts contact bounce too)
 * btn_press_count        presses accepted after the 30 ms debounce (each one toggles ON/OFF)
 * btn_release_count      releases accepted after the debounce
 * btn_last_press_ms      ms_ticks value of the last accepted press
 * ms_ticks               must keep counting up (~1000 per second), the debounce needs it     */
volatile uint32_t btn_raw_transitions = 0U;
volatile uint32_t btn_press_count     = 0U;
volatile uint32_t btn_release_count   = 0U;
volatile uint32_t btn_last_press_ms   = 0U;
volatile uint8_t  btn_pin_low_seen    = 0U;

/* RGB colours in RGB order */
static const uint8_t colors[6][3] =
{
    {255U,   0U,   0U},  /* Red   (index 0 - used) */
    {  0U, 255U,   0U},  /* Green (index 1 - used) */
    {  0U,   0U, 255U},  /* Blue */
    {255U, 255U,   0U},  /* Yellow */
    {  0U, 255U, 255U},  /* Cyan */
    {255U,   0U, 255U}   /* Magenta */
};

///////////////////////////////////////////////////
static void ADC1_Init(void);
static uint16_t ADC1_Read(uint32_t channel);
static int ADC1_Wait(volatile uint32_t *reg, uint32_t mask, uint32_t expected);
static float ADC_ToVoltage(uint16_t adc);

float BATT_ReadVoltage(void);
float BATT_ReadCurrent(void);
float TEC_ReadVoltage(void);
float TEC_ReadCurrent(void);
float CSFAN_ReadCurrent(void);
float HSFAN_ReadCurrent(void);
void Measurements_Update(void);

/* ---------------- CLOCK ---------------- */
/*
 * Step 1: SystemClock_Config()  -> SYSCLK = MSI 4 MHz (unchanged from PI code)
 * Step 2: Clock_Init()          -> SYSCLK = PLL 80 MHz (from LED code)
 * 80 MHz is required: the WS2812B bit-banging needs >= 48 MHz.
 */
static void SystemClock_Config(void)
{
    /* Ensure MSI is enabled */
    RCC->CR |= RCC_CR_MSION;

    while ((RCC->CR & RCC_CR_MSIRDY) == 0U)
    {
    }

    /* Select MSI range = 4 MHz, using the range field */
    RCC->CR |= RCC_CR_MSIRGSEL;

    RCC->CR = (RCC->CR & ~RCC_CR_MSIRANGE) |
              RCC_CR_MSIRANGE_6;

    /* Wait for MSI ready */
    while ((RCC->CR & RCC_CR_MSIRDY) == 0U)
    {
    }

    /* Set AHB and APB prescalers to /1 */
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 |
                   RCC_CFGR_PPRE2);

    /* Switch SYSCLK to MSI */
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_MSI;

    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_MSI)
    {
    }

    /* 4 MHz operation: zero Flash wait states */
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
}

static void Clock_Init(void)                                  /* NEW (from LED code, unchanged) */
{
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_MSI)
    {
        return;
    }

    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) |
                 FLASH_ACR_LATENCY_4WS |
                 FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_4WS)
    {
    }

    RCC->CR &= ~RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) != 0U)
    {
    }

    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_MSI |
                   (0U  << RCC_PLLCFGR_PLLM_Pos) |
                   (40U << RCC_PLLCFGR_PLLN_Pos) |
                   RCC_PLLCFGR_PLLREN;

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U)
    {
    }

    /* Switch system clock to PLL */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
    {
    }

    SystemCoreClock = 80000000U;
}

/* ---------------- DWT cycle counter (MOVED up from LED code, unchanged) ---------------- */

void DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline __attribute__((always_inline))
void DWT_WaitUntil(uint32_t start, uint32_t cycles)
{
    while ((uint32_t)(DWT->CYCCNT - start) < cycles)
    {
    }
}

static void DWT_WaitCycles(uint32_t cycles)
{
    DWT_WaitUntil(DWT->CYCCNT, cycles);
}

/* ---------------- TIMING (non-blocking) ---------------- */
/*  the blocking delay_ms() is removed.  Every wait is now done by comparing the
 * SysTick 1 ms counter (ms_ticks) against a stored start time, so the main loop never stalls. */
volatile uint32_t ms_ticks = 0U;                          /* MOVED up from the PI section */

/* NEW: start-up sequence states, advanced one step per main-loop pass by Startup_Task() */
#define STARTUP_IDLE         0U   /* system OFF                                   */
#define STARTUP_RESET_WAIT   1U   /* NRST released, waiting 20 ms, then probe     */
#define STARTUP_CLEAR_WAIT   2U   /* faults cleared, waiting 20 ms, then configure */
#define STARTUP_CFG_WAIT     3U   /* registers written, waiting 50 ms, then CONV_EN */
#define STARTUP_EN_WAIT      4U   /* CONV_EN written, waiting 10 ms               */
#define STARTUP_DONE         5U   /* sequence finished OK (handled by Startup_Task) */
#define STARTUP_FAIL         6U   /* sequence failed (handled by Startup_Task)    */
#define STARTUP_RUN          7U   /* converter running, PI loop active            */

volatile uint8_t startup_state = STARTUP_IDLE;
static uint32_t  startup_t0 = 0U;

/* ms_ticks when the current wait began */


#define TEC_HOT_LOW_OFF_HIGH   0U
/* ---------------- GPIO ---------------- */

static void LM51772_GPIO_Init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    (void)RCC->AHB2ENR;

    /*
     *  NRST (PB13) is now driven LOW at boot, so the converter stays in
     *  reset (system OFF) until the button is pressed.  DTRK LOW as before.
     */
    GPIOB->BSRR = (1U << (13U + 16U));
    GPIOB->BSRR = (1U << (11U + 16U));

    /* PB13 = output: LM51772_RST */
    GPIOB->MODER &= ~(3U << (13U * 2U));
    GPIOB->MODER |=  (1U << (13U * 2U));
    GPIOB->OTYPER &= ~(1U << 13U);

    /* PB11 = output: DTRK */
    GPIOB->MODER &= ~(3U << (11U * 2U));
    GPIOB->MODER |=  (1U << (11U * 2U));
    GPIOB->OTYPER &= ~(1U << 11U);

    /* PB10 = output: MODE HIGH */
    GPIOB->BSRR = (1U << 10U);
    GPIOB->MODER &= ~(3U << (10U * 2U));
    GPIOB->MODER |=  (1U << (10U * 2U));
    GPIOB->OTYPER &= ~(1U << 10U);

    /* PB7 = input with pull-up: nFAULT */
    GPIOB->MODER &= ~(3U << (7U * 2U));
    GPIOB->PUPDR &= ~(3U << (7U * 2U));
    GPIOB->PUPDR |=  (1U << (7U * 2U));
}

/* ---------------- I2C1 INITIALIZATION ---------------- */

static void I2C1_Init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
    (void)RCC->APB1ENR1;

    /* PB8=SCL, PB9=SDA */
    GPIOB->MODER &= ~((3U << (8U * 2U)) |
                      (3U << (9U * 2U)));

    GPIOB->MODER |=  ((2U << (8U * 2U)) |
                      (2U << (9U * 2U)));

    GPIOB->AFR[1] &= ~((0xFU << 0U) | (0xFU << 4U));
    GPIOB->AFR[1] |=  ((4U << 0U) | (4U << 4U));

    GPIOB->OTYPER |= (1U << 8U) | (1U << 9U);

    GPIOB->PUPDR &= ~((3U << (8U * 2U)) |
                      (3U << (9U * 2U)));

    GPIOB->PUPDR |=  ((1U << (8U * 2U)) |
                      (1U << (9U * 2U)));

    GPIOB->OSPEEDR |= ((3U << (8U * 2U)) |
                       (3U << (9U * 2U)));

    RCC->APB1RSTR1 |= RCC_APB1RSTR1_I2C1RST;
    RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_I2C1RST;

    I2C1->TIMINGR = 0x10909CECU;
    I2C1->CR1 = I2C_CR1_PE;
}

static void I2C1_ClearErrors(void)
{
    uint32_t icr = 0;

    if (I2C1->ISR & I2C_ISR_NACKF) icr |= I2C_ICR_NACKCF;
    if (I2C1->ISR & I2C_ISR_STOPF) icr |= I2C_ICR_STOPCF;
    if (I2C1->ISR & I2C_ISR_BERR)  icr |= I2C_ICR_BERRCF;
    if (I2C1->ISR & I2C_ISR_ARLO)  icr |= I2C_ICR_ARLOCF;

    I2C1->ICR = icr;
}

/* ---------------- I2C LOW-LEVEL FUNCTIONS ---------------- */

/* Return 1 on success, 0 on failure. */

static uint8_t I2C1_WriteReg(uint8_t addr,
                             uint8_t reg,
                             uint8_t data)
{
    uint32_t timeout = 100000U;

    while ((I2C1->ISR & I2C_ISR_BUSY) && --timeout)
    {
    }

    if (timeout == 0U) return 0U;

    I2C1->CR2 = ((uint32_t)addr << 1U) |
                (2U << I2C_CR2_NBYTES_Pos) |
                I2C_CR2_AUTOEND |
                I2C_CR2_START;

    timeout = 100000U;
    while (!(I2C1->ISR & (I2C_ISR_TXIS | I2C_ISR_NACKF)) &&
           --timeout)
    {
    }

    if ((I2C1->ISR & I2C_ISR_NACKF) || timeout == 0U)
    {
        I2C1_ClearErrors();
        return 0U;
    }

    I2C1->TXDR = reg;

    timeout = 100000U;
    while (!(I2C1->ISR & (I2C_ISR_TXIS | I2C_ISR_NACKF)) &&
           --timeout)
    {
    }

    if ((I2C1->ISR & I2C_ISR_NACKF) || timeout == 0U)
    {
        I2C1_ClearErrors();
        return 0U;
    }

    I2C1->TXDR = data;

    timeout = 100000U;
    while (!(I2C1->ISR & I2C_ISR_STOPF) && --timeout)
    {
    }

    uint32_t status = I2C1->ISR;          /* Capture before clearing flags. */
        uint8_t ok = (timeout > 0U) &&
                     ((status & I2C_ISR_STOPF) != 0U) &&
                     ((status & (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO)) == 0U);
        I2C1_ClearErrors();
        return ok;
}

static uint8_t I2C1_WriteCommand(uint8_t addr, uint8_t command)
{
    uint32_t timeout = 100000U;

    while ((I2C1->ISR & I2C_ISR_BUSY) && --timeout)
    {
    }

    if (timeout == 0U) return 0U;

    I2C1->CR2 = ((uint32_t)addr << 1U) |
                (1U << I2C_CR2_NBYTES_Pos) |
                I2C_CR2_AUTOEND |
                I2C_CR2_START;

    timeout = 100000U;
    while (!(I2C1->ISR & (I2C_ISR_TXIS | I2C_ISR_NACKF)) &&
           --timeout)
    {
    }

    if ((I2C1->ISR & I2C_ISR_NACKF) || timeout == 0U)
    {
        I2C1_ClearErrors();
        return 0U;
    }

    I2C1->TXDR = command;

    timeout = 100000U;
    while (!(I2C1->ISR & I2C_ISR_STOPF) && --timeout)
    {
    }

    uint32_t status = I2C1->ISR;          /* Capture before clearing flags. */
    uint8_t ok = (timeout > 0U) &&
                 ((status & I2C_ISR_STOPF) != 0U) &&
                 ((status & (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO)) == 0U);
    I2C1_ClearErrors();
    return ok;
}

static uint8_t I2C1_ReadReg(uint8_t addr,
                            uint8_t reg,
                            uint8_t *data)
{
    uint32_t timeout = 100000U;

    while ((I2C1->ISR & I2C_ISR_BUSY) && --timeout)
    {
    }

    if ((timeout == 0U) || (data == 0)) return 0U;

    /* Write register index */
    I2C1->CR2 = ((uint32_t)addr << 1U) |
                (1U << I2C_CR2_NBYTES_Pos) |
                I2C_CR2_START;

    timeout = 100000U;
    while (!(I2C1->ISR & (I2C_ISR_TXIS | I2C_ISR_NACKF)) &&
           --timeout)
    {
    }

    if ((I2C1->ISR & I2C_ISR_NACKF) || timeout == 0U)
    {
        I2C1_ClearErrors();
        return 0U;
    }

    I2C1->TXDR = reg;

    timeout = 100000U;
    while (!(I2C1->ISR & (I2C_ISR_TC | I2C_ISR_NACKF)) &&
           --timeout)
    {
    }

    if ((I2C1->ISR & I2C_ISR_NACKF) || timeout == 0U)
    {
        I2C1_ClearErrors();
        return 0U;
    }

    /* Repeated START, read one byte */
    I2C1->CR2 = ((uint32_t)addr << 1U) |
                (1U << I2C_CR2_NBYTES_Pos) |
                I2C_CR2_RD_WRN |
                I2C_CR2_AUTOEND |
                I2C_CR2_START;

    timeout = 100000U;
    while (!(I2C1->ISR & (I2C_ISR_RXNE | I2C_ISR_NACKF)) &&
           --timeout)
    {
    }

    if ((I2C1->ISR & I2C_ISR_NACKF) || timeout == 0U)
    {
        I2C1_ClearErrors();
        return 0U;
    }

    *data = (uint8_t)I2C1->RXDR;

    timeout = 100000U;
    while (!(I2C1->ISR & I2C_ISR_STOPF) && --timeout)
    {
    }

    I2C1_ClearErrors();
    return (timeout > 0U);
}

static uint8_t I2C1_ReadWord(uint8_t addr,
                             uint8_t reg,
                             uint16_t *data)
{
    uint8_t lsb = 0U;
    uint8_t msb = 0U;

    if (data == 0) return 0U;

    /*
     * Read low byte and high byte using two byte reads.
     * Confirm the LM51772 register's byte order against its datasheet.
     */
    if (!I2C1_ReadReg(addr, reg, &lsb)) return 0U;
    if (!I2C1_ReadReg(addr, (uint8_t)(reg + 1U), &msb)) return 0U;

    *data = ((uint16_t)msb << 8U) | lsb;
    return 1U;
}

static uint8_t I2C1_ProbeAddress(uint8_t addr)
{
    uint32_t timeout = 100000U;

    while ((I2C1->ISR & I2C_ISR_BUSY) && --timeout)
    {
    }

    if (timeout == 0U) return 0U;

    /*
     * Probe with a one-byte read.
     * This avoids transmitting a dummy command/data byte.
     */
    I2C1->CR2 = ((uint32_t)addr << 1U) |
                (1U << I2C_CR2_NBYTES_Pos) |
                I2C_CR2_RD_WRN |
                I2C_CR2_AUTOEND |
                I2C_CR2_START;

    timeout = 100000U;
    while (!(I2C1->ISR & (I2C_ISR_RXNE |
                          I2C_ISR_NACKF |
                          I2C_ISR_STOPF)) && --timeout)
    {
    }

    if ((I2C1->ISR & I2C_ISR_NACKF) || timeout == 0U)
    {
        I2C1_ClearErrors();
        return 0U;
    }

    if (I2C1->ISR & I2C_ISR_RXNE)
    {
        volatile uint8_t dummy = (uint8_t)I2C1->RXDR;
        (void)dummy;
    }

    timeout = 100000U;
    while (!(I2C1->ISR & I2C_ISR_STOPF) && --timeout)
    {
    }

    I2C1_ClearErrors();
    return (timeout > 0U);
}

/* ---------------- REGISTER CONFIGURATION ---------------- */

/* CHANGED: LM51772_Configure() was one blocking function with three delay_ms() calls.
 * It is now split at those three waits into a step function.  The register writes, their
 * values and their order are exactly the same; only the waiting is non-blocking.
 * Called repeatedly from Startup_Task(); each call does at most one step and returns. */
static void LM51772_StartupStep(void)
{
    uint16_t vout_code;
    uint8_t d0 = 0U;
    uint32_t elapsed = (uint32_t)(ms_ticks - startup_t0);

    switch (startup_state)
    {
    case STARTUP_RESET_WAIT:
        if (elapsed < 20U) return;                         /* was delay_ms(20) after NRST release */

        /* Same probe that used to run in Button_LED_Task */
        lm51772_ack = I2C1_ProbeAddress(LM51772_ADDR);

        if (!lm51772_ack)
        {
            firmware_error = 1U;
            startup_state = STARTUP_FAIL;
            return;
        }

        /* Clear previous faults */
        if (!I2C1_WriteCommand(LM51772_ADDR, REG_CLEAR_FAULTS))
        {
            firmware_error = 2U;
            startup_state = STARTUP_FAIL;
            return;
        }

        startup_t0 = ms_ticks;
        startup_state = STARTUP_CLEAR_WAIT;
        return;

    case STARTUP_CLEAR_WAIT:
        if (elapsed < 20U) return;                         /* was delay_ms(20) after clear faults */

        /*
         * These are the startup register values from your uploaded code.
         * Confirm them against your intended final configuration.
         */
        vout_code = (uint16_t)((TARGET_VOUT_V / 0.020f) + 0.5f);   /* 20 mV per code */

        if (!I2C1_WriteReg(LM51772_ADDR, REG_MFR_D0, 0x30U) ||
            !I2C1_WriteReg(LM51772_ADDR, REG_MFR_D2, 0x00U) ||
            !I2C1_WriteReg(LM51772_ADDR, REG_MFR_D3, 0x00U) ||
            !I2C1_WriteReg(LM51772_ADDR, REG_MFR_D4, 0x03U) ||
            !I2C1_WriteReg(LM51772_ADDR, REG_MFR_D9, 0x00U) ||
            !I2C1_WriteReg(LM51772_ADDR, REG_USB_PD_CONTROL, 0x01U) ||
            !I2C1_WriteReg(LM51772_ADDR, REG_VOUT_LSB, (uint8_t)(vout_code & 0xFFU)) ||
            !I2C1_WriteReg(LM51772_ADDR, REG_VOUT_MSB, (uint8_t)(vout_code >> 8U)) ||
            !I2C1_WriteReg(LM51772_ADDR, REG_ILIM_THRESHOLD, ILIM_THRESHOLD_VALUE))
        {
            firmware_error = 2U;
            startup_state = STARTUP_FAIL;
            return;
        }

        /* Confirm the current limit before enabling the power stage. */
        if (!I2C1_ReadReg(LM51772_ADDR, REG_ILIM_THRESHOLD, (uint8_t *)&rb_ilim) ||
            rb_ilim != ILIM_THRESHOLD_VALUE)
        {
            firmware_error = 4U;
            startup_state = STARTUP_FAIL;
            return;
        }

        startup_t0 = ms_ticks;
        startup_state = STARTUP_CFG_WAIT;
        return;

    case STARTUP_CFG_WAIT:
        if (elapsed < 50U) return;                         /* was delay_ms(50) before CONV_EN */

        /* Read current D0, then set CONV_EN bit 0. */
        if (!I2C1_ReadReg(LM51772_ADDR, REG_MFR_D0, &d0))
        {
            firmware_error = 2U;
            startup_state = STARTUP_FAIL;
            return;
        }

        d0 |= 0x01U;

        if (!I2C1_WriteReg(LM51772_ADDR, REG_MFR_D0, d0))
        {
            firmware_error = 2U;
            startup_state = STARTUP_FAIL;
            return;
        }

        startup_t0 = ms_ticks;
        startup_state = STARTUP_EN_WAIT;
        return;

    case STARTUP_EN_WAIT:
        if (elapsed < 10U) return;                         /* was delay_ms(10) after CONV_EN */

        startup_state = STARTUP_DONE;
        return;

    default:
        return;
    }
}

/* ---------------- REGISTER READBACK ---------------- */

static uint8_t LM51772_Readback(void)
{
    uint8_t ok = 1U;

    uint8_t value;
#define READ_PUBLISH(reg, destination) do { \
    if (I2C1_ReadReg(LM51772_ADDR, (reg), &value)) (destination) = value; else ok = 0U; \
} while (0)
    READ_PUBLISH(REG_MFR_D0, rb_mfr_d0);
    READ_PUBLISH(REG_MFR_D1, rb_mfr_d1);
    READ_PUBLISH(REG_MFR_D2, rb_mfr_d2);
    READ_PUBLISH(REG_MFR_D3, rb_mfr_d3);
    READ_PUBLISH(REG_MFR_D4, rb_mfr_d4);
    READ_PUBLISH(REG_MFR_D9, rb_mfr_d9);

    READ_PUBLISH(REG_USB_PD_CONTROL, rb_usb_pd_control);

    READ_PUBLISH(REG_ILIM_THRESHOLD, rb_ilim);
    READ_PUBLISH(REG_VOUT_LSB, rb_vout_lsb);
    READ_PUBLISH(REG_VOUT_MSB, rb_vout_msb);

    READ_PUBLISH(REG_STATUS_BYTE, rb_status_byte);
#undef READ_PUBLISH

    rb_vout_code = ((uint16_t)rb_vout_msb << 8U) | rb_vout_lsb;
    rb_vout_voltage_v = (float)rb_vout_code * 0.020f;

    lm51772_converter_enabled = (rb_mfr_d0 & 0x01U) ? 1U : 0U;

    lm51772_nfault_pin = (GPIOB->IDR & (1U << 7U)) ? 1U : 0U;
    lm51772_reset_pin  = (GPIOB->IDR & (1U << 13U)) ? 1U : 0U;

    if (!ok) firmware_error = 3U;
    return ok;
}



float BATT_ReadVoltage(void)
{
    batt_vsns_adc_raw = ADC1_Read(BATT_VSNS_CHANNEL);
    batt_vsns_adc_voltage = ADC_ToVoltage(batt_vsns_adc_raw);

    batt_voltage = adc_last_ok ? ph_battery_voltage(&battery_calibration, batt_vsns_adc_raw) : NAN;
    return batt_voltage;
}

float BATT_ReadCurrent(void)
{
    uint32_t sum = 0U;
    for (uint32_t k = 0U; k < 32U; k++)
    {
        sum += ADC1_Read(BATT_ISNS_CHANNEL);
    }
    batt_isns_adc_raw = (uint16_t)(sum / 32U);
    batt_isns_adc_voltage = ADC_ToVoltage(batt_isns_adc_raw);
    batt_current = adc_last_ok ? ph_battery_current(&battery_calibration, batt_isns_adc_raw) : NAN;
        return batt_current;
    }

float TEC_ReadVoltage(void)
{
    uint32_t sum = 0U;
    for (uint32_t k = 0U; k < 32U; k++) sum += ADC1_Read(TEC_VSNS_CHANNEL);
    tec_vsns_adc_raw = (uint16_t)(sum / 32U);
    tec_vsns_adc_voltage = ADC_ToVoltage(tec_vsns_adc_raw);
    tec_voltage = tec_vsns_adc_voltage * TEC_DIVIDER_RATIO;
    return tec_voltage;
}

float TEC_ReadCurrent(void)
{
    tec_isns_adc_raw = ADC1_Read(TEC_ISNS_CHANNEL);
    tec_isns_adc_voltage = ADC_ToVoltage(tec_isns_adc_raw);

    /* U5 is ACS71240LLCBTR-010B3: 1.65 V zero-current output,
       132 mV/A typical at 3.3 V. Calibrate zero offset on the assembled board. */
    tec_current = (tec_isns_adc_voltage - 1.65f) / 0.132f;

    return tec_current;
}

float CSFAN_ReadCurrent(void)
{
#if FAN_ISNS_ENABLE
    cs_fan_isns_adc_raw = ADC1_Read(CSFAN_ISNS_CHANNEL);
    cs_fan_isns_adc_voltage = ADC_ToVoltage(cs_fan_isns_adc_raw);
    cs_fan_current = (cs_fan_isns_adc_voltage - CSFAN_ISNS_ZERO_V) / CSFAN_ISNS_V_PER_A;
#else
    cs_fan_current = 0.0f;
#endif
    return cs_fan_current;
}

float HSFAN_ReadCurrent(void)
{
#if FAN_ISNS_ENABLE
    hs_fan_isns_adc_raw = ADC1_Read(HSFAN_ISNS_CHANNEL);
    hs_fan_isns_adc_voltage = ADC_ToVoltage(hs_fan_isns_adc_raw);
    hs_fan_current = (hs_fan_isns_adc_voltage - HSFAN_ISNS_ZERO_V) / HSFAN_ISNS_V_PER_A;
#else
    hs_fan_current = 0.0f;
#endif
    return hs_fan_current;
}

///////////////////////////////////////////////////////

static void ADC1_Init(void)
{
    /* Enable GPIOA, GPIOB and ADC clocks */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN |
                    RCC_AHB2ENR_GPIOBEN |
                    RCC_AHB2ENR_GPIOCEN |
                    RCC_AHB2ENR_ADCEN;
    (void)RCC->AHB2ENR;

    /* PA0 = BATT_VSNS, PA1 = BATT_ISNS */
    GPIOA->MODER |= (3U << 0U) | (3U << 2U);
    GPIOA->PUPDR &= ~((3U << 0U) | (3U << 2U));

    /* PB0 = TEC_ISNS, PB1 = TEC_VSNS */
    GPIOB->MODER |= (3U << 0U) | (3U << 2U);
    GPIOB->PUPDR &= ~((3U << 0U) | (3U << 2U));

    /* PC0..PC4: ambient, cold, hot, flask top, flask bottom. */
    GPIOC->MODER |= 0x3FFU;
    GPIOC->PUPDR &= ~0x3FFU;

    /* PC4 = FLASK_BOTTOM_TEMP (ADC1_IN13) */
    GPIOC->MODER |= (3U << (4U * 2U));
    GPIOC->PUPDR &= ~(3U << (4U * 2U));

#if FAN_ISNS_ENABLE
    /* Fan current sense pin -> analog mode (clock for its port enabled here) */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN |
                    RCC_AHB2ENR_GPIOCEN | RCC_AHB2ENR_GPIODEN;
    (void)RCC->AHB2ENR;
    CSFAN_ISNS_PORT->MODER |= (3U << (CSFAN_ISNS_PIN * 2U));
    CSFAN_ISNS_PORT->PUPDR &= ~(3U << (CSFAN_ISNS_PIN * 2U));
    HSFAN_ISNS_PORT->MODER |= (3U << (HSFAN_ISNS_PIN * 2U));
    HSFAN_ISNS_PORT->PUPDR &= ~(3U << (HSFAN_ISNS_PIN * 2U));
#endif

    /* ADC clock prescaler: retain the existing project setting */
    ADC1_COMMON->CCR &= ~(3U << 16U);
    ADC1_COMMON->CCR |=  (1U << 16U);

    /* Exit deep-power-down and enable ADC regulator */
    ADC1->CR &= ~(1U << 29U);
    ADC1->CR |=  (1U << 28U);

    for (volatile uint32_t i = 0; i < 10000U; i++)
        __NOP();

    /* Calibrate ADC while disabled */
    ADC1->CR |= (1U << 31U);
    if (!ADC1_Wait(&ADC1->CR, 1U << 31U, 0U)) return;

    /* 12-bit resolution */
    ADC1->CFGR &= ~(3U << 3U);

    /* Channels 2, 5 and 6: SMPR1 (channel 2 = COLDSINK_TEMP, NEW) */
    ADC1->SMPR1 &= ~((7U << 6U) | (7U << 15U) | (7U << 18U));
    ADC1->SMPR1 |=  ((7U << 6U) | (7U << 15U) | (7U << 18U));
    ADC1->SMPR1 |= (7U << 3U) | (7U << 9U) | (7U << 12U);

    /* Channels 13, 15 and 16: SMPR2 (bits (ch-10)*3) */
    ADC1->SMPR2 &= ~((7U << 9U) | (7U << 15U) | (7U << 18U));
    ADC1->SMPR2 |=  ((7U << 9U) | (7U << 15U) | (7U << 18U));

#if FAN_ISNS_ENABLE
    /* Fan current channel: longest sample time (SMPR1 = ch 0-9, SMPR2 = ch 10-18) */
    if (CSFAN_ISNS_CHANNEL < 10U) ADC1->SMPR1 |= (7U << (CSFAN_ISNS_CHANNEL * 3U));
    else                          ADC1->SMPR2 |= (7U << ((CSFAN_ISNS_CHANNEL - 10U) * 3U));

    if (HSFAN_ISNS_CHANNEL < 10U) ADC1->SMPR1 |= (7U << (HSFAN_ISNS_CHANNEL * 3U));
    else                          ADC1->SMPR2 |= (7U << ((HSFAN_ISNS_CHANNEL - 10U) * 3U));
#endif

    /* Clear ADRDY, enable ADC and wait for ready */
    ADC1->ISR = (1U << 0U);
    ADC1->CR |= (1U << 0U);

    (void)ADC1_Wait(&ADC1->ISR, 1U, 1U);
}

/////////////////////////////////////////
static uint16_t ADC1_Read(uint32_t channel)
{
    adc_last_ok = 0U;
    if (channel > 18U || feature_adc_fault) return 0U;
    /* Select one conversion */
    ADC1->SQR1 &= ~((0xFU << 0U) | (0x1FU << 6U));
    ADC1->SQR1 |= (channel << 6U);

    /* Clear EOC and start conversion */
    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
    ADC1->CR |= (1U << 2U);
    PH_ADC_POLL_HOOK(&ADC1->ISR, ADC_ISR_EOC, ADC_ISR_EOC);

    if (!ADC1_Wait(&ADC1->ISR, 1U << 2U, 1U << 2U)) {
        ADC1->CR |= ADC_CR_ADSTP;
        return 0U;
    }
    if (ADC1->ISR & ADC_ISR_OVR) {
        feature_adc_fault = 1U;
        GPIOB->BSRR = 1UL << (13U + 16U);
        return 0U;
    }
    adc_last_ok = 1U;
    return (uint16_t)ADC1->DR;
}

static int ADC1_Wait(volatile uint32_t *reg, uint32_t mask, uint32_t expected)
{
    uint32_t start = DWT->CYCCNT, guard = 200000U;
    while ((*reg & mask) != expected) {
        PH_ADC_POLL_HOOK(reg, mask, expected);
        if ((uint32_t)(DWT->CYCCNT - start) >= SystemCoreClock / 500U || --guard == 0U) {
            adc_timeout_count++;
            feature_adc_fault = 1U;
            GPIOB->BSRR = 1UL << (13U + 16U);
            return 0;
        }
    }
    return 1;
}

static float ADC_ToVoltage(uint16_t adc)
{
    return ((float)adc * ADC_VREF) / ADC_MAX_COUNT;
}
////////////////////////////////////////

/*
 *  TEC TEMPERATURE CONTROL  --  PI + anti-windup
 *  Cold-sink NTC (PC1 = ADC1_IN2)  ->  16-sample average -> beta eq.

 *  -> EMA filter (alpha 0.25) -> PI (Ts = 1 s) -> TEC current request
 *  -> LM51772 ILIM_THRESHOLD code (reg 0x0A) written over I2C.
 * ===================================================================== */

#define PI_ENABLE                 1U      /* 0 = measure temperature only, never touch ILIM */

#define FLASK_BOTTOM_TEMP_CHANNEL 13U     /* PC4 -> ADC1_IN13 (no longer used by the PI) */
#define COLDSINK_TEMP_CHANNEL      2U     /* NEW: PC1 -> ADC1_IN2, COLDSINK_TEMP = PI / LED input */
#define THERMISTOR_SAMPLES        16U

/* NTC divider: +3V0 --10k-- node --NTC-- GND.  The ADC full scale is VDDA (~3.3 V),
 * the divider is excited from the 3.0 V reference, so the two are NOT the same number. */
#define NTC_ADC_FS_V              3.3f    /* VDDA = ADC full-scale  (confirm with a meter) */
#define NTC_EXCITATION_V          3.0f    /* +3V0 rail feeding R129 */
#define NTC_R_FIXED               10000.0f
#define NTC_R0                    10000.0f
#define NTC_T0                    298.15f
#define NTC_BETA                  3977.0f
#define TEMP_VALID_MIN_C          (-40.0f)
#define TEMP_VALID_MAX_C          125.0f

#define TEC_TEMP_SETPOINT         3.0f
#define TEMP_FILTER_ALPHA         0.25f

#define PI_KP                     (-0.21f)
#define PI_KI                     (-0.0003f)
#define PI_TS                     1.0f
#define MIN_CURRENT               0.5f
#define MAX_CURRENT               7.00f
#define CURRENT_STEP              0.05f

#define ILIM_CODE_MIN             0x0AU   /* 0.50 A (code = 0x0A + (I-0.5)/0.05) */
#define ILIM_CODE_MAX             0x8CU   /* 7.00 A */

#define CONTROL_PERIOD_MS         1000U
#define MEASURE_PERIOD_MS         100U

/* ---- live variables for the debugger ---- */
volatile float    flask_bottom_raw_c      = 0.0f;
volatile float    flask_bottom_filt_c     = 0.0f;
volatile float    ntc_adc_avg             = 0.0f;
volatile float    ntc_resistance_ohm      = 0.0f;
volatile uint8_t  flask_temp_valid        = 0U;
volatile uint32_t sensor_fault_count      = 0U;
volatile uint8_t  filter_initialised      = 0U;

volatile float    pi_error                = 0.0f;
volatile float    pi_integral             = 0.0f;
volatile float    pi_provisional_integral = 0.0f;
volatile float    pi_provisional_current  = 0.0f;
volatile float    pi_requested_current_a  = MIN_CURRENT;
volatile uint8_t  pi_sat_high             = 0U;
volatile uint8_t  pi_sat_low              = 0U;

volatile uint8_t  ilim_code_requested     = ILIM_CODE_MIN;
volatile uint8_t  ilim_readback_match     = 0U;
volatile uint32_t ilim_write_count        = 0U;
volatile uint32_t ilim_write_fail_count   = 0U;
volatile uint32_t control_cycles          = 0U;

/* ---- 1 ms time base ---- */
#define SYSCLK_HZ                 80000000U  /* CHANGED: was 4000000U. PLL 80 MHz, set in Clock_Init() */
/* ms_ticks is now declared in the TIMING section near the top (MOVED) */

/* Pure bare metal: the 1 ms tick interrupt is defined here, no HAL involved.
 * In stm32l4xx_it.c the HAL SysTick_Handler() must be removed or commented out, otherwise
 * the linker reports "multiple definition of SysTick_Handler". */
void SysTick_Handler(void)
{
    ms_ticks++;
    Features_InterruptTick();
}

static void SysTick_Start(void)
{
    SysTick->CTRL = 0U;
    SysTick->LOAD = (SYSCLK_HZ / 1000U) - 1U;
    SysTick->VAL  = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |   /* core clock */
                    SysTick_CTRL_TICKINT_Msk   |
                    SysTick_CTRL_ENABLE_Msk;
}

/* ---- NTC -> temperature ---- */
static float NTC_ReadTemperatureC(uint32_t channel, uint8_t *valid)
{
    uint32_t sum = 0U;
    float avg, v, r, tk, t;
    uint32_t i;

    for (i = 0U; i < THERMISTOR_SAMPLES; i++)
    {
        sum += ADC1_Read(channel);
    }

    avg = (float)sum / (float)THERMISTOR_SAMPLES;
    v   = (avg * NTC_ADC_FS_V) / ADC_MAX_COUNT;

    ntc_adc_avg = avg;

    /* Shorted / open sensor (or wrong excitation): reject */
    if ((v <= 0.0f) || (v >= NTC_EXCITATION_V))
    {
        ntc_resistance_ohm = 0.0f;
        *valid = 0U;
        return -273.15f;
    }

    r = NTC_R_FIXED * v / (NTC_EXCITATION_V - v);
    ntc_resistance_ohm = r;

    tk = 1.0f / ((1.0f / NTC_T0) + (1.0f / NTC_BETA) * logf(r / NTC_R0));
    t  = tk - 273.15f;

    *valid = ((t >= TEMP_VALID_MIN_C) && (t <= TEMP_VALID_MAX_C)) ? 1U : 0U;
    return t;
}

/* ---- PI with conditional-integration anti-windup (same maths as the ESP32 code) ---- */
void PI_Reset(void)
{
    pi_integral = 0.0f;
}

/////////////////////////////////////////////////////////
static volatile uint8_t tec_fault_latched = 0U;

static void TEC_StopFault(uint32_t reason)
{
    GPIOB->BSRR = 1UL << (13U + 16U);   /* Assert existing LM nRST LOW. */
    if (!tec_fault_latched) firmware_error = reason;
    tec_fault_latched = 1U;
    startup_state = STARTUP_IDLE;
    ledState = 0U;
    lm51772_init_ok = 0U;
    lm51772_converter_enabled = 0U;
    ilim_readback_match = 0U;
    pi_requested_current_a = 0.0f;
    filter_initialised = 0U;
    flask_temp_valid = 0U;
    PI_Reset();

}


////////////////////////////////////////////////////////

static float PI_Controller(float setpoint, float fl_bottom_temp)
{
    float error = setpoint - fl_bottom_temp;
    float integral = pi_integral;

    float provisional_integral = integral + PI_KI * error * PI_TS;
    float provisional_current  = PI_KP * error + provisional_integral;

    uint8_t saturated_high = (provisional_current >= MAX_CURRENT) && (error < 0.0f);
    uint8_t saturated_low  = (provisional_current <= MIN_CURRENT) && (error > 0.0f);

    float tec_current;

    if (!saturated_high && !saturated_low)
    {
        integral = provisional_integral;
    }

    tec_current = PI_KP * error + integral;

    if (tec_current < MIN_CURRENT) tec_current = MIN_CURRENT;
    if (tec_current > MAX_CURRENT) tec_current = MAX_CURRENT;

    /* round to nearest 0.05 A (value is >= 0.5, so truncation == floor) */
    tec_current = CURRENT_STEP * (float)(int32_t)((tec_current / CURRENT_STEP) + 0.5f);

    pi_error                = error;
    pi_integral             = integral;
    pi_provisional_integral = provisional_integral;
    pi_provisional_current  = provisional_current;
    pi_sat_high             = saturated_high;
    pi_sat_low              = saturated_low;

    return tec_current;
}

/* ---- current (A) -> ILIM code.  0.50 A = 0x0A, +0.05 A per code, 7.00 A = 0x8C ---- */
static uint8_t CurrentToILIMCode(float current_a)
{
    int32_t steps;

    if (current_a <= MIN_CURRENT) return ILIM_CODE_MIN;
    if (current_a >= MAX_CURRENT) return ILIM_CODE_MAX;

    steps = (int32_t)(((current_a - MIN_CURRENT) / CURRENT_STEP) + 0.5f);

    if ((ILIM_CODE_MIN + steps) > (int32_t)ILIM_CODE_MAX) return ILIM_CODE_MAX;
    return (uint8_t)(ILIM_CODE_MIN + steps);
}

/* Write ILIM only when the code changes. The cached value is updated only after a
 * successful write, so a failed I2C transfer is simply retried on the next cycle. */
static uint8_t TEC_ApplyIlimCode(uint8_t code)
{
    uint8_t actual = 0U;

    ilim_code_requested = code;
    ilim_readback_match = 0U;

    if (tec_fault_latched || code < ILIM_CODE_MIN || code > ILIM_CODE_MAX)
        return 0U;

    /* First bench revision: verify every call; no cache-based skipping. */
    if (!I2C1_WriteReg(LM51772_ADDR, REG_ILIM_THRESHOLD, code) ||
        !I2C1_ReadReg(LM51772_ADDR, REG_ILIM_THRESHOLD, &actual) ||
        actual != code)
    {
        ilim_write_fail_count++;      /* Counts write OR verification failure. */
        return 0U;
    }

    rb_ilim = actual;
    ILIM_THRESHOLD_VALUE = actual;    /* Update only after verified success. */
    ilim_readback_match = 1U;
    ilim_write_count++;
    return 1U;
}

/* Called every CONTROL_PERIOD_MS (= Ts). */
static void TEC_ControlStep(void)
{
    uint8_t valid = (uint8_t)ph_temperatures_fresh(&temperatures, ms_ticks);
    float raw = temperatures.c[PH_COLD];

    control_cycles++;
    flask_bottom_raw_c = raw;
    flask_temp_valid   = valid;

    if (!valid)
    {
        sensor_fault_count++;
        TEC_StopFault(100U);
        return;
    }

    if (!filter_initialised)
    {
        flask_bottom_filt_c = raw;
        filter_initialised  = 1U;
    }
    else
    {
        flask_bottom_filt_c = TEMP_FILTER_ALPHA * raw +
                              (1.0f - TEMP_FILTER_ALPHA) * flask_bottom_filt_c;
    }

#if PI_ENABLE
    pi_requested_current_a = PI_Controller(TEC_TEMP_SETPOINT, flask_bottom_filt_c);
    if (!TEC_ApplyIlimCode(CurrentToILIMCode(pi_requested_current_a)))
    {
        TEC_StopFault(101U);
        return;
    }
#endif
}
/* END TEC_PI */


volatile uint8_t fans_on = 0U;

static void Fans_Set(uint8_t on)
{
    if (on)
    {
        GPIOB->BSRR = (1U << 4U);              /* CSFAN_PWM HIGH = fan ON */
        GPIOD->BSRR = (1U << 2U);              /* HSFAN_PWM HIGH = fan ON */
    }
    else
    {
        GPIOB->BSRR = (1U << (4U + 16U));
        GPIOD->BSRR = (1U << (2U + 16U));
    }

    fans_on = on ? 1U : 0U;
}

static void Fan_GPIO_Init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN |
                    RCC_AHB2ENR_GPIODEN;
    (void)RCC->AHB2ENR;

    /* CHANGED: preload both outputs LOW = fans OFF (HIGH turns the fan FETs on, so the fans
     * were running from power-up). Fans are switched on by Fans_Set(1) when the system starts. */
    GPIOB->BSRR = (1U << (4U + 16U));
    GPIOD->BSRR = (1U << (2U + 16U));

    /* PB4 = CSFAN_PWM as GPIO output */
    GPIOB->MODER &= ~(3U << 8U);
    GPIOB->MODER |=  (1U << 8U);
    GPIOB->OTYPER &= ~(1U << 4U);

    /* PD2 = HSFAN_PWM as GPIO output */
    GPIOD->MODER &= ~(3U << 4U);
    GPIOD->MODER |=  (1U << 4U);
    GPIOD->OTYPER &= ~(1U << 2U);
}


static void TEC_HOT_LOW_GPIO_Init(void)
{
#ifndef TEC_HOT_LOW_OFF_HIGH
#error "Define TEC_HOT_LOW_OFF_HIGH (1 or 0) from the measured bridge truth table"
#endif
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    (void)RCC->AHB2ENR;

    /* Preload the OFF level BEFORE switching the pin to output */
#if TEC_HOT_LOW_OFF_HIGH
    GPIOC->BSRR = 1UL << 10U;           /* MCU pin HIGH */
#else
    GPIOC->BSRR = 1UL << (10U + 16U);   /* MCU pin LOW */
#endif

    GPIOC->MODER &= ~(3U << (10U * 2U));
    GPIOC->MODER |=  (1U << (10U * 2U));
    GPIOC->OTYPER &= ~(1U << 10U);
    GPIOC->PUPDR  &= ~(3U << (10U * 2U));
}


//   LED with BUTTON CONTROL


void GPIO_Inputs_Init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN |
                    RCC_AHB2ENR_GPIOBEN |
                    RCC_AHB2ENR_GPIOCEN;

    (void)RCC->AHB2ENR;

    /* PA9: button input */
    GPIOA->MODER &= ~(3U << (WAKEUP_SW_PIN * 2U));
    GPIOA->PUPDR &= ~(3U << (WAKEUP_SW_PIN * 2U));

    /* PC7 and PC8: input pins */
    GPIOC->MODER &= ~((3U << (LID_OPEN_PIN * 2U)) |
                      (3U << (CHARGER_LOCK_PIN * 2U)));

    GPIOC->PUPDR &= ~((3U << (LID_OPEN_PIN * 2U)) |
                      (3U << (CHARGER_LOCK_PIN * 2U)));

    /* PB14 and PB15: output mode */
    GPIOB->MODER &= ~((3U << (SR_SER_PIN * 2U)) |
                      (3U << (SR_SRCLK_PIN * 2U)));

    GPIOB->MODER |=  ((1U << (SR_SER_PIN * 2U)) |
                      (1U << (SR_SRCLK_PIN * 2U)));

    /* PC6: latch output */
    GPIOC->MODER &= ~(3U << (SR_RCLK_PIN * 2U));
    GPIOC->MODER |=  (1U << (SR_RCLK_PIN * 2U));

    /* Initialize shift-register signals LOW */
    GPIOB->BSRR = (1U << (SR_SER_PIN + 16U)) |
                  (1U << (SR_SRCLK_PIN + 16U));

    GPIOC->BSRR = 1U << (SR_RCLK_PIN + 16U);

    /* PA8: WS2812B output, push-pull, very high speed, idle LOW */
    GPIOA->BSRR = WS2812B_MASK << 16U;

    GPIOA->MODER &= ~(3U << (WS2812B_PIN * 2U));
    GPIOA->MODER |=  (1U << (WS2812B_PIN * 2U));

    GPIOA->OTYPER &= ~WS2812B_MASK;

    GPIOA->OSPEEDR &= ~(3U << (WS2812B_PIN * 2U));
    GPIOA->OSPEEDR |=  (3U << (WS2812B_PIN * 2U));
}

//   READ INPUT PINS

void GPIO_Inputs_Read(void)
{
    gpioa_input_snapshot = GPIOA->IDR;
    gpioc_input_snapshot = GPIOC->IDR;

    lid_open_pin_state =
        (gpioc_input_snapshot & (1U << LID_OPEN_PIN)) ? 1U : 0U;

    charger_lock_pin_state =
        (gpioc_input_snapshot & (1U << CHARGER_LOCK_PIN)) ? 1U : 0U;

    wakeup_sw_pin_state =
        (gpioa_input_snapshot & (1U << WAKEUP_SW_PIN)) ? 1U : 0U;
}

//   74HC595 SHIFT REGISTER

void ShiftRegister_Write(uint8_t data)
{
    /* Latch LOW while shifting */
    GPIOC->BSRR = 1U << (SR_RCLK_PIN + 16U);

    for (int8_t bit = 7; bit >= 0; bit--)
    {
        if ((data & (1U << bit)) != 0U)
        {
            GPIOB->BSRR = 1U << SR_SER_PIN;
        }
        else
        {
            GPIOB->BSRR = 1U << (SR_SER_PIN + 16U);
        }

        GPIOB->BSRR = 1U << SR_SRCLK_PIN;
        GPIOB->BSRR = 1U << (SR_SRCLK_PIN + 16U);
    }

    /* Latch the shifted byte */
    GPIOC->BSRR = 1U << SR_RCLK_PIN;
    GPIOC->BSRR = 1U << (SR_RCLK_PIN + 16U);
}


void WS2812B_SetColor(uint8_t red, uint8_t green, uint8_t blue)
{
    uint32_t cpu_mhz = SystemCoreClock / 1000000U;

    /* At low clock speeds the 0.35/0.8 us pulses cannot be generated.
       Sending garbage makes the LED show white, so do not send at all. */
    if (cpu_mhz < WS2812B_MIN_CPU_MHZ)
    {
        ws2812b_clock_too_slow = 1U;
        GPIOA->BSRR = WS2812B_MASK << 16U;
        return;
    }

    ws2812b_clock_too_slow = 0U;

    /* Brightness scaling */
    red   = (uint8_t)(((uint32_t)red   * NEO_BRIGHTNESS) / 255U);
    green = (uint8_t)(((uint32_t)green * NEO_BRIGHTNESS) / 255U);
    blue  = (uint8_t)(((uint32_t)blue  * NEO_BRIGHTNESS) / 255U);

    /* WS2812B expects GRB byte order */
    uint32_t color = ((uint32_t)green << 16U) |
                     ((uint32_t)red   << 8U)  |
                     (uint32_t)blue;

    uint32_t t0h        = (cpu_mhz * 30U)  / 100U;   /* 0.30 us high for a 0 bit */
    uint32_t t1h        = (cpu_mhz * 70U)  / 100U;   /* 0.70 us high for a 1 bit */
    uint32_t bit_cycles = (cpu_mhz * 125U) / 100U;   /* 1.25 us per bit */

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    for (uint32_t bit = 0U; bit < 24U; bit++)
    {
        uint32_t high_cycles =
            ((color & (1UL << (23U - bit))) != 0U) ? t1h : t0h;

        uint32_t start = DWT->CYCCNT;

        GPIOA->BSRR = WS2812B_MASK;               /* rising edge */
        DWT_WaitUntil(start, high_cycles);

        GPIOA->BSRR = WS2812B_MASK << 16U;        /* falling edge */
        DWT_WaitUntil(start, bit_cycles);
    }

    if (primask == 0U)
    {
        __enable_irq();
    }

    GPIOA->BSRR = WS2812B_MASK << 16U;
    DWT_WaitCycles(cpu_mhz * 80U);
}

static void LED_UpdateFromTemperature(void)
{
    uint8_t wanted = 0U;                          /* 0 = Red   */

    if ((filter_initialised != 0U) &&
        (flask_temp_valid != 0U) &&
        (flask_bottom_filt_c <= LED_GREEN_TEMP_C))
    {
        wanted = 1U;                              /* 1 = Green */
    }

    if (wanted != neoColorIndex)
    {
        neoColorIndex = wanted;

        WS2812B_SetColor(colors[wanted][0],
                         colors[wanted][1],
                         colors[wanted][2]);

        ws2812b_is_on = 1U;
        ws2812b_blink_count++;
    }
}

static void Startup_Task(void)
{
    if (!Features_Ready()) { Features_Pause(); return; }
    if (ledState && startup_state == STARTUP_IDLE) Features_BeginStartup();
    if ((startup_state == STARTUP_IDLE) || (startup_state == STARTUP_RUN))
    {
        return;
    }

    LM51772_StartupStep();
    if (!Features_Ready()) { Features_Pause(); return; }

    if (startup_state == STARTUP_FAIL)
    {
        /* Failed start -> back to OFF (NRST LOW), button still works */
        lm51772_init_ok = 0U;
        firmware_stage = (firmware_error == 1U) ? 0xE1U : 0xE2U;

        GPIOB->BSRR = (1U << (13U + 16U));
        Fans_Set(0U);
        ledState = 0U;
        startup_state = STARTUP_IDLE;
    }
    else if (startup_state == STARTUP_DONE)
    {
    	if (!LM51772_Readback() || !lm51772_nfault_pin || !lm51772_converter_enabled)
          {
    	   TEC_StopFault(102U);
    		return;
    	  }
        lm51772_init_ok = 1U;

        led_pattern = ph_soc_leds(batt_voltage, battery_sampler.valid);
        ShiftRegister_Write(led_pattern);
        led_is_on = 1U;

        /* Start RED; LED_UpdateFromTemperature() switches to GREEN */
        neoColorIndex = 0U;
        WS2812B_SetColor(colors[0][0], colors[0][1], colors[0][2]);
        ws2812b_is_on = 1U;

        Fans_Set(1U);

        firmware_stage = 6U;
        startup_state = STARTUP_RUN;                       /* PI loop is allowed to run now */
    }
}

/*  a button press turns the system ON / OFF */
void Button_LED_Task(void)
{
    /*  debounce - accept a new level only after it has been stable for 30 ms */
    if (wakeup_sw_pin_state == 0U)
    {
        btn_pin_low_seen = 1U;
    }

    if (wakeup_sw_pin_state != button_raw_last)
    {
        button_raw_last = wakeup_sw_pin_state;
        button_raw_change_ms = ms_ticks;
        btn_raw_transitions++;
    }

    if ((uint32_t)(ms_ticks - button_raw_change_ms) >= BUTTON_DEBOUNCE_MS)
    {
        button_current_state = button_raw_last;
    }

    if ((button_previous_state == 0U) &&
        (button_current_state == 1U))
    {
        btn_release_count++;
    }

    if ((button_previous_state == 1U) &&
        (button_current_state == 0U))
    {
        btn_press_count++;
        btn_last_press_ms = ms_ticks;

        ledState = !ledState;

        if (ledState == 0U)
        {
            Features_Beep(PH_BEEP_SLEEP);
            /* ---------- OFF ---------- */

            /* LM51772 -> NRST LOW = converter off  */
            GPIOB->BSRR = (1U << (13U + 16U));
            startup_state = STARTUP_IDLE;                  /*  also cancels a start-up in progress */
            Fans_Set(0U);
            lm51772_init_ok = 0U;

            ShiftRegister_Write(0x00U);
            WS2812B_SetColor(0U, 0U, 0U);

            led_pattern = 0x00U;
            led_is_on = 0U;
            ws2812b_is_on = 0U;

            neoColorIndex = 0U;

            firmware_stage = 0U;
        }
        else
        {
         /* ---------- ON ---------- */
        if (tec_fault_latched)
         {
          ledState = 0U;
          button_previous_state = button_current_state;
          return;
          }

           firmware_error = 0U;
            Features_Beep(PH_BEEP_WAKE);


            ILIM_THRESHOLD_VALUE   = ILIM_CODE_MIN;
            ilim_code_requested    = ILIM_CODE_MIN;
            pi_requested_current_a = MIN_CURRENT;
            filter_initialised     = 0U;
            PI_Reset();

            lm51772_ack = 0U;
            lm51772_init_ok = 0U;

            /* Interlocks qualify in Startup_Task before releasing converter reset. */
            startup_state = STARTUP_IDLE;
            firmware_stage = 2U;
        }
    }

    button_previous_state = button_current_state;
}


#include "stm32l451xx.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>


#define TIM2_CLOCK_HZ       80000000U
#define SD_CS_PIN           2U
#define SD_BLOCK_SIZE       512U
#define SD_TOKEN_START      0xFEU
#define SD_TOKEN_ACCEPT     0x05U
#define SD_SPI_TIMEOUT      200000U
#define SD_READY_TIMEOUT    200000U
#define FAT32_MAX_ROOT_CLUSTERS 4096U



volatile uint32_t sd_debug_stage = 0;
volatile uint32_t sd_debug_error = 0;
volatile uint32_t sd_init_ok = 0;
volatile uint32_t sd_spi_timeout = 0;

volatile uint32_t sd_test_stage = 0;
volatile uint32_t sd_test_error = 0;
volatile uint32_t sd_write_ok = 0;
volatile uint32_t sd_read_ok = 0;
volatile uint32_t sd_data_match = 0;

volatile uint32_t sd_card_type = 0;
volatile uint32_t sd_ocr = 0;
volatile uint32_t sd_acmd41_tries = 0;

volatile uint8_t sd_cmd0_response = 0xFF;
volatile uint8_t sd_cmd8_response = 0xFF;
volatile uint8_t sd_cmd55_response = 0xFF;
volatile uint8_t sd_acmd41_response = 0xFF;
volatile uint8_t sd_cmd58_response = 0xFF;

volatile uint8_t sd_readback[7] = {0};

volatile uint32_t fat32_mount_ok = 0;
volatile uint32_t fat32_error = 0;
volatile uint32_t fat32_file_created = 0;
volatile uint32_t fat32_file_read_ok = 0;
volatile uint32_t fat32_file_match = 0;
volatile uint32_t fat32_file_closed = 0;

volatile uint32_t fat32_partition_lba = 0;
volatile uint32_t fat32_total_sectors = 0;
volatile uint32_t fat32_fat_start = 0;
volatile uint32_t fat32_data_start = 0;
volatile uint32_t fat32_root_cluster = 0;
volatile uint32_t fat32_cluster_count = 0;

volatile uint32_t fat32_file_cluster = 0;
volatile uint32_t fat32_file_sector = 0;
volatile uint32_t fat32_dir_sector = 0;
volatile uint32_t fat32_dir_offset = 0;



static uint8_t sd_verify[512];
static uint8_t fat_sector[512];
static uint8_t file_sector[512];

static uint8_t fat_mounted = 0;

static uint32_t fat_sectors_per_cluster = 0;
static uint32_t fat_reserved_sectors = 0;
static uint32_t fat_num_fats = 0;
static uint32_t fat_sectors_per_fat = 0;

static uint32_t fat_partition_start = 0;
static uint32_t fat_volume_sectors = 0;
static uint32_t fat_first_fat = 0;
static uint32_t fat_first_data = 0;
static uint32_t fat_root = 0;
static uint32_t fat_cluster_count_internal = 0;

static uint32_t fat_next_free_hint = 2;

/* =========================================================
   FUNCTION PROTOTYPES
   ========================================================= */

static uint8_t SD_SPI_Transfer(uint8_t tx);
static void SD_Select(void);
static void SD_Deselect(void);
static uint8_t SD_SendCommand(uint8_t cmd,
                              uint32_t arg,
                              uint8_t crc);

static uint8_t SD_WaitReady(void);
static uint8_t SD_Init(void);

static uint8_t SD_ReadBlock(uint32_t sector,
                            uint8_t *buffer);

static uint8_t SD_WriteBlock(uint32_t sector,
                             const uint8_t *buffer);

static uint8_t FAT32_Mount(void);



static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] |
           ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* =========================================================
   TIM2 DELAY DRIVER
   ========================================================= */

static void Delay_Init(void)
{
    sd_debug_stage = 1;

    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
    (void)RCC->APB1ENR1;

    TIM2->CR1 = 0;
    TIM2->DIER = 0;

    TIM2->PSC = (TIM2_CLOCK_HZ / 1000000U) - 1U;
    TIM2->ARR = 0xFFFFFFFFU;
    TIM2->CNT = 0;

    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->CR1 = TIM_CR1_CEN;

    sd_debug_stage = 2;
}

static uint8_t delay_us(uint32_t us)
{
    uint32_t start = TIM2->CNT;
    uint32_t guard = 4000000U;

    while ((uint32_t)(TIM2->CNT - start) < us)
    {
        if (--guard == 0)
        {
            sd_debug_error = 7;
            return 0;
        }
    }

    return 1;
}

static uint8_t delay_ms(uint32_t ms)
{
    while (ms--)
    {
        if (!delay_us(1000U))
            return 0;
    }

    return 1;
}

/* =========================================================
   SPI1 INITIALIZATION
   ========================================================= */

static void SD_SPI_Init(void)
{
    sd_debug_stage = 10;

    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN |
                    RCC_AHB2ENR_GPIOBEN;

    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    (void)RCC->AHB2ENR;
    (void)RCC->APB2ENR;

    /* PA5, PA6, PA7 alternate function */

    GPIOA->MODER &= ~((3U << 10) |
                      (3U << 12) |
                      (3U << 14));

    GPIOA->MODER |= ((2U << 10) |
                     (2U << 12) |
                     (2U << 14));

    /* AF5 for SPI1 */

    GPIOA->AFR[0] &= ~((15U << 20) |
                      (15U << 24) |
                      (15U << 28));

    GPIOA->AFR[0] |= ((5U << 20) |
                      (5U << 24) |
                      (5U << 28));

    GPIOA->OTYPER &= ~((1U << 5) |
                       (1U << 6) |
                       (1U << 7));

    GPIOA->OSPEEDR |= ((3U << 10) |
                       (3U << 12) |
                       (3U << 14));

    GPIOA->PUPDR &= ~((3U << 10) |
                      (3U << 12) |
                      (3U << 14));

    /* PB2 = SD card CS */

    GPIOB->MODER &= ~(3U << (SD_CS_PIN * 2U));
    GPIOB->MODER |= (1U << (SD_CS_PIN * 2U));

    GPIOB->OTYPER &= ~(1U << SD_CS_PIN);

    GPIOB->BSRR = (1U << SD_CS_PIN);

    /* SPI configuration */

    SPI1->CR1 = 0;
    SPI1->CR2 = 0;

    /*
     * Master mode
     * Software NSS
     * SPI mode 0
     * Clock prescaler /256
     */

    SPI1->CR1 = SPI_CR1_MSTR |
                SPI_CR1_SSM |
                SPI_CR1_SSI |
                SPI_CR1_BR_2 |
                SPI_CR1_BR_1 |
                SPI_CR1_BR_0;

    /* 8-bit data size */

    SPI1->CR2 = SPI_CR2_DS_0 |
                SPI_CR2_DS_1 |
                SPI_CR2_DS_2 |
                SPI_CR2_FRXTH;

    SPI1->CR1 |= SPI_CR1_SPE;

    sd_debug_stage = 11;
}

/* =========================================================
   SPI BYTE TRANSFER
   ========================================================= */

static uint8_t SD_SPI_Transfer(uint8_t tx)
{
    uint32_t timeout = SD_SPI_TIMEOUT;

    while ((SPI1->SR & SPI_SR_TXE) == 0)
    {
        if (--timeout == 0)
        {
            sd_spi_timeout = 1;
            return 0xFF;
        }
    }

    *(__IO uint8_t *)&SPI1->DR = tx;

    timeout = SD_SPI_TIMEOUT;

    while ((SPI1->SR & SPI_SR_RXNE) == 0)
    {
        if (--timeout == 0)
        {
            sd_spi_timeout = 2;
            return 0xFF;
        }
    }

    uint8_t rx = *(__IO uint8_t *)&SPI1->DR;

    if (SPI1->SR & SPI_SR_OVR)
    {
        (void)SPI1->DR;
        (void)SPI1->SR;
    }

    return rx;
}

/* =========================================================
   SD CARD CHIP SELECT
   ========================================================= */

static void SD_Select(void)
{
    GPIOB->BSRR = (1U << (SD_CS_PIN + 16U));
}

static void SD_Deselect(void)
{
    GPIOB->BSRR = (1U << SD_CS_PIN);

    (void)SD_SPI_Transfer(0xFF);
}

/* =========================================================
   WAIT FOR SD CARD READY
   ========================================================= */

static uint8_t SD_WaitReady(void)
{
    for (uint32_t i = 0; i < SD_READY_TIMEOUT; i++)
    {
        uint8_t value = SD_SPI_Transfer(0xFF);

        if (sd_spi_timeout)
            return 0;

        if (value == 0xFF)
            return 1;
    }

    return 0;
}

/* =========================================================
   SD COMMAND TRANSFER
   ========================================================= */

static uint8_t SD_SendCommand(uint8_t cmd,
                              uint32_t arg,
                              uint8_t crc)
{
    uint8_t response = 0xFF;

    SD_Deselect();
    SD_Select();

    if (!SD_WaitReady())
        return 0xFF;

    (void)SD_SPI_Transfer((uint8_t)(0x40U | cmd));

    (void)SD_SPI_Transfer((uint8_t)(arg >> 24));
    (void)SD_SPI_Transfer((uint8_t)(arg >> 16));
    (void)SD_SPI_Transfer((uint8_t)(arg >> 8));
    (void)SD_SPI_Transfer((uint8_t)arg);

    (void)SD_SPI_Transfer(crc);

    if (sd_spi_timeout)
        return 0xFF;

    for (uint32_t i = 0; i < 10; i++)
    {
        response = SD_SPI_Transfer(0xFF);

        if (sd_spi_timeout ||
            ((response & 0x80U) == 0U))
            break;
    }

    return response;
}

/* =========================================================
   SD CARD INITIALIZATION
   ========================================================= */

static uint8_t SD_Init(void)
{
    uint8_t r;
    uint8_t cmd8_ok = 0;

    uint8_t r7[4];
    uint8_t ocr[4];

    uint8_t ready = 0;

    sd_debug_stage = 20;
    sd_debug_error = 0;
    sd_init_ok = 0;
    sd_spi_timeout = 0;
    sd_card_type = 0;
    sd_ocr = 0;

    SD_SPI_Init();

    SD_Deselect();

    /* 80 clock pulses with CS high */

    for (uint32_t i = 0; i < 10; i++)
    {
        (void)SD_SPI_Transfer(0xFF);

        if (sd_spi_timeout)
        {
            sd_debug_error = 6;
            return 0;
        }
    }

    if (!delay_ms(10))
        return 0;

    /* CMD0 */

    sd_debug_stage = 21;

    r = SD_SendCommand(0, 0, 0x95);
    sd_cmd0_response = r;

    SD_Deselect();

    if (sd_spi_timeout || r != 0x01)
    {
        sd_debug_error = 1;
        return 0;
    }

    /* CMD8 */

    sd_debug_stage = 22;

    r = SD_SendCommand(8, 0x000001AA, 0x87);
    sd_cmd8_response = r;

    if (sd_spi_timeout)
    {
        SD_Deselect();
        sd_debug_error = 6;
        return 0;
    }

    if (r == 0x01)
    {
        for (uint32_t i = 0; i < 4; i++)
        {
            r7[i] = SD_SPI_Transfer(0xFF);

            if (sd_spi_timeout)
            {
                SD_Deselect();
                sd_debug_error = 6;
                return 0;
            }
        }

        SD_Deselect();

        if (r7[2] != 0x01 || r7[3] != 0xAA)
        {
            sd_debug_error = 2;
            return 0;
        }

        cmd8_ok = 1;
        sd_card_type = 2;
    }
    else if (r & 0x04)
    {
        SD_Deselect();
        sd_card_type = 1;
    }
    else
    {
        SD_Deselect();
        sd_debug_error = 2;
        return 0;
    }

    /* CMD55 + ACMD41 */

    sd_debug_stage = 23;

    for (uint32_t tries = 0; tries < 1000; tries++)
    {
        sd_acmd41_tries = tries + 1;

        r = SD_SendCommand(55, 0, 0x01);
        sd_cmd55_response = r;

        SD_Deselect();

        if (sd_spi_timeout || r > 0x01)
        {
            sd_debug_error = 3;
            return 0;
        }

        r = SD_SendCommand(
                41,
                cmd8_ok ? 0x40000000U : 0U,
                0x01);

        sd_acmd41_response = r;

        SD_Deselect();

        if (sd_spi_timeout)
        {
            sd_debug_error = 6;
            return 0;
        }

        if (r == 0)
        {
            ready = 1;
            break;
        }

        if (r != 1)
        {
            sd_debug_error = 3;
            return 0;
        }

        if (!delay_ms(10))
            return 0;
    }

    if (!ready)
    {
        sd_debug_error = 3;
        return 0;
    }

    /* CMD58: read OCR */

    sd_debug_stage = 24;

    r = SD_SendCommand(58, 0, 0x01);
    sd_cmd58_response = r;

    if (sd_spi_timeout || r != 0)
    {
        SD_Deselect();
        sd_debug_error = 4;
        return 0;
    }

    for (uint32_t i = 0; i < 4; i++)
    {
        ocr[i] = SD_SPI_Transfer(0xFF);

        if (sd_spi_timeout)
        {
            SD_Deselect();
            sd_debug_error = 6;
            return 0;
        }
    }

    SD_Deselect();

    sd_ocr = ((uint32_t)ocr[0] << 24) |
             ((uint32_t)ocr[1] << 16) |
             ((uint32_t)ocr[2] << 8) |
             (uint32_t)ocr[3];

    if ((sd_ocr & 0x80000000U) == 0)
    {
        sd_debug_error = 5;
        return 0;
    }

    if (cmd8_ok)
    {
        sd_card_type =
            ((sd_ocr & 0x40000000U) != 0) ? 3 : 2;
    }

    /* SDSC requires CMD16 */

    if (sd_card_type != 3)
    {
        r = SD_SendCommand(16, 512, 0x01);

        SD_Deselect();

        if (sd_spi_timeout || r != 0)
        {
            sd_debug_error = 8;
            return 0;
        }
    }

    sd_init_ok = 1;
    sd_debug_stage = 30;

    return 1;
}

/* =========================================================
   READ ONE 512-BYTE BLOCK
   ========================================================= */

static uint8_t SD_ReadBlock(uint32_t sector,
                            uint8_t *buffer)
{
    if (!buffer)
        return 0;

    sd_spi_timeout = 0;

    uint32_t address = sector;

    if (sd_card_type != 3)
    {
        if (sector > 0xFFFFFFFFU / 512U)
            return 0;

        address = sector * 512U;
    }

    uint8_t response =
        SD_SendCommand(17, address, 0x01);

    if (response != 0 || sd_spi_timeout)
    {
        SD_Deselect();
        return 0;
    }

    uint8_t token = 0xFF;

    for (uint32_t i = 0; i < SD_READY_TIMEOUT; i++)
    {
        token = SD_SPI_Transfer(0xFF);

        if (sd_spi_timeout)
        {
            SD_Deselect();
            return 0;
        }

        if (token == SD_TOKEN_START)
            break;
    }

    if (token != SD_TOKEN_START)
    {
        SD_Deselect();
        return 0;
    }

    for (uint32_t i = 0; i < 512; i++)
    {
        buffer[i] = SD_SPI_Transfer(0xFF);

        if (sd_spi_timeout)
        {
            SD_Deselect();
            return 0;
        }
    }

    /* Discard CRC */

    (void)SD_SPI_Transfer(0xFF);
    (void)SD_SPI_Transfer(0xFF);

    if (sd_spi_timeout)
    {
        SD_Deselect();
        return 0;
    }

    SD_Deselect();

    return 1;
}

/* =========================================================
   WRITE ONE 512-BYTE BLOCK
   ========================================================= */

static uint8_t SD_WriteBlock(uint32_t sector,
                             const uint8_t *buffer)
{
    if (!buffer)
        return 0;

    sd_spi_timeout = 0;

    uint32_t address = sector;

    if (sd_card_type != 3)
    {
        if (sector > 0xFFFFFFFFU / 512U)
            return 0;

        address = sector * 512U;
    }

    uint8_t response =
        SD_SendCommand(24, address, 0x01);

    if (response != 0 || sd_spi_timeout)
    {
        SD_Deselect();
        return 0;
    }

    if (!SD_WaitReady())
    {
        SD_Deselect();
        return 0;
    }

    (void)SD_SPI_Transfer(SD_TOKEN_START);

    for (uint32_t i = 0; i < 512; i++)
    {
        (void)SD_SPI_Transfer(buffer[i]);

        if (sd_spi_timeout)
        {
            SD_Deselect();
            return 0;
        }
    }

    /* Dummy CRC */

    (void)SD_SPI_Transfer(0xFF);
    (void)SD_SPI_Transfer(0xFF);

    response = SD_SPI_Transfer(0xFF);

    if (sd_spi_timeout ||
        ((response & 0x1FU) != SD_TOKEN_ACCEPT))
    {
        SD_Deselect();
        return 0;
    }

    if (!SD_WaitReady())
    {
        SD_Deselect();
        return 0;
    }

    SD_Deselect();

    return 1;
}

/* =========================================================
   FAT32 CLUSTER AND FAT TABLE FUNCTIONS
   ========================================================= */

static uint32_t FAT32_ClusterLBA(uint32_t cluster)
{
    return fat_first_data +
           (cluster - 2U) * fat_sectors_per_cluster;
}

static uint8_t FAT32_ReadFAT(uint32_t cluster,
                            uint32_t *value)
{
    uint32_t byte_offset = cluster * 4U;

    uint32_t sector =
        fat_first_fat + (byte_offset / 512U);

    uint32_t offset = byte_offset % 512U;

    if (!SD_ReadBlock(sector, fat_sector))
        return 0;

    *value = rd32(&fat_sector[offset]) & 0x0FFFFFFFU;

    return 1;
}

static uint8_t FAT32_WriteFAT(uint32_t cluster,
                             uint32_t value)
{
    uint32_t byte_offset = cluster * 4U;

    uint32_t rel_sector = byte_offset / 512U;
    uint32_t offset = byte_offset % 512U;

    for (uint32_t copy = 0; copy < fat_num_fats; copy++)
    {
        uint32_t sector =
            fat_first_fat +
            copy * fat_sectors_per_fat +
            rel_sector;

        if (!SD_ReadBlock(sector, fat_sector))
            return 0;

        uint32_t old = rd32(&fat_sector[offset]);

        wr32(&fat_sector[offset],
             (old & 0xF0000000U) |
             (value & 0x0FFFFFFFU));

        if (!SD_WriteBlock(sector, fat_sector))
            return 0;
    }

    return 1;
}

/* =========================================================
   ALLOCATE FREE CLUSTER
   ========================================================= */

static uint8_t FAT32_AllocCluster(uint32_t *allocated)
{
    uint32_t start = fat_next_free_hint;

    if (start < 2 ||
        start >= fat_cluster_count_internal + 2)
    {
        start = 2;
    }

    uint32_t c = start;

    do
    {
        uint32_t value;

        if (!FAT32_ReadFAT(c, &value))
            return 0;

        if (value == 0)
        {
            if (!FAT32_WriteFAT(c, 0x0FFFFFFFU))
                return 0;

            *allocated = c;

            fat_next_free_hint = c + 1;

            if (fat_next_free_hint >=
                fat_cluster_count_internal + 2)
            {
                fat_next_free_hint = 2;
            }

            return 1;
        }

        c++;

        if (c >= fat_cluster_count_internal + 2)
            c = 2;

    } while (c != start);

    return 0;
}

/* =========================================================
   FREE OLD FILE CLUSTER CHAIN
   ========================================================= */

static uint8_t FAT32_FreeChain(uint32_t first)
{
    uint32_t c = first;
    uint32_t guard = 0;

    while (c >= 2 &&
           c < fat_cluster_count_internal + 2 &&
           guard++ < fat_cluster_count_internal)
    {
        uint32_t next;

        if (!FAT32_ReadFAT(c, &next))
            return 0;

        if (!FAT32_WriteFAT(c, 0))
            return 0;

        if (next >= 0x0FFFFFF8U ||
            next == 0x0FFFFFFFU)
        {
            break;
        }

        if (next < 2 ||
            next >= fat_cluster_count_internal + 2)
        {
            return 0;
        }

        c = next;
    }

    return 1;
}

/* =========================================================
   FAT32 MOUNT
   ========================================================= */

static uint8_t FAT32_Mount(void)
{
    uint32_t volume = 0;
    uint8_t boot[512];

    fat32_error = 0;
    fat_mounted = 0;
    fat32_mount_ok = 0;

    if (!SD_ReadBlock(0, boot))
    {
        fat32_error = 1;
        return 0;
    }

    /*
     * Check for FAT32 superfloppy layout.
     */

    if (boot[510] == 0x55 &&
        boot[511] == 0xAA &&
        rd16(&boot[11]) == 512 &&
        rd16(&boot[14]) != 0 &&
        boot[16] != 0 &&
        rd32(&boot[36]) != 0 &&
        rd32(&boot[44]) >= 2)
    {
        volume = 0;
    }
    else
    {
        /*
         * Search MBR partition table for FAT32.
         */

        if (boot[510] != 0x55 ||
            boot[511] != 0xAA)
        {
            fat32_error = 2;
            return 0;
        }

        uint32_t chosen = 0;

        for (uint32_t i = 0; i < 4; i++)
        {
            uint8_t *e = &boot[446 + i * 16];

            uint8_t type = e[4];

            uint32_t lba = rd32(&e[8]);

            if (lba &&
                (type == 0x0B ||
                 type == 0x0C ||
                 type == 0x1B ||
                 type == 0x1C))
            {
                chosen = lba;
                break;
            }
        }

        if (!chosen)
        {
            fat32_error = 3;
            return 0;
        }

        volume = chosen;

        if (!SD_ReadBlock(volume, boot))
        {
            fat32_error = 1;
            return 0;
        }
    }

    /*
     * Validate FAT32 BPB.
     */

    if (boot[510] != 0x55 ||
        boot[511] != 0xAA ||
        rd16(&boot[11]) != 512 ||
        boot[13] == 0 ||
        (boot[13] & (boot[13] - 1U)) != 0 ||
        rd16(&boot[14]) == 0 ||
        boot[16] == 0 ||
        rd32(&boot[36]) == 0)
    {
        fat32_error = 4;
        return 0;
    }

    fat_partition_start = volume;

    fat_sectors_per_cluster = boot[13];

    fat_reserved_sectors = rd16(&boot[14]);

    fat_num_fats = boot[16];

    fat_sectors_per_fat = rd32(&boot[36]);

    fat_root = rd32(&boot[44]) & 0x0FFFFFFFU;

    fat_volume_sectors = rd16(&boot[19]);

    if (fat_volume_sectors == 0)
        fat_volume_sectors = rd32(&boot[32]);

    if (fat_num_fats == 0 ||
        fat_num_fats > 2 ||
        fat_sectors_per_fat == 0 ||
        fat_volume_sectors == 0 ||
        fat_root < 2)
    {
        fat32_error = 5;
        return 0;
    }

    fat_first_fat =
        volume + fat_reserved_sectors;

    fat_first_data =
        fat_first_fat +
        fat_num_fats * fat_sectors_per_fat;

    if (fat_first_data <= volume ||
        fat_first_data >= volume + fat_volume_sectors)
    {
        fat32_error = 6;
        return 0;
    }

    fat_cluster_count_internal =
        (volume + fat_volume_sectors - fat_first_data) /
        fat_sectors_per_cluster;

    if (fat_cluster_count_internal < 1 ||
        fat_root >= fat_cluster_count_internal + 2 ||
        (fat_sectors_per_fat * 128U) <
        (fat_cluster_count_internal + 2U))
    {
        fat32_error = 7;
        return 0;
    }

    fat32_partition_lba = volume;
    fat32_total_sectors = fat_volume_sectors;
    fat32_fat_start = fat_first_fat;
    fat32_data_start = fat_first_data;
    fat32_root_cluster = fat_root;
    fat32_cluster_count = fat_cluster_count_internal;

    fat_mounted = 1;
    fat32_mount_ok = 1;

    return 1;
}

/* =========================================================
   FIND TEC_LOG.CSV IN ROOT DIRECTORY
   ========================================================= */

static uint8_t FAT32_FindRootEntry(
    uint32_t *entry_sector,
    uint32_t *entry_offset,
    uint32_t *old_cluster,
    uint32_t *old_size,
    uint8_t *found)
{
    uint32_t cluster = fat_root;

    uint8_t free_slot_found = 0;

    *found = 0;
    *old_cluster = 0;
    *old_size = 0;

    for (uint32_t chain = 0;
         chain < FAT32_MAX_ROOT_CLUSTERS;
         chain++)
    {
        uint32_t base = FAT32_ClusterLBA(cluster);

        for (uint32_t s = 0;
             s < fat_sectors_per_cluster;
             s++)
        {
            if (!SD_ReadBlock(base + s, file_sector))
                return 0;

            for (uint32_t off = 0;
                 off < 512;
                 off += 32)
            {
                uint8_t first = file_sector[off];

                if (first == 0x00)
                {
                    if (!free_slot_found)
                    {
                        *entry_sector = base + s;
                        *entry_offset = off;
                    }

                    return 1;
                }

                if (first == 0xE5)
                {
                    if (!free_slot_found)
                    {
                        *entry_sector = base + s;
                        *entry_offset = off;
                        free_slot_found = 1;
                    }
                }
                else if ((file_sector[off + 11] & 0x18U) == 0 &&
                         file_sector[off + 11] != 0x0F &&
                         memcmp(&file_sector[off],
                                "TEC_V2  CSV", 11) == 0)
                {
                    *entry_sector = base + s;
                    *entry_offset = off;

                    *old_cluster =
                        ((uint32_t)rd16(&file_sector[off + 20]) << 16) |
                        rd16(&file_sector[off + 26]);

                    *old_size = rd32(&file_sector[off + 28]);
                    *found = 1;

                    return 1;
                }
            }
        }

        uint32_t next;

        if (!FAT32_ReadFAT(cluster, &next))
            return 0;

        if (next >= 0x0FFFFFF8U ||
            next == 0x0FFFFFFFU)
        {
            break;
        }

        if (next < 2 ||
            next >= fat_cluster_count_internal + 2)
        {
            return 0;
        }

        cluster = next;
    }

    return free_slot_found;
}


/* =========================================================
   CONTINUOUS CSV LOGGER (root file TEC_LOG.CSV)
   ========================================================= */
#define SD_LOG_PERIOD_MS 1000U
#define FAT32_EOC 0x0FFFFFFFU

static uint32_t log_dir_sector = 0;
static uint32_t log_dir_offset = 0;
static uint32_t log_first_cluster = 0;
static uint32_t log_file_size = 0;
static uint8_t log_ready = 0;
static uint8_t log_failed = 0;
static uint32_t log_rows_written = 0;

static uint8_t FAT32_UpdateLogSize(uint32_t size)
{
    if (!SD_ReadBlock(log_dir_sector, file_sector)) return 0;
    wr32(&file_sector[log_dir_offset + 28], size);
    return SD_WriteBlock(log_dir_sector, file_sector);
}

static uint8_t FAT32_LogWrite(const uint8_t *src, uint32_t len)
{
    uint32_t cluster_bytes = fat_sectors_per_cluster * 512U;
    uint32_t pos = log_file_size;
    uint32_t done = 0;

    if (!log_ready || !src || cluster_bytes == 0U ||
        len > (0xFFFFFFFFU - log_file_size)) return 0;

    while (done < len)
    {
        uint32_t target_index = pos / cluster_bytes;
        uint32_t in_cluster = pos % cluster_bytes;
        uint32_t cluster = log_first_cluster;
        uint32_t index = 0;
        uint32_t next;

        while (index < target_index)
        {
            if (!FAT32_ReadFAT(cluster, &next)) return 0;
            if (next >= 0x0FFFFFF8U || next == FAT32_EOC)
            {
                uint32_t allocated;
                if (!FAT32_AllocCluster(&allocated)) return 0;
                if (!FAT32_WriteFAT(cluster, allocated)) return 0;
                next = allocated;
            }
            if (next < 2U || next >= fat_cluster_count_internal + 2U) return 0;
            cluster = next;
            index++;
        }

        uint32_t sector_in_cluster = in_cluster / 512U;
        uint32_t byte_in_sector = in_cluster % 512U;
        uint32_t chunk = 512U - byte_in_sector;
        uint32_t cluster_remaining = cluster_bytes - in_cluster;
        if (chunk > cluster_remaining) chunk = cluster_remaining;
        if (chunk > (len - done)) chunk = len - done;

        uint32_t lba = FAT32_ClusterLBA(cluster) + sector_in_cluster;
        if (pos < log_file_size)
        {
            if (!SD_ReadBlock(lba, file_sector)) return 0;
        }
        else if (byte_in_sector == 0U)
        {
            memset(file_sector, 0, sizeof(file_sector));
        }
        else
        {
            if (!SD_ReadBlock(lba, file_sector)) return 0;
        }

        memcpy(&file_sector[byte_in_sector], &src[done], chunk);
        if (!SD_WriteBlock(lba, file_sector)) return 0;

        pos += chunk;
        done += chunk;
    }

    log_file_size = pos;
    if (!FAT32_UpdateLogSize(log_file_size)) return 0;
    return 1;
}

static uint8_t SD_LogInit(void)
{
    uint32_t dir_sector = 0, dir_offset = 0, old_cluster = 0, old_size = 0;
    uint8_t found = 0;

    log_ready = 0;
    log_failed = 0;
    log_file_size = 0;

    if (!FAT32_FindRootEntry(&dir_sector, &dir_offset,
                             &old_cluster, &old_size, &found)) return 0;

    log_dir_sector = dir_sector;
    log_dir_offset = dir_offset;

    if (found)
    {
        if (old_cluster < 2U || old_cluster >= fat_cluster_count_internal + 2U) return 0;
        log_first_cluster = old_cluster;
        log_file_size = old_size;
    }
    else
    {
        uint32_t cluster;
        if (!FAT32_AllocCluster(&cluster)) return 0;
        log_first_cluster = cluster;

        if (!SD_ReadBlock(log_dir_sector, file_sector)) return 0;
        uint8_t *e = &file_sector[log_dir_offset];
        memset(e, 0, 32);
        memcpy(e, "TEC_V2  CSV", 11);
        e[11] = 0x20;
        wr16(&e[20], (uint16_t)(cluster >> 16));
        wr16(&e[26], (uint16_t)cluster);
        wr32(&e[28], 0U);
        if (!SD_WriteBlock(log_dir_sector, file_sector)) return 0;
    }

    log_ready = 1;

    if (!found || log_file_size == 0U)
    {
        static const uint8_t header[] =
            "time_ms,tec_voltage_V,tec_current_A,battery_voltage_V,battery_current_A,"
            "cs_fan_current_A,hs_fan_current_A,pi_provisional_integral_A,"
            "cold_side_temp_C,cold_side_valid,ambient_temp_C,hot_sink_temp_C,flask_top_temp_C,"
            "flask_bottom_temp_C,temp_valid_mask,soc_voltage_percent,hot_inhibited,lid_open,"
            "cooling_requested,fault,calibration_status,temp_sequence\r\n";
        if (!FAT32_LogWrite(header, (uint32_t)(sizeof(header) - 1U)))
        {
            log_ready = 0;
            return 0;
        }
    }

    return 1;
}


#define ESP_UART_BAUD     115200U
#define ESP_UART_PCLK_HZ  80000000U
#define ESP_UART_ENABLE   1U            /* 0 = no printing to the ESP32 */

volatile uint32_t esp_uart_lines_sent = 0U;
volatile uint32_t esp_uart_tx_timeouts = 0U;

static void ESP_UART_Init(void)
{
    RCC->AHB2ENR  |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    (void)RCC->APB1ENR1;

    /* PA2 = AF7 (USART2_TX), push-pull, high speed. Other PA pins untouched. */
    GPIOA->MODER   = (GPIOA->MODER & ~(3U << (2U * 2U))) | (2U << (2U * 2U));
    GPIOA->OTYPER &= ~(1U << 2U);
    GPIOA->OSPEEDR |= (3U << (2U * 2U));
    GPIOA->PUPDR   &= ~(3U << (2U * 2U));
    GPIOA->AFR[0]  = (GPIOA->AFR[0] & ~(0xFU << (2U * 4U))) | (7U << (2U * 4U));

    USART2->CR1 = 0U;
    USART2->BRR = (ESP_UART_PCLK_HZ + (ESP_UART_BAUD / 2U)) / ESP_UART_BAUD;   /* 694 -> 115274 baud */
    USART2->CR1 = USART_CR1_TE | USART_CR1_UE;
}

static void ESP_UART_Write(const char *buf, uint32_t len)
{
#if ESP_UART_ENABLE
    for (uint32_t i = 0U; i < len; i++)
    {
        uint32_t timeout = 400000U;

        while (!(USART2->ISR & USART_ISR_TXE) && --timeout)
        {
        }

        if (timeout == 0U)
        {
            esp_uart_tx_timeouts++;
            return;
        }

        USART2->TDR = (uint8_t)buf[i];
        PH_UART_BYTE_HOOK((uint8_t)buf[i]);
    }
#else
    (void)buf; (void)len;
#endif
}

static int32_t LogMilli(float value)
{
    if (value >= 0.0f) return (int32_t)(value * 1000.0f + 0.5f);
    return (int32_t)(value * 1000.0f - 0.5f);
}


static void LogValue(char out[16], float value)
{
    if (!isfinite(value) || value < -2147483.0f || value > 2147483.0f)
    {
        strcpy(out, "NA");
        return;
    }
    int32_t m = LogMilli(value);
    uint32_t u = (m < 0) ? (uint32_t)(-(int64_t)m) : (uint32_t)m;
    snprintf(out, 16, "%s%lu.%03lu", (m < 0) ? "-" : "",
             (unsigned long)(u / 1000U), (unsigned long)(u % 1000U));
}


static uint8_t Log_Sample(uint32_t time_ms)
{
    uint8_t valid = (uint8_t)((temperatures.valid_mask >> PH_COLD) & 1U);
    float temp = temperatures.c[PH_COLD];

    const float cols[8] = {
        tec_voltage, tec_current, batt_voltage, batt_current,
        cs_fan_current, hs_fan_current, pi_provisional_integral, temp
    };

    char v[8][16];
    for (unsigned i = 0U; i < 8U; ++i) LogValue(v[i], cols[i]);

#if !FAN_ISNS_ENABLE
    strcpy(v[4], "NA");
    strcpy(v[5], "NA");
#endif
    if (!valid) strcpy(v[7], "NA");

    char extra[5][16];
    LogValue(extra[0], temperatures.c[PH_AMBIENT]);
    LogValue(extra[1], temperatures.c[PH_HOT]);
    LogValue(extra[2], temperatures.c[PH_TOP]);
    LogValue(extra[3], temperatures.c[PH_BOTTOM]);
    LogValue(extra[4], battery_sampler.valid ? battery_soc_percent : NAN);
    char line[512];
    int n = snprintf(line, sizeof line,
                     "%lu,%s,%s,%s,%s,%s,%s,%s,%s,%u,%s,%s,%s,%s,%lu,%s,%u,%u,%lu,%lu,%lu,%lu\r\n",
                     (unsigned long)time_ms, v[0], v[1], v[2], v[3],
                     v[4], v[5], v[6], v[7], (unsigned)valid,
                     extra[0], extra[1], extra[2], extra[3], (unsigned long)temperatures.valid_mask,
                     extra[4], (unsigned)hot_inhibited, (unsigned)feature_lid.open,
                     (unsigned long)ledState, (unsigned long)firmware_error,
                     (unsigned long)calibration_status, (unsigned long)temperatures.sequence);
    if (n < 0 || (size_t)n >= sizeof line) return 0U;

    /* Send the same CSV row to ESP and SD: no separate column mapping. */
    ESP_UART_Write(line, (uint32_t)n);

    if (log_ready && !log_failed)
    {
        if (!FAT32_LogWrite((const uint8_t *)line, (uint32_t)n)) return 0U;
        log_rows_written++;
    }
    return 1U;
}

static uint32_t control_missed_deadlines;
#include "phloton_feature_app.inc"

/* ---------------- MAIN ---------------- */
int main(void)
{
#if (__FPU_PRESENT == 1U)
    SCB->CPACR |= (0xFUL << 20U);
    __DSB();
    __ISB();
#endif

    LM51772_GPIO_Init();        /* Enables GPIOB clock, then asserts PB13 (LM51772 NRST) LOW */
    Fan_GPIO_Init();            /* Establish fan OFF command */
    TEC_HOT_LOW_GPIO_Init();    /* PC10 to its measured OFF level */

    SystemClock_Config();
    Clock_Init();
    DWT_Init();
    ph_lid_init(&feature_lid, 0U);
    ph_lid_init(&feature_alarm_lid, 0U);
    ph_battery_init(&battery_sampler, 0U);
    {
        uint32_t sequence;
        calibration_status = (uint32_t)ph_nvs_load(&ph_nvs_stm32, &battery_calibration, &sequence);
        calibration_sequence = sequence;
        calibration_pending = battery_calibration;
    }
    Features_BuzzerInit();

    ESP_UART_Init();                         /* PA2 -> ESP32 GPIO16 */
    {
        static const char hdr[] = "\r\nTEC logger started. Vtec/Itec = TEC voltage/current, Vbat/Ibat = battery, Ifan_cs = cold side fan current, Ifan_hs = hot side fan current, PIint = PI integral term, Tcold = cold sink temperature\r\n";
        ESP_UART_Write(hdr, (uint32_t)(sizeof(hdr) - 1U));
        ESP_UART_Write("Build: ", 7U);
        ESP_UART_Write(firmware_build_id, (uint32_t)(sizeof(firmware_build_id) - 1U));
        ESP_UART_Write("\r\n", 2U);
    }

    /* SD init uses TIM2 at 1 MHz; the PLL makes APB1/TIM2 80 MHz. */
    Delay_Init();
    SD_SPI_Init();
    if (SD_Init() && FAT32_Mount())
    {
        if (SD_LogInit()) sd_init_ok = 1U;
        else { sd_init_ok = 0U; log_failed = 1U; }
    }
    else
    {
        sd_init_ok = 0U;
        log_failed = 1U;
    }

    firmware_stage = 1U;
    //LM51772_GPIO_Init();
    I2C1_Init();
    GPIO_Inputs_Init();
    ADC1_Init();
    //Fan_GPIO_Init();
    //TEC_HOT_LOW_GPIO_Init();

    ShiftRegister_Write(0x00U);
    WS2812B_SetColor(0U, 0U, 0U);

    GPIO_Inputs_Read();
    button_previous_state = wakeup_sw_pin_state;
    button_current_state = wakeup_sw_pin_state;
    button_raw_last = wakeup_sw_pin_state;

    PI_Reset();
    SysTick_Start();
    firmware_stage = 2U;

    {
        uint32_t t_meas = ms_ticks;
        uint32_t t_ctrl = ms_ticks - CONTROL_PERIOD_MS;
        uint32_t t_log = ms_ticks;
        uint8_t was_on = 0U;

        while (1)
        {
            uint32_t now;
            GPIO_Inputs_Read();
            Features_Service();
            Button_LED_Task();
            Startup_Task();
            now = ms_ticks;

            if ((uint32_t)(now - t_meas) >= MEASURE_PERIOD_MS)
            {
                t_meas = now;
                tec_voltage = TEC_ReadVoltage();
                tec_current = TEC_ReadCurrent();
                cs_fan_current = CSFAN_ReadCurrent();
                hs_fan_current = HSFAN_ReadCurrent();
            }

            if (startup_state == STARTUP_RUN)
            {
                if (!was_on)
                {
                    was_on = 1U;
                    t_ctrl = now - CONTROL_PERIOD_MS;
                }
                uint32_t elapsed = (uint32_t)(now - t_ctrl);
                if (elapsed >= CONTROL_PERIOD_MS)
                {
                    t_ctrl = now;
                    if (elapsed >= 2U * CONTROL_PERIOD_MS)
                    {
                        control_missed_deadlines++;
                        TEC_StopFault(103U);   /* Don't integrate fictitious one-second samples. */
                    }
                    else
                    {
                        TEC_ControlStep();
                        LED_UpdateFromTemperature();
                    }
                }
            }
            else was_on = 0U;

            if ((uint32_t)(now - t_log) >= SD_LOG_PERIOD_MS)
            {
                t_log = now;
                if (!Log_Sample(now)) log_failed = 1U;
            }
        }
    }
}
