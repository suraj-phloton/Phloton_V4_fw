#include "phloton_nvs.h"
#include <string.h>

static uint32_t crc32(const uint8_t *p, unsigned n)
{
    uint32_t crc = UINT32_MAX;
    while (n--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static int record_read(const ph_nvs_io_t *io, unsigned slot, uint32_t w[16])
{
    ph_calibration_t cal;
    if (!io || !io->read || !io->read(io->context, slot, 0U, w, PH_NVS_RECORD_BYTES)) return 0;
    if (w[0] != 0x50484341U || w[1] != 1U || w[3] != sizeof cal ||
        w[14] != 0x434F4D54U || w[15] != ~0x434F4D54U ||
        w[13] != crc32((const uint8_t *)w, 52U)) return 0;
    memcpy(&cal, &w[4], sizeof cal);
    return ph_calibration_valid(&cal);
}

static int newest(const uint32_t a[16], int va, const uint32_t b[16], int vb)
{
    uint32_t delta = b[2] - a[2];
    if (!va) return vb ? 1 : -1;
    return vb && delta != 0U && delta < 0x80000000U ? 1 : 0;
}

int ph_nvs_load(const ph_nvs_io_t *io, ph_calibration_t *cal, uint32_t *sequence)
{
    uint32_t a[16] = {0}, b[16] = {0};
    int va, vb, slot;
    if (!cal) return 0;
    ph_calibration_defaults(cal);
    if (sequence) *sequence = 0U;
    va = record_read(io, 0U, a); vb = record_read(io, 1U, b);
    slot = newest(a, va, b, vb);
    if (slot < 0) return 0;
    memcpy(cal, &(slot ? b : a)[4], sizeof *cal);
    if (sequence) *sequence = (slot ? b : a)[2];
    return 1;
}

int ph_nvs_save(const ph_nvs_io_t *io, const ph_calibration_t *cal)
{
    uint32_t a[16] = {0}, b[16] = {0}, w[16] = {0}, verify[16];
    int va, vb, active;
    unsigned target;
    if (!io || !io->read || !io->erase || !io->program8 || !ph_calibration_valid(cal)) return 0;
    va = record_read(io, 0U, a); vb = record_read(io, 1U, b);
    active = newest(a, va, b, vb);
    target = active == 0 ? 1U : 0U;
    w[0] = 0x50484341U; w[1] = 1U; w[2] = active < 0 ? 1U : (active ? b : a)[2] + 1U;
    w[3] = sizeof *cal;
    memcpy(&w[4], cal, sizeof *cal);
    w[13] = crc32((const uint8_t *)w, 52U);
    w[14] = 0x434F4D54U; w[15] = ~0x434F4D54U;
    if (!io->erase(io->context, target)) return 0;
    /* Commit double word is programmed last. The previous valid page is never erased. */
    for (unsigned offset = 0U; offset < PH_NVS_RECORD_BYTES; offset += 8U)
        if (!io->program8(io->context, target, offset, (const uint8_t *)w + offset)) return 0;
    return record_read(io, target, verify) && memcmp(w, verify, sizeof w) == 0;
}
