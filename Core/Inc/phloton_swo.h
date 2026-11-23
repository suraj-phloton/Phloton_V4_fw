#ifndef PHLOTON_SWO_H
#define PHLOTON_SWO_H
#include <stdint.h>

/* User confirmed 21-Nov-2026: R113 and C124 removed, PB3 wired to ST-LINK V2
 * SWO. Keep 0 on an unmodified board where PB3 is connected to HSFAN_ISNS. */
#ifndef PH_SWO_PIN_APPROVED
#define PH_SWO_PIN_APPROVED 1
#endif
#if PH_SWO_PIN_APPROVED != 0 && PH_SWO_PIN_APPROVED != 1
#error "PH_SWO_PIN_APPROVED must be 0 or 1"
#endif
#define PH_SWO_HZ 2000000U
#define PH_SWO_QUEUE_BYTES 1024U
#define PH_SWO_SERVICE_BYTES 64U

extern volatile uint32_t ph_swo_initialized, ph_swo_bytes_sent;
extern volatile uint32_t ph_swo_bytes_dropped, ph_swo_writes_dropped;
extern volatile uint32_t ph_swo_queue_high_water;

/* Foreground only: no ISR, fault-handler, or concurrent producers. */
void ph_swo_init(void);
uint8_t ph_swo_write(const char *data, uint32_t length);
void ph_swo_service(void);
/* Retarget the existing newlib _write -> __io_putchar path to ITM port 0. */
int __io_putchar(int character);
#endif
