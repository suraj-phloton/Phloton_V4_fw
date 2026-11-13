#include "phloton_features.h"
#include "phloton_nvs.h"
#include "test_support.h"
#include <string.h>
#include <float.h>

typedef struct {
    uint8_t page[2][PH_NVS_PAGE_BYTES];
    unsigned writes, erases, fail_write, torn_bytes;
    int fail_erase, fail_read_slot;
} fake_flash_t;
static fake_flash_t flash;
static int rd(void *p,unsigned slot,unsigned off,void *dst,size_t n)
{ fake_flash_t *f=p; if ((int)slot==f->fail_read_slot) return 0; memcpy(dst,f->page[slot]+off,n); return 1; }
static int er(void *p,unsigned slot)
{ fake_flash_t *f=p; if (f->fail_erase) return 0; memset(f->page[slot],255,PH_NVS_PAGE_BYTES); f->erases++; return 1; }
static int wr(void *p,unsigned slot,unsigned off,const void *v)
{
    fake_flash_t *f=p; const uint8_t *src=v;
    unsigned bytes = f->writes == f->fail_write ? f->torn_bytes : 8U;
    for(unsigned i=0;i<bytes;i++) { if ((f->page[slot][off+i]&src[i])!=src[i]) return 0; f->page[slot][off+i]&=src[i]; }
    return f->writes++ != f->fail_write;
}
static ph_nvs_io_t io={&flash,rd,er,wr};
static void blank(void)
{ memset(&flash,0,sizeof flash); memset(flash.page,255,sizeof flash.page); flash.fail_write=UINT32_MAX; flash.fail_read_slot=-1; }
static void fixture_sequence(unsigned slot,uint32_t seq)
{
    uint32_t w[16],crc=UINT32_MAX;memcpy(w,flash.page[slot],sizeof w);w[2]=seq;
    const uint8_t *p=(const uint8_t*)w;
    for(unsigned i=0;i<52;i++){crc^=p[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^((crc&1)?0xEDB88320U:0U);}
    w[13]=~crc;memcpy(flash.page[slot],w,sizeof w);
}

int main(void)
{
    ph_temperatures_t t={0}; uint16_t raw[5]={1861,1861,1861,1861,1861};
    begin("F1 temperatures","T01","All five independent channels and nominal conversion");
    ph_temperatures_decode(&t,raw,PH_TEMP_ALL,100U);
    EXPECT(t.valid_mask==PH_TEMP_ALL); EXPECT(t.sequence==1); EXPECT(NEAR(t.c[0],25,0.1f));
    for(unsigned i=0;i<5;i++) EXPECT(NEAR(t.c[i],t.c[0],0.0001f)); finish();
    begin("F1 temperatures","T02","Each individual channel invalidates independently");
    for(unsigned i=0;i<5;i++) { ph_temperatures_decode(&t,raw,PH_TEMP_ALL^(1U<<i),100); EXPECT(t.valid_mask==(PH_TEMP_ALL^(1U<<i))); EXPECT(isnan(t.c[i])); }
    finish();
    begin("F1 temperatures","T03","Exhaustive 4096-code sweep for every sensor");
    for(unsigned i=0;i<5;i++) for(unsigned code=0;code<4096;code++) {
        int valid; float value=ph_ntc_temperature((uint16_t)code,&valid);
        EXPECT(valid ? isfinite(value)&&value>=-40&&value<=125 : isnan(value));
        raw[i]=(uint16_t)code; ph_temperatures_decode(&t,raw,PH_TEMP_ALL,100);
        EXPECT(!!(t.valid_mask&(1U<<i))==valid);
    }
    for(unsigned i=0;i<5;i++) raw[i]=1861;
    finish();
    begin("F1 temperatures","T04","Open short near rails and invalid output pointer");
    unsigned invalid[]={0,1,2,3,4092,4093,4094,4095,65535};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++){int valid=1; EXPECT(isnan(ph_ntc_temperature((uint16_t)invalid[i],&valid)));EXPECT(!valid);}
    EXPECT(isnan(ph_ntc_temperature(1861,0)));finish();
    begin("F1 temperatures","T05","Freshness boundary and unsigned wraparound");
    ph_temperatures_decode(&t,raw,PH_TEMP_ALL,UINT32_MAX-100U);
    EXPECT(ph_temperatures_fresh(&t,149U)); EXPECT(!ph_temperatures_fresh(&t,150U));
    EXPECT(!ph_temperatures_fresh(&t,t.time_ms-1U));finish();

    uint8_t hot=0;
    begin("F2 hot protection","H01","Exact trip and recovery with retained hysteresis");
    EXPECT(!ph_hot_update(&hot,59.999f,1)); EXPECT(ph_hot_update(&hot,60,1));
    EXPECT(ph_hot_update(&hot,55,1)); EXPECT(ph_hot_update(&hot,50.001f,1));
    EXPECT(!ph_hot_update(&hot,50,1)); EXPECT(!ph_hot_update(&hot,55,1));finish();
    begin("F2 hot protection","H02","NaN infinity invalid flag and recovery");
    EXPECT(ph_hot_update(&hot,NAN,1)); EXPECT(ph_hot_update(&hot,25,0));
    EXPECT(ph_hot_update(&hot,INFINITY,1)); EXPECT(ph_hot_update(&hot,-273.15f,1)); EXPECT(!ph_hot_update(&hot,25,1));finish();
    begin("F2 hot protection","H03","Repeated noisy temperature sweep stays inhibited until recovery");
    for(unsigned i=0;i<10000;i++){EXPECT(ph_hot_update(&hot,60.1f,1));EXPECT(ph_hot_update(&hot,50.1f,1));EXPECT(!ph_hot_update(&hot,49.9f,1));} finish();

    ph_lid_t lid;
    begin("F3 lid interlock","L01","Boot closed requires 30 ms qualification");
    ph_lid_init(&lid,0);ph_lid_update(&lid,0,0); EXPECT(lid.open);
    ph_lid_update(&lid,0,29);EXPECT(lid.open);ph_lid_update(&lid,0,30);EXPECT(!lid.open);finish();
    begin("F3 lid interlock","L02","Open immediate closure bounce resets timer");
    ph_lid_update(&lid,1,100);EXPECT(lid.open);ph_lid_update(&lid,0,101);
    ph_lid_update(&lid,1,120);ph_lid_update(&lid,0,121);ph_lid_update(&lid,0,150);EXPECT(lid.open);
    ph_lid_update(&lid,0,151);EXPECT(!lid.open);finish();
    begin("F3 lid interlock","L03","Closure qualification crosses millisecond rollover");
    ph_lid_init(&lid,UINT32_MAX-10);ph_lid_update(&lid,0,UINT32_MAX-10);
    ph_lid_update(&lid,0,18);EXPECT(lid.open);ph_lid_update(&lid,0,19);EXPECT(!lid.open);finish();

    ph_buzzer_t buzzer={0};
    begin("F4 buzzer","B01","Wake beep on-off-on boundaries");
    ph_buzzer_start(&buzzer,PH_BEEP_WAKE,0);
    unsigned times[]={0,199,200,299,300,699,700};int levels[]={1,1,0,0,1,1,0};
    for(unsigned i=0;i<7;i++) EXPECT(ph_buzzer_level(&buzzer,times[i])==levels[i]); EXPECT(!buzzer.kind);finish();
    begin("F4 buzzer","B02","Sleep tone and preemption");
    ph_buzzer_start(&buzzer,PH_BEEP_WAKE,0);ph_buzzer_start(&buzzer,PH_BEEP_SLEEP,100);
    EXPECT(ph_buzzer_level(&buzzer,599));EXPECT(!ph_buzzer_level(&buzzer,600));finish();
    begin("F4 buzzer","B03","Twenty second lid alarm and ten second repeat");
    ph_lid_init(&lid,100);EXPECT(!ph_lid_alarm_due(&lid,1,20099));
    EXPECT(ph_lid_alarm_due(&lid,1,20100));EXPECT(!ph_lid_alarm_due(&lid,1,30099));
    EXPECT(ph_lid_alarm_due(&lid,1,30100));EXPECT(!ph_lid_alarm_due(&lid,0,40100));finish();
    begin("F4 buzzer","B04","Alarm cancellation on close and OFF resets delay");
    ph_lid_init(&lid,0);EXPECT(!ph_lid_alarm_due(&lid,0,100000));
    EXPECT(!ph_lid_alarm_due(&lid,1,119999));EXPECT(ph_lid_alarm_due(&lid,1,120000));
    ph_lid_update(&lid,0,120001);ph_lid_update(&lid,0,120031);
    EXPECT(!ph_lid_alarm_due(&lid,1,140000));finish();
    begin("F4 buzzer","B05","Buzzer and alarm wraparound");
    ph_buzzer_start(&buzzer,PH_BEEP_SLEEP,UINT32_MAX-100);EXPECT(ph_buzzer_level(&buzzer,398));EXPECT(!ph_buzzer_level(&buzzer,399));
    ph_lid_init(&lid,UINT32_MAX-100);EXPECT(!ph_lid_alarm_due(&lid,1,19898));EXPECT(ph_lid_alarm_due(&lid,1,19899));finish();

    ph_calibration_t c,d;uint32_t seq;
    begin("F5 calibration NVS","N01","Blank storage nominal defaults without flash writes");
    blank();EXPECT(!ph_nvs_load(&io,&c,&seq));EXPECT(seq==0);EXPECT(flash.erases==0);EXPECT(ph_calibration_valid(&c));
    EXPECT(NEAR(ph_battery_voltage(&c,4095),36.3f,0.0001f));EXPECT(NEAR(ph_battery_current(&c,2047.5f),0,0.0001f));finish();
    begin("F5 calibration NVS","N02","Save reboot load newer record and previous copy retained");
    EXPECT(ph_nvs_save(&io,&c));EXPECT(ph_nvs_load(&io,&d,&seq));EXPECT(seq==1);EXPECT(!memcmp(&c,&d,sizeof c));
    c.voltage_offset=0.1f;EXPECT(ph_nvs_save(&io,&c));EXPECT(ph_nvs_load(&io,&d,&seq));EXPECT(seq==2);EXPECT(d.voltage_offset==0.1f);finish();
    fake_flash_t baseline=flash;
    begin("F5 calibration NVS","N03","Every single-bit record corruption falls back to previous copy");
    for(unsigned bit=0;bit<512;bit++){flash=baseline;flash.page[1][bit/8]^=(uint8_t)(1U<<(bit%8));EXPECT(ph_nvs_load(&io,&d,&seq));EXPECT(seq==1);EXPECT(d.voltage_offset==0);}
    finish();
    begin("F5 calibration NVS","N04","Power interruption at every write and partial double-word byte");
    for(unsigned stop=0;stop<8;stop++) {
      for(unsigned bytes=0;bytes<8;bytes++) {
        flash=baseline;flash.writes=0;flash.fail_write=stop;flash.torn_bytes=bytes;
        c.voltage_offset=0.2f;EXPECT(!ph_nvs_save(&io,&c));EXPECT(ph_nvs_load(&io,&d,&seq));EXPECT(seq==2);EXPECT(d.voltage_offset==0.1f);
      }
    }
    finish();
    begin("F5 calibration NVS","N05","Erase and read failure preserve usable calibration");
    flash=baseline;flash.fail_erase=1;EXPECT(!ph_nvs_save(&io,&c));EXPECT(ph_nvs_load(&io,&d,&seq));EXPECT(seq==2);
    flash=baseline;flash.fail_read_slot=1;EXPECT(ph_nvs_load(&io,&d,&seq));EXPECT(seq==1);finish();
    begin("F5 calibration NVS","N06","Both corrupted records use defaults without writes");
    flash=baseline;flash.page[0][0]^=1;flash.page[1][0]^=1;EXPECT(!ph_nvs_load(&io,&d,&seq));EXPECT(seq==0);EXPECT(d.voltage_offset==0);finish();
    begin("F5 calibration NVS","N07","Invalid calibration cannot erase or program flash");
    float bad[]={NAN,INFINITY,-INFINITY,0,-1,100};
    for(unsigned i=0;i<6;i++){blank();ph_calibration_defaults(&c);c.voltage_slope=bad[i];EXPECT(!ph_nvs_save(&io,&c));EXPECT(!flash.erases);}
    ph_calibration_defaults(&c);c.current_offset=NAN;EXPECT(!ph_calibration_valid(&c));c.current_offset=101;EXPECT(!ph_calibration_valid(&c));
    c.current_offset=0;c.current_slope=0;EXPECT(!ph_calibration_valid(&c));finish();
    begin("F5 calibration NVS","N08","Calibrated conversion fractional counts signed current and invalid raw");
    ph_calibration_defaults(&c);c.voltage_slope=0.01f;c.voltage_offset=-0.1f;c.current_slope=-0.002f;c.current_offset=4;
    EXPECT(NEAR(ph_battery_voltage(&c,900.5f),8.905f,0.0001f));EXPECT(NEAR(ph_battery_current(&c,2100),-0.2f,0.0001f));
    EXPECT(isnan(ph_battery_voltage(&c,NAN)));EXPECT(isnan(ph_battery_current(&c,4096)));EXPECT(isnan(ph_battery_voltage(0,1)));finish();

    begin("F5 calibration NVS","N09","Persisted generation counter wraps from maximum to zero");
    blank();ph_calibration_defaults(&c);EXPECT(ph_nvs_save(&io,&c));fixture_sequence(0,UINT32_MAX);
    EXPECT(ph_nvs_load(&io,&d,&seq));EXPECT(seq==UINT32_MAX);c.voltage_offset=0.3f;
    EXPECT(ph_nvs_save(&io,&c));EXPECT(ph_nvs_load(&io,&d,&seq));EXPECT(seq==0);EXPECT(d.voltage_offset==0.3f);
    EXPECT(ph_nvs_save(&io,&c));EXPECT(ph_nvs_load(&io,&d,&seq));EXPECT(seq==1);finish();
    begin("F5 calibration NVS","N10","Incomplete storage interface never erases calibration");
    blank();ph_nvs_io_t missing=io;missing.read=0;EXPECT(!ph_nvs_save(&missing,&c));EXPECT(!flash.erases);
    EXPECT(!ph_nvs_load(0,&d,&seq));EXPECT(ph_calibration_valid(&d));EXPECT(!ph_nvs_load(&io,0,&seq));finish();
    begin("F5 calibration NVS","N11","Lost readback acknowledgment accepts only a complete committed copy");
    blank();ph_calibration_defaults(&c);EXPECT(ph_nvs_save(&io,&c));c.voltage_offset=0.4f;
    flash.fail_read_slot=1;EXPECT(!ph_nvs_save(&io,&c));EXPECT(ph_nvs_load(&io,&d,&seq));EXPECT(seq==1);
    flash.fail_read_slot=-1;EXPECT(ph_nvs_load(&io,&d,&seq));EXPECT(seq==2);EXPECT(d.voltage_offset==0.4f);finish();
    begin("F6 voltage SOC LEDs","S01","User mapping endpoints clamp and intermediate levels");
    EXPECT(NEAR(ph_soc_voltage(9),10,0.0001f));EXPECT(NEAR(ph_soc_voltage(12.6f),100,0.001f));
    EXPECT(NEAR(ph_soc_voltage(10.8f),55,0.001f));EXPECT(ph_soc_voltage(0)==0);EXPECT(ph_soc_voltage(99)==100);finish();
    begin("F6 voltage SOC LEDs","S02","Every LED threshold below exact and above");
    float bounds[]={8.6f,9.4f,10.2f,11.0f,11.8f};unsigned masks[]={0,1,3,7,15};
    for(unsigned i=0;i<5;i++) {EXPECT(ph_soc_leds(bounds[i],1)==masks[i]);EXPECT(ph_soc_leds(nextafterf(bounds[i],-INFINITY),1)==masks[i]);EXPECT(ph_soc_leds(nextafterf(bounds[i],INFINITY),1)==(unsigned)((1U<<(i+1))-1U));}
    EXPECT(ph_soc_leds(9,1)==1);EXPECT(ph_soc_leds(12.6f,1)==31);finish();
    begin("F6 voltage SOC LEDs","S03","Missing invalid and nonfinite battery data never lights bars");
    EXPECT(!ph_soc_leds(12.6f,0));EXPECT(!ph_soc_leds(NAN,1));EXPECT(!ph_soc_leds(INFINITY,1));EXPECT(!ph_soc_leds(-INFINITY,1));finish();
    begin("F6 voltage SOC LEDs","S04","Monotonic voltage sweep with five-bit-only output");
    unsigned previous=0;float last=-1;
    for(unsigned i=0;i<20001;i++){float v=(float)i/1000;unsigned m=ph_soc_leds(v,1);float soc=ph_soc_voltage(v);EXPECT(m>=previous&&m<=31);EXPECT(soc>=last&&soc>=0&&soc<=100);previous=m;last=soc;}finish();
    ph_battery_sampler_t sampler;
    begin("F6 voltage SOC LEDs","S05","ESP32 sampling 25 current then 25 voltage at 10 ms and 50 ms rest");
    ph_battery_init(&sampler,0);
    for(unsigned i=0;i<50;i++){EXPECT(ph_battery_due(&sampler,i*10));EXPECT(ph_battery_push(&sampler,i*10,(uint16_t)(i<25?2000:1200),1)==(i==49));}
    EXPECT(sampler.valid);EXPECT(sampler.current_raw==2000);EXPECT(sampler.voltage_raw==1200);EXPECT(!ph_battery_due(&sampler,549));EXPECT(ph_battery_due(&sampler,550));finish();
    begin("F6 voltage SOC LEDs","S06","Fractional averages and no sample replay after delayed foreground");
    ph_battery_init(&sampler,0);for(unsigned i=0;i<50;i++)ph_battery_push(&sampler,i*10,(uint16_t)(i<24?i:(i==24?25:100)),1);
    EXPECT(NEAR(sampler.current_raw,12.04f,0.00001f));EXPECT(sampler.voltage_raw==100);
    EXPECT(ph_battery_push(&sampler,10000,123,1)==0);EXPECT(!ph_battery_due(&sampler,10000));EXPECT(ph_battery_due(&sampler,10010));finish();
    begin("F6 voltage SOC LEDs","S07","Acquisition failure resets complete batch and invalidates old average");
    EXPECT(ph_battery_push(&sampler,10010,0,0)==-1);EXPECT(!sampler.valid);EXPECT(!sampler.position);EXPECT(!sampler.current_sum);finish();
    begin("F6 voltage SOC LEDs","S08","Sampling deadline crosses unsigned rollover");
    ph_battery_init(&sampler,UINT32_MAX-5);EXPECT(ph_battery_due(&sampler,UINT32_MAX-5));
    ph_battery_push(&sampler,UINT32_MAX-5,2000,1);EXPECT(!ph_battery_due(&sampler,3));EXPECT(ph_battery_due(&sampler,4));finish();
    fprintf(stderr,"Assertions: %u; failures: %u\n",assertions,failures);
    return failures?1:0;
}
