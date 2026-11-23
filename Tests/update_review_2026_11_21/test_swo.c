#include <stdint.h>
#include <stddef.h>
#include <windows.h>
#undef LPTR
#include <stdio.h>
#include <string.h>
#include "../feature_validation_2026_11_12/test_support.h"
uint32_t test_primask, SystemCoreClock = 80000000U;
static void capture(uint8_t byte);
static int ready = 1, disconnect_at;
#define PH_SWO_READY() ready
#define PH_SWO_BYTE_HOOK capture
#include "../../Core/Src/phloton_swo.c"
static char captured[40000];
static unsigned captured_length;
static void capture(uint8_t byte)
{
    if(captured_length < sizeof captured) captured[captured_length++] = (char)byte;
    if(disconnect_at && captured_length == (unsigned)disconnect_at) CoreDebug->DHCSR=0;
}
static void reset(void)
{
    memset((void*)0xE0000000,0,0x100000);
    memset((void*)0x48000000,0,0x10000);
    memset((void*)0x40000000,0,0x30000);
    queue_head=queue_count=0;
    ph_swo_bytes_dropped=ph_swo_writes_dropped=ph_swo_bytes_sent=ph_swo_queue_high_water=0;
    captured_length=0;ready=1;disconnect_at=0;test_primask=0;SystemCoreClock=80000000U;
    ph_swo_init(); CoreDebug->DHCSR=CoreDebug_DHCSR_C_DEBUGEN_Msk;
}
static void drain(void){for(unsigned i=0;i<32;i++) ph_swo_service();}
int main(void)
{
    if(VirtualAlloc((void*)0x40000000,0x30000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)!=(void*)0x40000000) return 2;
    if(VirtualAlloc((void*)0xE0000000,0x100000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)!=(void*)0xE0000000) return 2;
    if(VirtualAlloc((void*)0x48000000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)!=(void*)0x48000000) return 2;
#if PH_SWO_PIN_APPROVED
    begin("SWO transport","S01","Approved board initializes PB3 AF0, async 2 MHz and ITM port 0 without changing other GPIO bits");
    reset();GPIOB->MODER=0x55555555;GPIOB->AFR[0]=0x76543210;GPIOB->OTYPER=0xffff;
    GPIOB->PUPDR=0x55555555;GPIOB->OSPEEDR=0;DBGMCU->CR=0x67;ph_swo_init();
    EXPECT(ph_swo_initialized);EXPECT(GPIOB->MODER==((0x55555555U&~(3U<<6))|(2U<<6)));
    EXPECT(GPIOB->AFR[0]==(0x76543210U&~(15U<<12)));EXPECT(GPIOB->OTYPER==(0xffffU&~8U));
    EXPECT(GPIOB->PUPDR==(0x55555555U&~(3U<<6)));EXPECT(GPIOB->OSPEEDR==(3U<<6));
    EXPECT(TPI->ACPR==39);EXPECT(TPI->SPPR==2);EXPECT(ITM->TER&1);EXPECT(!test_primask);finish();
    begin("SWO transport","S02","A missing debugger discards logging without waiting or touching UART registers");
    reset();CoreDebug->DHCSR=0;USART2->CR1=0x1234;USART2->BRR=0x5678;USART2->TDR=0x9abc;
    EXPECT(!ph_swo_write("abc",3));ph_swo_service();
    EXPECT(ph_swo_bytes_dropped==3);EXPECT(ph_swo_writes_dropped==1);EXPECT(!captured_length);EXPECT(!queue_count);
    EXPECT(USART2->CR1==0x1234&&USART2->BRR==0x5678&&USART2->TDR==0x9abc);finish();
    begin("SWO transport","S03","Each disabled trace gate independently drops output");
    for(unsigned gate=0;gate<4;gate++){reset();if(gate==0)ph_swo_initialized=0;if(gate==1)CoreDebug->DEMCR=0;if(gate==2)ITM->TCR=0;if(gate==3)ITM->TER=0;
        EXPECT(!ph_swo_write("abc",3));ph_swo_service();EXPECT(!captured_length);EXPECT(!queue_count);}finish();
    begin("SWO transport","S04","Busy ITM leaves queued bytes intact and returns immediately");
    reset();ready=0;EXPECT(ph_swo_write("abc",3));for(unsigned i=0;i<100000;i++)ph_swo_service();
    EXPECT(queue_count==3);EXPECT(!captured_length);EXPECT(!ph_swo_bytes_dropped);ready=1;drain();EXPECT(captured_length==3&&!memcmp(captured,"abc",3));finish();
    begin("SWO transport","S05","Service sends at most 64 bytes per main-loop pass");
    reset();char data[1024];for(unsigned i=0;i<sizeof data;i++)data[i]=(char)(i%251);
    EXPECT(ph_swo_write(data,200));ph_swo_service();EXPECT(captured_length==64);EXPECT(queue_count==136);
    drain();EXPECT(captured_length==200&&!memcmp(captured,data,200));finish();
    begin("SWO transport","S06","Queue accepts exact capacity and drops an overflowing message in full");
    reset();EXPECT(ph_swo_write(data,1024));EXPECT(!ph_swo_write("xyz",3));EXPECT(queue_count==1024);
    EXPECT(ph_swo_bytes_dropped==3);EXPECT(ph_swo_queue_high_water==1024);drain();EXPECT(captured_length==1024&&!memcmp(captured,data,1024));finish();
    begin("SWO transport","S07","Messages wrap without reordering over thousands of queue cycles");
    reset();for(unsigned cycle=0;cycle<4000;cycle++){captured_length=0;EXPECT(ph_swo_write(data,300));drain();EXPECT(captured_length==300&&!memcmp(captured,data,300));}finish();
    begin("SWO transport","S08","Null oversized and zero-length writes cannot overrun queue storage");
    reset();EXPECT(ph_swo_write(NULL,0));EXPECT(!ph_swo_write(NULL,1));EXPECT(!ph_swo_write(data,UINT32_MAX));EXPECT(!queue_count);finish();
    begin("SWO transport","S09","Detaching before service discards queued data and reconnect accepts fresh messages");
    reset();EXPECT(ph_swo_write("old",3));CoreDebug->DHCSR=0;ph_swo_service();EXPECT(!queue_count);EXPECT(ph_swo_bytes_dropped==3);
    CoreDebug->DHCSR=1;EXPECT(ph_swo_write("new",3));drain();EXPECT(captured_length==3&&!memcmp(captured,"new",3));finish();
    begin("SWO transport","S10","Detaching during a service pass stops transmission and discards the remainder");
    reset();EXPECT(ph_swo_write(data,200));disconnect_at=7;ph_swo_service();EXPECT(captured_length==7);EXPECT(!queue_count);EXPECT(ph_swo_bytes_dropped==193);finish();
    begin("SWO transport","S11","Reinitializing discards pending data rather than replaying stale output");
    reset();EXPECT(ph_swo_write("old",3));ph_swo_init();EXPECT(!queue_count);EXPECT(ph_swo_bytes_dropped==3);finish();
    begin("SWO transport","S12","Incorrect core clock refuses initialization");
    reset();SystemCoreClock=4000000;GPIOB->MODER=0x12345678;ph_swo_init();EXPECT(!ph_swo_initialized);EXPECT(GPIOB->MODER==0x12345678);EXPECT(!ph_swo_write("x",1));finish();
    begin("SWO transport","S13","Printf character retarget uses the same queue and reports rejected writes");
    reset();EXPECT(__io_putchar('A')=='A');drain();EXPECT(captured_length==1&&captured[0]=='A');CoreDebug->DHCSR=0;EXPECT(__io_putchar('B')==-1);finish();
    begin("SWO transport","S14","Diagnostic counter overflow cannot corrupt queue or disable forward progress");
    reset();ph_swo_bytes_sent=UINT32_MAX;EXPECT(ph_swo_write("x",1));drain();EXPECT(ph_swo_bytes_sent==0);EXPECT(!queue_count);
    ph_swo_bytes_dropped=ph_swo_writes_dropped=UINT32_MAX;CoreDebug->DHCSR=0;EXPECT(!ph_swo_write("x",1));EXPECT(ph_swo_bytes_dropped==0&&ph_swo_writes_dropped==0);finish();
#else
    begin("SWO transport","S15","Unmodified-board build leaves PB3 and trace configuration untouched");
    GPIOB->MODER=0x12345678;CoreDebug->DEMCR=0;ph_swo_init();
    EXPECT(!ph_swo_initialized);EXPECT(GPIOB->MODER==0x12345678);EXPECT(!CoreDebug->DEMCR);
    EXPECT(!ph_swo_write("x",1));finish();
#endif
    fprintf(stderr,"Assertions: %u; failures: %u\n",assertions,failures);
    return failures?1:0;
}
