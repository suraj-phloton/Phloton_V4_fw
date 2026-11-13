#ifndef PHLOTON_FEATURES_H
#define PHLOTON_FEATURES_H
#include <stdint.h>

enum { PH_AMBIENT, PH_COLD, PH_HOT, PH_TOP, PH_BOTTOM, PH_TEMP_COUNT };
#define PH_TEMP_ALL ((1U << PH_TEMP_COUNT) - 1U)
#define PH_HOT_CUTOFF_C 60.0f
#define PH_HOT_RECOVER_C 50.0f
#define PH_TEMP_STALE_MS 250U
#define PH_LID_CLOSE_MS 30U
#define PH_LID_ALARM_MS 20000U
#define PH_LID_REPEAT_MS 10000U
#define PH_BATTERY_SAMPLES 25U
#define PH_BATTERY_SPACING_MS 10U

typedef struct {
    float c[PH_TEMP_COUNT];
    uint16_t raw[PH_TEMP_COUNT];
    uint32_t valid_mask, time_ms, sequence;
} ph_temperatures_t;
typedef struct {
    float voltage_slope, voltage_offset, current_slope, current_offset;
} ph_calibration_t;
typedef struct {
    uint32_t current_sum, voltage_sum, next_ms, time_ms;
    uint8_t position, valid;
    float current_raw, voltage_raw;
} ph_battery_sampler_t;
typedef struct {
    uint32_t closed_since, opened_at, alarm_at;
    uint8_t open, closing, alarm_sent;
} ph_lid_t;
enum { PH_BEEP_NONE, PH_BEEP_WAKE, PH_BEEP_SLEEP, PH_BEEP_LID };
typedef struct { uint32_t started_ms; uint8_t kind; } ph_buzzer_t;

float ph_ntc_temperature(uint16_t raw, int *valid);
void ph_temperatures_decode(ph_temperatures_t *out, const uint16_t raw[PH_TEMP_COUNT],
                            uint32_t read_ok, uint32_t now);
int ph_temperatures_fresh(const ph_temperatures_t *t, uint32_t now);
int ph_hot_update(uint8_t *inhibited, float hot_c, int valid);
void ph_lid_init(ph_lid_t *lid, uint32_t now);
void ph_lid_update(ph_lid_t *lid, int raw_open, uint32_t now);
int ph_lid_alarm_due(ph_lid_t *lid, int requested_on, uint32_t now);
void ph_buzzer_start(volatile ph_buzzer_t *b, uint8_t kind, uint32_t now);
int ph_buzzer_level(volatile ph_buzzer_t *b, uint32_t now);
void ph_calibration_defaults(ph_calibration_t *cal);
int ph_calibration_valid(const ph_calibration_t *cal);
float ph_battery_voltage(const ph_calibration_t *cal, float raw);
float ph_battery_current(const ph_calibration_t *cal, float raw);
float ph_soc_voltage(float volts);
uint8_t ph_soc_leds(float volts, int valid);
void ph_battery_init(ph_battery_sampler_t *s, uint32_t now);
int ph_battery_due(const ph_battery_sampler_t *s, uint32_t now);
int ph_battery_push(ph_battery_sampler_t *s, uint32_t now, uint16_t raw, int ok);
#endif
