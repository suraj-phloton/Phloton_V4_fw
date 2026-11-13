#include "phloton_features.h"
#include <math.h>
#include <string.h>

float ph_ntc_temperature(uint16_t raw, int *valid)
{
    float volts, resistance, t;
    if (!valid) return NAN;
    *valid = 0;
    if (raw < 4U || raw > 4091U) return NAN;
    volts = (float)raw * 3.3f / 4095.0f;
    if (volts <= 0.003f || volts >= 2.997f) return NAN;
    resistance = 10000.0f * volts / (3.0f - volts);
    t = 1.0f / (1.0f / 298.15f + logf(resistance / 10000.0f) / 3977.0f) - 273.15f;
    if (!isfinite(t) || t < -40.0f || t > 125.0f) return NAN;
    *valid = 1;
    return t;
}

void ph_temperatures_decode(ph_temperatures_t *out, const uint16_t raw[PH_TEMP_COUNT],
                            uint32_t read_ok, uint32_t now)
{
    out->valid_mask = 0U;
    for (unsigned i = 0; i < PH_TEMP_COUNT; ++i) {
        int valid = 0;
        out->raw[i] = raw[i];
        out->c[i] = (read_ok & (1U << i)) ? ph_ntc_temperature(raw[i], &valid) : NAN;
        if (valid) out->valid_mask |= 1U << i;
    }
    out->time_ms = now;
    out->sequence++;
}

int ph_temperatures_fresh(const ph_temperatures_t *t, uint32_t now)
{
    return t->valid_mask == PH_TEMP_ALL && (uint32_t)(now - t->time_ms) <= PH_TEMP_STALE_MS;
}

int ph_hot_update(uint8_t *inhibited, float hot_c, int valid)
{
    if (!valid || !isfinite(hot_c) || hot_c < -40.0f || hot_c > 125.0f ||
        hot_c >= PH_HOT_CUTOFF_C) *inhibited = 1U;
    else if (hot_c <= PH_HOT_RECOVER_C) *inhibited = 0U;
    return *inhibited != 0U;
}

void ph_lid_init(ph_lid_t *lid, uint32_t now)
{
    memset(lid, 0, sizeof *lid);
    lid->open = 1U; /* Closed input must qualify before the first enable. */
    lid->opened_at = now;
}

void ph_lid_update(ph_lid_t *lid, int raw_open, uint32_t now)
{
    if (raw_open) {
        if (!lid->open) { lid->opened_at = now; lid->alarm_sent = 0U; }
        lid->open = 1U;
        lid->closing = 0U;
    } else if (lid->open) {
        if (!lid->closing) { lid->closing = 1U; lid->closed_since = now; }
        if ((uint32_t)(now - lid->closed_since) >= PH_LID_CLOSE_MS) {
            lid->open = 0U;
            lid->closing = 0U;
            lid->alarm_sent = 0U;
        }
    }
}

int ph_lid_alarm_due(ph_lid_t *lid, int requested_on, uint32_t now)
{
    if (!lid->open || !requested_on) { lid->alarm_sent = 0U; lid->opened_at = now; return 0; }
    if ((uint32_t)(now - lid->opened_at) < PH_LID_ALARM_MS) return 0;
    if (lid->alarm_sent && (uint32_t)(now - lid->alarm_at) < PH_LID_REPEAT_MS) return 0;
    lid->alarm_sent = 1U;
    lid->alarm_at = now;
    return 1;
}

void ph_buzzer_start(volatile ph_buzzer_t *b, uint8_t kind, uint32_t now)
{
    b->started_ms = now;
    b->kind = kind;
}

int ph_buzzer_level(volatile ph_buzzer_t *b, uint32_t now)
{
    uint32_t elapsed = now - b->started_ms;
    if (b->kind == PH_BEEP_SLEEP) {
        if (elapsed < 500U) return 1;
    } else if (b->kind == PH_BEEP_WAKE || b->kind == PH_BEEP_LID) {
        if (elapsed < 200U || (elapsed >= 300U && elapsed < 700U)) return 1;
        if (elapsed < 700U) return 0;
    }
    b->kind = PH_BEEP_NONE;
    return 0;
}

void ph_calibration_defaults(ph_calibration_t *cal)
{
    cal->voltage_slope = 3.3f * 11.0f / 4095.0f;
    cal->voltage_offset = 0.0f;
    cal->current_slope = 3.3f / (4095.0f * 0.132f);
    cal->current_offset = -1.65f / 0.132f;
}

int ph_calibration_valid(const ph_calibration_t *c)
{
    return c && isfinite(c->voltage_slope) && c->voltage_slope >= 0.00001f &&
           c->voltage_slope <= 0.02f && isfinite(c->voltage_offset) &&
           fabsf(c->voltage_offset) <= 5.0f && isfinite(c->current_slope) &&
           fabsf(c->current_slope) >= 0.00001f && fabsf(c->current_slope) <= 0.05f &&
           isfinite(c->current_offset) && fabsf(c->current_offset) <= 100.0f;
}

float ph_battery_voltage(const ph_calibration_t *c, float raw)
{
    if (!ph_calibration_valid(c) || !isfinite(raw) || raw < 0 || raw > 4095) return NAN;
    return c->voltage_slope * raw + c->voltage_offset;
}

float ph_battery_current(const ph_calibration_t *c, float raw)
{
    if (!ph_calibration_valid(c) || !isfinite(raw) || raw < 0 || raw > 4095) return NAN;
    return c->current_slope * raw + c->current_offset;
}

float ph_soc_voltage(float volts)
{
    float soc;
    if (!isfinite(volts)) return 0.0f;
    if (volts <= 8.6f) return 0.0f;
    if (volts >= 12.6f) return 100.0f;
    soc = 10.0f + (volts - 9.0f) * 25.0f;
    if (soc < 0) return 0;
    if (soc > 100) return 100;
    return soc;
}

uint8_t ph_soc_leds(float volts, int valid)
{
    unsigned count = 0U;
    /* Compare voltage boundaries directly: avoid rounding moving exact SOC edges. */
    if (!valid || !isfinite(volts) || volts <= 8.6f) return 0U;
    count = 1U;
    if (volts > 9.4f) count++;
    if (volts > 10.2f) count++;
    if (volts > 11.0f) count++;
    if (volts > 11.8f) count++;
    return (uint8_t)((1U << count) - 1U);
}

void ph_battery_init(ph_battery_sampler_t *s, uint32_t now)
{
    memset(s, 0, sizeof *s);
    s->next_ms = now;
}

int ph_battery_due(const ph_battery_sampler_t *s, uint32_t now)
{
    return (uint32_t)(now - s->next_ms) < 0x80000000U;
}

int ph_battery_push(ph_battery_sampler_t *s, uint32_t now, uint16_t raw, int ok)
{
    if (!ph_battery_due(s, now)) return 0;
    if (!ok || raw > 4095U) {
        s->position = s->valid = 0U;
        s->current_sum = s->voltage_sum = 0U;
        s->next_ms = now + 50U;
        return -1;
    }
    if (s->position < PH_BATTERY_SAMPLES) s->current_sum += raw;
    else s->voltage_sum += raw;
    s->position++;
    s->next_ms = now + PH_BATTERY_SPACING_MS; /* Rebase; never replay a stale backlog. */
    if (s->position < 2U * PH_BATTERY_SAMPLES) return 0;
    s->current_raw = (float)s->current_sum / (float)PH_BATTERY_SAMPLES;
    s->voltage_raw = (float)s->voltage_sum / (float)PH_BATTERY_SAMPLES;
    s->time_ms = now;
    s->valid = 1U;
    s->position = 0U;
    s->current_sum = s->voltage_sum = 0U;
    s->next_ms += 50U; /* Last 10 ms sample delay + ESP32's 50 ms task delay. */
    return 1;
}
