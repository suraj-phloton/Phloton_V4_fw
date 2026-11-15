/* Characterize the unchanged production PI using supplied temperature traces.
 * This is not a model of the thermal plant or the physical current regulator. */
#include <stdint.h>
#include <stdio.h>
#include <math.h>

volatile float pi_error, pi_integral, pi_provisional_integral, pi_provisional_current;
volatile uint8_t pi_sat_high, pi_sat_low;
#include "pi_from_firmware.h"

static unsigned checks, failures, case_checks, case_failures;
static void begin(void) { case_checks = checks; case_failures = failures; }
#define CHECK(expr) do { checks++; if (!(expr)) { failures++; \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expr); } } while (0)
static void end(const char *id, const char *description)
{
    printf("{\"id\":\"%s\",\"description\":\"%s\",\"status\":\"%s\",\"assertions\":%u}\n",
           id, description, failures == case_failures ? "PASS" : "FAIL", checks - case_checks);
}
static void trace(FILE *out, const char *scenario, unsigned second, float raw, float filtered, float request)
{
    fprintf(out, "%s,%u,%.6f,%.6f,%.6f,%.6f,0x%02X\n", scenario, second,
            raw, filtered, (float)pi_integral, request, CurrentToILIMCode(request));
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *out = fopen(argv[1], "w");
    if (!out) return 2;
    fputs("scenario,t_s,raw_c,filtered_c,integral_a,request_a,ilim_code\n", out);

    begin();
    CHECK(TEC_TEMP_SETPOINT == 3.0f);
    CHECK(PI_KP < 0.0f && PI_KI < 0.0f && PI_TS == 1.0f);
    end("PI01", "Actual configuration is 3 C setpoint with cooling controller sign");

    begin();
    const float points[] = {25, 15, 10, 8, 5, 3, 2, 0, -5};
    const float expected[] = {4.65f, 2.5f, 1.45f, 1.05f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float previous = MAX_CURRENT;
    for (unsigned i = 0; i < sizeof points / sizeof points[0]; ++i) {
        PI_Reset();
        float request = PI_Controller(TEC_TEMP_SETPOINT, points[i]);
        CHECK(fabsf(request - expected[i]) < 0.0001f);
        CHECK(request <= previous);
        trace(out, "isolated_zero_integral", 0, points[i], points[i], request);
        previous = request;
    }
    end("PI02", "Fresh-state current requests reduce as filtered temperature falls");

    begin();
    PI_Reset();
    for (unsigned second = 0; second <= 86400; ++second) {
        float request = PI_Controller(TEC_TEMP_SETPOINT, 2.0f);
        CHECK(request == MIN_CURRENT);
        if (second == 0 || second == 60 || second == 3600 || second == 86400)
            trace(out, "constant_2C_24_hours", second, 2, 2, request);
    }
    CHECK(pi_integral == 0.0f);
    end("PI03", "Persistent 2 C feedback still requests 0.5 A and never OFF");

    begin();
    PI_Reset();
    for (unsigned second = 0; second <= 3600; ++second) {
        float request = PI_Controller(TEC_TEMP_SETPOINT, -5.0f);
        CHECK(request == MIN_CURRENT);
    }
    end("PI04", "Persistent minus 5 C feedback still requests the nonzero floor");

    begin();
    PI_Reset();
    pi_integral = 4.0f;
    float request = PI_Controller(TEC_TEMP_SETPOINT, 2.0f);
    CHECK(fabsf(request - 3.8f) < 0.0001f);
    trace(out, "seeded_integral_4A", 0, 2, 2, request);
    pi_integral = 4.0f;
    request = PI_Controller(TEC_TEMP_SETPOINT, -5.0f);
    CHECK(fabsf(request - 2.3f) < 0.0001f);
    trace(out, "seeded_integral_4A", 0, -5, -5, request);
    end("PI05", "Accumulated integral can retain multiamp commands below the setpoint");

    begin();
    PI_Reset();
    float filtered = 25.0f, at_two = 0.0f, at_minus_five = 0.0f;
    for (unsigned second = 0; second <= 1800; ++second) {
        float raw = 25.0f - (float)second / 60.0f;
        filtered = TEMP_FILTER_ALPHA * raw + (1.0f - TEMP_FILTER_ALPHA) * filtered;
        request = PI_Controller(TEC_TEMP_SETPOINT, filtered);
        CHECK(isfinite(request) && request >= MIN_CURRENT && request <= MAX_CURRENT);
        if (second == 1380) at_two = request;
        if (second == 1800) at_minus_five = request;
        if (second % 300 == 0 || second == 1200 || second == 1320 || second == 1380)
            trace(out, "synthetic_1C_per_minute_ramp", second, raw, filtered, request);
    }
    CHECK(at_two > 3.0f);
    CHECK(at_minus_five > 1.0f);
    end("PI06", "Synthetic cooling history reproduces continued current at 2 C and minus 5 C");

    begin();
    PI_Reset();
    filtered = 25.0f;
    for (unsigned second = 1; second <= 20; ++second) {
        filtered = TEMP_FILTER_ALPHA * 2.0f + (1.0f - TEMP_FILTER_ALPHA) * filtered;
        request = PI_Controller(TEC_TEMP_SETPOINT, filtered);
        if (second == 1) CHECK(request > 3.0f);
        if (second == 20) CHECK(request == MIN_CURRENT);
        trace(out, "raw_step_25C_to_2C", second, 2, filtered, request);
    }
    end("PI07", "EMA lag separates raw logged temperature from PI feedback after a fast step");

    begin();
    CHECK(CurrentToILIMCode(0.0f) == ILIM_CODE_MIN);
    CHECK(CurrentToILIMCode(-1.0f) == ILIM_CODE_MIN);
    for (unsigned step = 0; step <= 130; ++step)
        CHECK(CurrentToILIMCode(0.5f + 0.05f * (float)step) == 10U + step);
    end("PI08", "Current mapping has no zero-current OFF code and retains all 131 limit steps");

    fclose(out);
    fprintf(stderr, "Assertions: %u; failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
