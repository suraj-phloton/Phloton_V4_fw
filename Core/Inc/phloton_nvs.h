#ifndef PHLOTON_NVS_H
#define PHLOTON_NVS_H
#include "phloton_features.h"
#include <stddef.h>
#define PH_NVS_PAGE_BYTES 2048U
#define PH_NVS_RECORD_BYTES 64U
#define PH_NVS_BASE 0x0807F000UL
typedef struct {
    void *context;
    int (*read)(void *, unsigned, unsigned, void *, size_t);
    int (*erase)(void *, unsigned);
    int (*program8)(void *, unsigned, unsigned, const void *);
} ph_nvs_io_t;
/* load: 1 = persisted calibration, 0 = nominal fallback. No boot-time flash writes. */
int ph_nvs_load(const ph_nvs_io_t *io, ph_calibration_t *cal, uint32_t *sequence);
int ph_nvs_save(const ph_nvs_io_t *io, const ph_calibration_t *cal);
extern const ph_nvs_io_t ph_nvs_stm32;
#endif
