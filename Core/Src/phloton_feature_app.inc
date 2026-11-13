/* Application integration: included once by main.c after the register drivers. */
const char firmware_build_id[] = "PHV4-SIX-FEATURES-20261112-A";

static void Features_Beep(uint8_t kind)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    ph_buzzer_start(&feature_buzzer, kind, ms_ticks);
    if (!saved) __enable_irq();
}

static void Features_BuzzerInit(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    (void)RCC->APB2ENR;
    GPIOA->BSRR = 1UL << (10U + 16U);
    GPIOA->OTYPER &= ~(1UL << 10U);
    GPIOA->PUPDR &= ~(3UL << 20U);
    GPIOA->MODER = (GPIOA->MODER & ~(3UL << 20U)) | (2UL << 20U);
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~(15UL << 8U)) | (1UL << 8U);
    TIM1->CR1 = 0U;
    TIM1->PSC = SystemCoreClock / 1000000U - 1U;
    TIM1->ARR = 199U; /* 5 kHz carrier; 50% duty only during a scheduled beep. */
    TIM1->CCR3 = 0U;
    TIM1->CCMR2 = (6UL << 4U) | TIM_CCMR2_OC3PE;
    TIM1->CCER = TIM_CCER_CC3E;
    TIM1->BDTR = TIM_BDTR_MOE;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

static void Features_InterruptTick(void)
{
    /* ISR reads only atomically published flags/time, never the ADC or I2C. */
    uint8_t reason = 0U;
    int raw_open = ((GPIOC->IDR >> LID_OPEN_PIN) & 1U) == PH_LID_OPEN_LEVEL;
    ph_lid_update(&feature_alarm_lid, raw_open, ms_ticks);
    if (ph_lid_alarm_due(&feature_alarm_lid, ledState && !tec_fault_latched, ms_ticks) &&
        feature_buzzer.kind == PH_BEEP_NONE)
        ph_buzzer_start(&feature_buzzer, PH_BEEP_LID, ms_ticks);
    if ((!raw_open || !ledState) && feature_buzzer.kind == PH_BEEP_LID)
        ph_buzzer_start(&feature_buzzer, PH_BEEP_NONE, ms_ticks);
    if (ledState) {
        if (raw_open) reason |= 1U;
        if (feature_irq_hot) reason |= 2U;
        if (!feature_irq_valid || feature_adc_fault) reason |= 4U;
        if ((uint32_t)(ms_ticks - feature_irq_sample_ms) > PH_TEMP_STALE_MS) reason |= 8U;
        if (reason) {
            GPIOB->BSRR = 1UL << (13U + 16U); /* Direct cutoff during a blocked SD/UART call. */
            feature_irq_pause |= reason;
        }
    }
    TIM1->CCR3 = ph_buzzer_level(&feature_buzzer, ms_ticks) ? 100U : 0U;
}

static int Features_Ready(void)
{
    return ledState && !tec_fault_latched && !feature_adc_fault && !feature_lid.open &&
           !hot_inhibited && !feature_irq_pause && ph_temperatures_fresh(&temperatures, ms_ticks) &&
           (((GPIOC->IDR >> LID_OPEN_PIN) & 1U) != PH_LID_OPEN_LEVEL);
}

static void Features_Pause(void)
{
    GPIOB->BSRR = 1UL << (13U + 16U);
    if (startup_state != STARTUP_IDLE || lm51772_init_ok) {
        startup_state = STARTUP_IDLE;
        lm51772_init_ok = lm51772_converter_enabled = 0U;
        ilim_readback_match = 0U;
        ILIM_THRESHOLD_VALUE = ILIM_CODE_MIN;
        pi_requested_current_a = 0.0f;
        filter_initialised = 0U;
        PI_Reset();
    }
    /* Keep heat removal available through a hot pause, even after user OFF. */
    Fans_Set(hot_inhibited && (temperatures.valid_mask & (1U << PH_HOT)) ? 1U : 0U);
}

static void Features_BeginStartup(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    if (Features_Ready()) {
        ILIM_THRESHOLD_VALUE = ILIM_CODE_MIN;
        ilim_code_requested = ILIM_CODE_MIN;
        filter_initialised = 0U;
        PI_Reset();
        Fans_Set(1U);
        GPIOB->BSRR = 1UL << 13U;
        startup_t0 = ms_ticks;
        startup_state = STARTUP_RESET_WAIT;
    }
    if (!saved) __enable_irq();
}

static void Features_Temperatures(uint32_t now)
{
    static const uint8_t channels[PH_TEMP_COUNT] = {1U, 2U, 3U, 4U, 13U};
    uint16_t raw[PH_TEMP_COUNT] = {0};
    uint32_t ok_mask = 0U;
    for (unsigned sensor = 0U; sensor < PH_TEMP_COUNT; ++sensor) {
        uint32_t sum = 0U;
        unsigned n;
        for (n = 0U; n < THERMISTOR_SAMPLES; ++n) {
            sum += ADC1_Read(channels[sensor]);
            if (!adc_last_ok) break;
        }
        if (n == THERMISTOR_SAMPLES) {
            raw[sensor] = (uint16_t)((sum + THERMISTOR_SAMPLES / 2U) / THERMISTOR_SAMPLES);
            ok_mask |= 1U << sensor;
        }
    }
    ph_temperatures_decode(&temperatures, raw, ok_mask, now);
    ph_hot_update(&hot_inhibited, temperatures.c[PH_HOT],
                  (temperatures.valid_mask & (1U << PH_HOT)) != 0U);
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    feature_irq_sample_ms = now;
    feature_irq_valid = temperatures.valid_mask == PH_TEMP_ALL;
    feature_irq_hot = hot_inhibited;
    if (!saved) __enable_irq();
}

static void Features_CalibrationService(void)
{
    ph_calibration_t candidate;
    uint32_t request = calibration_save_request, sequence;
    if (!request) return;
    calibration_save_request = 0U;
    if (request != 0x43414C31U) { calibration_status = 3U; return; }
    if (ledState || startup_state != STARTUP_IDLE || (GPIOB->ODR & (1UL << 13U))) {
        calibration_status = 4U;
        return;
    }
    candidate.voltage_slope = calibration_pending.voltage_slope;
    candidate.voltage_offset = calibration_pending.voltage_offset;
    candidate.current_slope = calibration_pending.current_slope;
    candidate.current_offset = calibration_pending.current_offset;
    if (!ph_calibration_valid(&candidate)) { calibration_status = 3U; return; }
    if (!ph_nvs_save(&ph_nvs_stm32, &candidate) ||
        !ph_nvs_load(&ph_nvs_stm32, &candidate, &sequence)) { calibration_status = 5U; return; }
    battery_calibration = candidate;
    calibration_sequence = sequence;
    calibration_status = 2U;
    ph_battery_init(&battery_sampler, ms_ticks); /* Never mix samples across calibration changes. */
}

static void Features_Service(void)
{
    static uint32_t temp_at;
    uint32_t now = ms_ticks;
    int raw_open = ((GPIOC->IDR >> LID_OPEN_PIN) & 1U) == PH_LID_OPEN_LEVEL;
    /* An open edge seen during blocked foreground still requires fresh close qualification. */
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    uint8_t lid_seen = feature_irq_pause & 1U;
    feature_irq_pause &= (uint8_t)~1U;
    if (!saved) __enable_irq();
    if (lid_seen && !feature_lid.open) ph_lid_update(&feature_lid, 1, now);
    ph_lid_update(&feature_lid, raw_open, now);
    if (!temperatures.sequence || (uint32_t)(now - temp_at) >= MEASURE_PERIOD_MS) {
        temp_at = now;
        Features_Temperatures(now);
    }
    if (ph_battery_due(&battery_sampler, now)) {
        uint32_t channel = battery_sampler.position < PH_BATTERY_SAMPLES ? BATT_ISNS_CHANNEL : BATT_VSNS_CHANNEL;
        uint16_t raw = ADC1_Read(channel);
        int complete = ph_battery_push(&battery_sampler, now, raw, adc_last_ok);
        if (complete > 0) {
            batt_isns_adc_raw = (uint16_t)battery_sampler.current_raw;
            batt_vsns_adc_raw = (uint16_t)battery_sampler.voltage_raw;
            batt_voltage = ph_battery_voltage(&battery_calibration, battery_sampler.voltage_raw);
            batt_current = ph_battery_current(&battery_calibration, battery_sampler.current_raw);
            battery_soc_percent = ph_soc_voltage(batt_voltage);
        }
    }
    if (ledState && !tec_fault_latched) {
        if (feature_adc_fault) TEC_StopFault(113U);
        else if (temperatures.valid_mask != PH_TEMP_ALL) TEC_StopFault(110U);
        else if (!ph_temperatures_fresh(&temperatures, now) || (feature_irq_pause & 8U)) TEC_StopFault(112U);
    }
    saved = __get_PRIMASK();
    __disable_irq();
    if (!feature_lid.open && !hot_inhibited &&
        (((GPIOC->IDR >> LID_OPEN_PIN) & 1U) != PH_LID_OPEN_LEVEL) &&
        ph_temperatures_fresh(&temperatures, now) && !feature_adc_fault)
        feature_irq_pause &= 1U; /* Preserve an unconsumed lid edge from acquisition. */
    if (!saved) __enable_irq();
    if (!Features_Ready()) Features_Pause();
    if ((!feature_lid.open || !ledState) && feature_buzzer.kind == PH_BEEP_LID) Features_Beep(PH_BEEP_NONE);
    uint8_t pattern = ledState && !tec_fault_latched ?
        ph_soc_leds(batt_voltage, battery_sampler.valid &&
                    (uint32_t)(now - battery_sampler.time_ms) <= 1000U) : 0U;
    if (pattern != led_pattern) {
        ShiftRegister_Write(pattern);
        led_pattern = pattern;
        led_is_on = pattern != 0U;
    }
    Features_CalibrationService();
}
