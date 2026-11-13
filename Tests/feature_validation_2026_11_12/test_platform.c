#include <stdint.h>
#include <windows.h>
#undef LPTR /* Windows allocation macro collides with the real CMSIS QSPI register. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "test_support.h"
uint32_t test_primask, SystemCoreClock=80000000U;
static void adc_poll(volatile uint32_t *,uint32_t,uint32_t);
static void capture_byte(uint8_t byte);
static void lid_pin(int open);
#define PH_ADC_POLL_HOOK adc_poll
#define PH_UART_BYTE_HOOK capture_byte
#define main stm32_application_main
#include "../../Core/Src/main.c"
#undef main
#include "../../Core/Src/phloton_nvs_stm32.c"

static uint16_t adc_values[19];
static unsigned adc_reads[19];
static int adc_failed_channel=-1, adc_overrun, frozen_clock;
static int inject_lid_pulse;
static char captured[2048];static unsigned captured_length;
static void capture_byte(uint8_t byte){if(captured_length<sizeof captured-1)captured[captured_length++]=(char)byte;captured[captured_length]=0;}
static void adc_poll(volatile uint32_t *reg,uint32_t mask,uint32_t expected)
{
    if(!frozen_clock)DWT->CYCCNT+=80000U;
    if(reg==&ADC1->ISR && mask==ADC_ISR_EOC){
        unsigned channel=(ADC1->SQR1>>6)&31U;
        if((int)channel==adc_failed_channel){ADC1->ISR=0;return;}
        ADC1->DR=adc_values[channel];ADC1->ISR=ADC_ISR_EOC|(adc_overrun?ADC_ISR_OVR:0U);adc_reads[channel]++;
        if(inject_lid_pulse){inject_lid_pulse=0;lid_pin(1);Features_InterruptTick();lid_pin(0);}
    }else *reg=(*reg&~mask)|expected;
}
static void allocate(uintptr_t address,size_t bytes)
{if(VirtualAlloc((void*)address,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)!=(void*)address){fprintf(stderr,"MMIO allocation failed %lu\n",GetLastError());ExitProcess(2);}}
static void lid_pin(int open)
{unsigned level=open?PH_LID_OPEN_LEVEL:!PH_LID_OPEN_LEVEL;GPIOC->IDR=(GPIOC->IDR&~(1UL<<7))|(level<<7);}
static uint16_t raw_temperature(float c)
{double r=10000*exp(3977*(1/(c+273.15)-1/298.15));return(uint16_t)(4095*(3*r/(10000+r))/3.3+0.5);}
static void reset(void)
{
    memset((void*)0x40000000,0,0x30000);memset((void*)0x48000000,0,0x10000);memset((void*)0x50040000,0,0x10000);
    ms_ticks=1000;ledState=0;tec_fault_latched=0;firmware_error=0;startup_state=STARTUP_IDLE;
    lm51772_init_ok=lm51772_converter_enabled=0;feature_irq_pause=feature_adc_fault=0;
    hot_inhibited=feature_irq_hot=0;feature_irq_valid=1;feature_irq_sample_ms=ms_ticks;
    test_primask=0;filter_initialised=0;pi_integral=0;adc_last_ok=1;
    memset(&temperatures,0,sizeof temperatures);temperatures.valid_mask=PH_TEMP_ALL;temperatures.time_ms=ms_ticks;temperatures.sequence=1;
    for(unsigned i=0;i<5;i++)temperatures.c[i]=25;
    for(unsigned i=0;i<19;i++)adc_values[i]=1861;
    memset(adc_reads,0,sizeof adc_reads);adc_failed_channel=-1;adc_overrun=frozen_clock=0;
    inject_lid_pulse=0;
    ph_lid_init(&feature_lid,ms_ticks);ph_lid_update(&feature_lid,0,ms_ticks-30);ph_lid_update(&feature_lid,0,ms_ticks);
    ph_lid_init(&feature_alarm_lid,ms_ticks);ph_buzzer_start(&feature_buzzer,PH_BEEP_NONE,ms_ticks);
    ph_battery_init(&battery_sampler,ms_ticks+10);ph_calibration_defaults(&battery_calibration);
    lid_pin(0);calibration_save_request=0;led_pattern=0;captured_length=0;captured[0]=0;
}
int main(void)
{
    allocate(0x40000000,0x30000);allocate(0x48000000,0x10000);allocate(0x50040000,0x10000);
    allocate(0xE0000000,0x100000);allocate(0x08000000,0x80000);allocate(0x1FFF0000,0x10000);
    *(uint16_t*)FLASH_SIZE_DATA_REGISTER=512;
    begin("F1 temperatures","P01","Real acquisition uses PC0-PC4 channel mapping and 16 samples each");
    reset();unsigned channels[]={1,2,3,4,13};for(unsigned i=0;i<5;i++)adc_values[channels[i]]=(uint16_t)(1800+50*i);
    Features_Temperatures(ms_ticks);EXPECT(temperatures.valid_mask==PH_TEMP_ALL);
    for(unsigned i=0;i<5;i++){EXPECT(temperatures.raw[i]==1800+50*i);EXPECT(adc_reads[channels[i]]==16);}
    finish();
    begin("F1 temperatures","P02","Real GPIO and sampling-time configuration covers every sensor");
    reset();ADC1_Init();EXPECT((GPIOC->MODER&0x3ff)==0x3ff);EXPECT(!(GPIOC->PUPDR&0x3ff));
    for(unsigned i=1;i<=6;i++)EXPECT(((ADC1->SMPR1>>(i*3))&7)==7);finish();
    begin("F1 temperatures","P03","ADC stuck conversion returns and requests direct shutdown");
    reset();adc_failed_channel=2;EXPECT(!ADC1_Read(2));EXPECT(feature_adc_fault);EXPECT(!adc_last_ok);
    EXPECT(GPIOB->BSRR==(1UL<<29));EXPECT(ADC1->CR&ADC_CR_ADSTP);finish();
    begin("F1 temperatures","P04","ADC timeout also terminates with a frozen DWT clock");
    reset();adc_failed_channel=2;frozen_clock=1;EXPECT(!ADC1_Read(2));EXPECT(feature_adc_fault);finish();
    begin("F1 temperatures","P05","ADC overrun and invalid channel rejected");
    reset();adc_overrun=1;EXPECT(!ADC1_Read(2));EXPECT(feature_adc_fault);reset();EXPECT(!ADC1_Read(19));EXPECT(!adc_last_ok);finish();
    begin("F1 temperatures","P06","Any individual sensor failure prevents cooling and latches fault");
    for(unsigned i=0;i<5;i++){reset();adc_values[channels[i]]=0;ledState=1;Features_Temperatures(ms_ticks);Features_Service();EXPECT(tec_fault_latched);EXPECT(!ledState);EXPECT(startup_state==STARTUP_IDLE);}
    finish();
    begin("F2 hot protection","P07","Hot cutoff pauses cooling keeps fans and retains user request");
    reset();ledState=1;startup_state=STARTUP_RUN;lm51772_init_ok=1;adc_values[3]=raw_temperature(65);
    Features_Temperatures(ms_ticks);Features_Service();EXPECT(hot_inhibited);EXPECT(ledState);EXPECT(!tec_fault_latched);EXPECT(startup_state==STARTUP_IDLE);EXPECT(fans_on);finish();
    begin("F2 hot protection","P08","Cooling restarts only after recovery with fresh qualification");
    adc_values[3]=raw_temperature(55);ms_ticks+=100;Features_Service();EXPECT(hot_inhibited);EXPECT(!Features_Ready());
    adc_values[3]=raw_temperature(45);ms_ticks+=100;Features_Service();EXPECT(!hot_inhibited);EXPECT(Features_Ready());
    Features_BeginStartup();EXPECT(startup_state==STARTUP_RESET_WAIT);EXPECT(GPIOB->BSRR==(1UL<<13));finish();
    begin("F3 lid interlock","P09","IRQ cuts power before foreground can process lid");
    reset();ledState=1;startup_state=STARTUP_RUN;lid_pin(1);SysTick_Handler();EXPECT(GPIOB->BSRR==(1UL<<29));EXPECT(feature_irq_pause&1);EXPECT(ledState);finish();
    begin("F3 lid interlock","P10","Close after IRQ still waits 30 ms then restarts configuration");
    lid_pin(0);Features_Service();EXPECT(!Features_Ready());ms_ticks+=29;Features_Service();EXPECT(!Features_Ready());
    ms_ticks++;Features_Service();EXPECT(Features_Ready());Features_BeginStartup();EXPECT(startup_state==STARTUP_RESET_WAIT);finish();
    begin("F3 lid interlock","P11","Every startup phase can be cancelled by lid without latching a fault");
    for(unsigned state=1;state<=7;state++){reset();ledState=1;startup_state=(uint8_t)state;lid_pin(1);Startup_Task();EXPECT(startup_state==STARTUP_IDLE);EXPECT(ledState);EXPECT(!tec_fault_latched);}finish();
    begin("F3 lid interlock","P12","Fault or user OFF prevents automatic resume");
    reset();tec_fault_latched=1;ledState=1;EXPECT(!Features_Ready());Startup_Task();EXPECT(startup_state==STARTUP_IDLE);
    reset();EXPECT(!Features_Ready());Startup_Task();EXPECT(startup_state==STARTUP_IDLE);finish();
    begin("F2 hot protection","P13","IRQ stale-sample cutoff works during blocked foreground and rollover");
    reset();ledState=1;feature_irq_sample_ms=UINT32_MAX-100;ms_ticks=149;
    Features_InterruptTick();EXPECT(!(feature_irq_pause&8));ms_ticks=150;Features_InterruptTick();EXPECT(feature_irq_pause&8);EXPECT(GPIOB->BSRR==(1UL<<29));finish();
    begin("F4 buzzer","P14","TIM1 CH3 PA10 5 kHz carrier leaves PA8 configuration alone");
    reset();GPIOA->MODER=1UL<<16;Features_BuzzerInit();EXPECT(TIM1->PSC==79);EXPECT(TIM1->ARR==199);EXPECT(TIM1->CCR3==0);
    EXPECT((TIM1->CCER&TIM_CCER_CC3E)!=0);EXPECT(((GPIOA->AFR[1]>>8)&15)==1);EXPECT((GPIOA->MODER&(3UL<<16))==(1UL<<16));finish();
    begin("F4 buzzer","P15","Real interrupt services wake and sleep tone without advancing foreground");
    reset();Features_Beep(PH_BEEP_WAKE);Features_InterruptTick();EXPECT(TIM1->CCR3==100);
    ms_ticks+=200;Features_InterruptTick();EXPECT(TIM1->CCR3==0);ms_ticks+=100;Features_InterruptTick();EXPECT(TIM1->CCR3==100);
    Features_Beep(PH_BEEP_SLEEP);ms_ticks+=499;Features_InterruptTick();EXPECT(TIM1->CCR3==100);ms_ticks++;Features_InterruptTick();EXPECT(TIM1->CCR3==0);finish();
    begin("F4 buzzer","P16","IRQ raises lid alarm at twenty seconds without any foreground service");
    reset();ledState=1;lid_pin(1);Features_InterruptTick();ms_ticks+=19999;Features_InterruptTick();EXPECT(feature_buzzer.kind==PH_BEEP_NONE);
    ms_ticks++;Features_InterruptTick();EXPECT(feature_buzzer.kind==PH_BEEP_LID);EXPECT(TIM1->CCR3==100);
    lid_pin(0);Features_InterruptTick();EXPECT(feature_buzzer.kind==PH_BEEP_NONE);EXPECT(!TIM1->CCR3);finish();
    begin("F4 buzzer","P17","Buzzer request preserves a preexisting interrupt mask");
    reset();test_primask=1;Features_Beep(PH_BEEP_WAKE);EXPECT(test_primask==1);finish();
    begin("F5 calibration NVS","P18","Save request rejects active cooling invalid key and invalid coefficients");
    reset();ledState=1;calibration_save_request=0x43414c31;Features_CalibrationService();EXPECT(calibration_status==4);
    reset();calibration_save_request=123;Features_CalibrationService();EXPECT(calibration_status==3);
    reset();calibration_pending=battery_calibration;calibration_pending.voltage_slope=NAN;calibration_save_request=0x43414c31;Features_CalibrationService();EXPECT(calibration_status==3);finish();
    begin("F5 calibration NVS","P19","Calibration region read checks and ECC recovery are restricted");
    reset();uint32_t value[16];EXPECT(!nvs_read(0,2,0,value,sizeof value));EXPECT(!nvs_read(0,0,2044,value,8));EXPECT(!nvs_read(0,0,1,value,4));
    nvs_reading=0;FLASH->ECCR=FLASH_ECCR_ECCD|0x7f000;EXPECT(!ph_nvs_ecc_nmi());nvs_reading=1;EXPECT(ph_nvs_ecc_nmi());EXPECT(nvs_ecc_error);
    FLASH->ECCR=FLASH_ECCR_ECCD|0x10;EXPECT(!ph_nvs_ecc_nmi());nvs_reading=0;finish();
    begin("F6 voltage SOC LEDs","P20","Real indicator uses calibrated voltage masks and clears on OFF");
    reset();ledState=1;battery_sampler.valid=1;battery_sampler.time_ms=ms_ticks;batt_voltage=9;
    Features_Service();EXPECT(led_pattern==1);batt_voltage=10.5f;Features_Service();EXPECT(led_pattern==7);
    ledState=0;Features_Service();EXPECT(!led_pattern);finish();
    begin("F6 voltage SOC LEDs","P21","Stale or missing battery sample clears indicator");
    reset();ledState=1;led_pattern=31;battery_sampler.valid=0;Features_Service();EXPECT(!led_pattern);
    battery_sampler.valid=1;battery_sampler.time_ms=ms_ticks-1001;battery_sampler.next_ms=ms_ticks+100;Features_Service();EXPECT(!led_pattern);finish();
    begin("Integration telemetry","P22","UART row contains all 22 fields with five sensor values");
    reset();USART2->ISR=USART_ISR_TXE;log_ready=0;battery_sampler.valid=1;battery_soc_percent=55;
    for(unsigned i=0;i<5;i++)temperatures.c[i]=(float)(21+i);
    EXPECT(Log_Sample(ms_ticks));unsigned commas=0;for(unsigned i=0;i<captured_length;i++)if(captured[i]==',')commas++;
    EXPECT(commas==21);EXPECT(strstr(captured,"22.000,1,21.000,23.000,24.000,25.000,31,55.000")!=0);
    EXPECT(captured_length>2&&captured[captured_length-2]=='\r'&&captured[captured_length-1]=='\n');finish();
    begin("Regression","P23","Original current-code mapping PI limits and reset remain intact");
    reset();for(unsigned i=0;i<=130;i++)EXPECT(CurrentToILIMCode(0.5f+0.05f*i)==10+i);
    EXPECT(PI_Controller(3,2)==MIN_CURRENT);EXPECT(PI_Controller(3,100)==MAX_CURRENT);
    ledState=1;startup_state=STARTUP_RUN;pi_integral=3;Features_Pause();EXPECT(pi_integral==0);finish();
    begin("F3 lid interlock","P24","Simultaneous hot and lid inhibit needs both to recover");
    reset();ledState=1;hot_inhibited=1;feature_irq_hot=1;lid_pin(1);Features_InterruptTick();EXPECT((feature_irq_pause&3)==3);
    lid_pin(0);EXPECT(!Features_Ready());hot_inhibited=0;EXPECT(!Features_Ready());finish();
    begin("F3 lid interlock","P25","Lid pulse during ADC publication cannot be lost by foreground flag clearing");
    reset();ledState=1;temperatures.sequence=0;inject_lid_pulse=1;Features_Service();EXPECT(feature_irq_pause&1);EXPECT(!Features_Ready());
    Features_Service();EXPECT(feature_lid.open);ms_ticks+=29;Features_Service();EXPECT(!Features_Ready());ms_ticks++;Features_Service();EXPECT(Features_Ready());finish();
    begin("F5 calibration NVS","P26","Failed flash save retains active coefficients and reports failure");
    reset();memset((void*)PH_NVS_BASE,255,4096);FLASH->CR=FLASH_CR_LOCK;
    calibration_pending=battery_calibration;calibration_pending.voltage_offset=0.25f;calibration_save_request=0x43414c31;
    Features_CalibrationService();EXPECT(calibration_status==5);EXPECT(battery_calibration.voltage_offset==0);finish();
    fprintf(stderr,"Assertions: %u; failures: %u\n",assertions,failures);
    return failures?1:0;
}
