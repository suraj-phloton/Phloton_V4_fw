# PI overcooling investigation — 14 November 2026

The user reported that an older firmware continued cooling its cold sink through approximately 2 C to -5 C, without the expected current reduction. The user subsequently clarified that the bench supply read **4.2 A at 12.5 V at -5 C**, with both fans ON; fan consumption was reported as **350 mA at 12.5 V**. On 15 November the user confirmed that **-5 C was the STM32-reported temperature**, not an independent thermometer reading, and that **no UART/SD log was saved**. The exact flashed build and recorded controller state have not been supplied. This investigation separates proven controller behavior from the still-unconfirmed cause of that physical run.

**The PI/control behavior was not changed.** On 15 November, after explicit user approval, a logging-only extension was applied to production sources. It adds the controller diagnostics and changes the CSV/file/build identity. Cold-sink feedback, gains, setpoint, current range and converter configuration remain unchanged. The original six-feature test report is preserved; see [APPLIED_TEST_RESULTS.md](APPLIED_TEST_RESULTS.md) for validation of the installed logging change.

## Findings

### 1. PI mathematics and configuration are unchanged

The functions `PI_Controller`, `PI_Reset`, and `CurrentToILIMCode` are text-identical between repository revision `505d97f` and current source at HEAD `37c9970`. The twelve inspected PI/filter/timing/channel definitions also match. The unconfirmed older image on the user's board must not automatically be assumed to be revision `505d97f`.

| Setting | Value |
| --- | --- |
| Feedback | Cold sink, PC1 / ADC1_IN2 |
| Setpoint | 3 C, not 2 C |
| Kp | -0.21 A/C |
| Ki | -0.0003 A/(C s), multiplied by Ts=1 s |
| Temperature EMA | alpha=0.25, updated at the 1 s control step |
| Output bounds | 0.5–7 A, rounded in 0.05 A steps |
| Current-limit register | 0x0A |

The six-feature patch did change acquisition from a control-time ADC read to the cached five-sensor acquisition, while retaining the cold-sink selection and conversion model. That measurement-path change should not be described as completely identical execution, even though the PI functions/constants match.

### 2. There is no normal zero-current cold coast

`PI_Controller` clips every active request to at least 0.5 A. A cold sink at 2 C, 0 C or -5 C does not by itself invoke converter OFF. A 24-hour software replay at constant 2 C retained the 0.5 A request for every tick. This proves continued cooling is commanded; it does not establish the actual thermal equilibrium of a fitted TEC.

The current-to-code function also maps a zero or negative request to 0x0A. Setting only a software minimum to zero would therefore not implement OFF. A reviewed coast action must use the converter/bridge shutdown path and a defined restart policy.

### 3. Accumulated integral can retain several amps below target

The signed proportional term is correct for cooling: with `error = 3 - temperature`, negative Kp requests more current when the cold sink is warmer. The issue is not a newly reversed sign.

The positive integral accumulated while warm remains after crossing 3 C. With the present Ki, at a constant 2 C the integral unwinds by only 0.0003 A/s (0.018 A/min) while not saturated. The anti-windup condition prevents further accumulation in the saturated direction; it does not remove accumulated output at the setpoint or account for an actuator unable to deliver the requested current.

These are exact first-step results from the production function. Each row starts independently with the indicated integral value; the proportional term is then added and the existing clamping/rounding applied:

| Filtered cold sink | Starting integral 0 A | Starting integral 4 A |
| --- | ---: | ---: |
| 3 C | 0.50 A | 4.00 A |
| 2 C | 0.50 A | 3.80 A |
| -5 C | 0.50 A | 2.30 A |

A second replay supplied a temperature trace falling from 25 C to -5 C at 1 C/min, passed through the actual alpha=0.25 filter expression at 1 Hz. It accumulated integral without manually seeding it:

| Elapsed | Supplied raw cold sink | Filtered feedback | Integral | Current request | ILIM code |
| --- | ---: | ---: | ---: | ---: | --- |
| 0 s | 25 C | 25 C | 0.006600 A | 4.65 A | 0x5D |
| 600 s | 15 C | 15.05 C | 3.074055 A | 5.60 A | 0x70 |
| 1200 s | 5 C | 5.05 C | 4.341556 A | 4.75 A | 0x5F |
| 1320 s | 3 C | 3.05 C | 4.379055 A | 4.40 A | 0x58 |
| 1380 s | 2 C | 2.05 C | 4.370806 A | 4.15 A | 0x53 |
| 1800 s | -5 C | -4.95 C | 3.809055 A | 2.15 A | 0x2B |

This is a supplied feedback trace, **not a thermal-plant simulation, a stability proof, or a reproduction of the user's measured run**. It establishes that the unchanged algorithm can command multiamp cooling below target following a plausible temperature history. It also shows that the request does fall over part of that trace; a completely unchanged measured current is a separate observation to investigate.

### 4. Raw displayed temperature differs from control feedback

The CSV reports raw converted cold-sink temperature. The PI consumes `flask_bottom_filt_c`, which despite its legacy name is filtered **cold-sink** temperature. After a supplied step from 25 C to 2 C, the first filtered value is 19.25 C and the current request is 3.40 A. In this particular zero-integral step test, the request reaches its 0.5 A floor at tick 7. Faster sensor response does not remove the software filter delay.

The field `pi_provisional_integral_A` is also not the requested current or necessarily the accepted integral state. Existing UART rows do not expose enough fields to prove the command-to-hardware path in one row.

### 5. Converter current limiting must be checked separately

TI's [LM51772 datasheet, Rev. D](https://www.ti.com/lit/ds/symlink/lm51772.pdf) specifies a 5 mV minimum current threshold; with a 10 mOhm sense resistor this is 0.5 A. Codes below 0x0A are not an OFF command. Current limiting also depends on the limiter enable, reference selection and analog sense/compensation circuit. A register readback alone cannot establish delivered current. See sections 7.3.12 and 8.2, and D0/D9 descriptions.

Current code writes D0=0x30 before enabling bit 0 (nominally 0x31), and D9=0x00. Thus D0 limiter bit 4 is requested ON, and D9 external-ISET selection bit 5 is requested OFF. Those writes appear consistent with DAC limiting; the application does not explicitly validate those mode bits after startup or sample the separate CC-operation status (0x21 bit 6).

The reviewed V4 netlist contains R57=10mR in the ISNS sense path, through R55/R58=10R. It also shows JP2 between ILIMCOMP and GND, and an MCU PB6 connection to that net. Actual fitted values, jumper condition, board revision and analog regulation remain unverified; the schematic is evidence of intended wiring, not of the assembled unit. Do not infer or change a jumper from an outdated schematic note referring to JP1.

## Discriminating the physical failure

### Bench-supply clarification

Assuming 350 mA is the combined fan current:

| Quantity | Calculation | Result |
| --- | --- | ---: |
| Total input power | 12.5 V * 4.2 A | 52.5 W |
| Fan power | 12.5 V * 0.35 A | 4.375 W |
| Remaining converter/circuit input power | 52.5 - 4.375 W | 48.125 W |
| Remaining input current | 4.2 - 0.35 A | 3.85 A |
| Ideal current at a 13 V output, neglecting losses and other loads | 48.125 W / 13 V | 3.70 A |

The remaining 3.85 A is **input-side current**, not measured TEC current. Output voltage, converter loss and other circuits must be accounted for before inferring TEC current. If 350 mA was per fan instead, remaining input power would be 43.75 W; the overall conclusion is still substantial power consumption.

A correctly enforced 0.5 A output limit at no more than the nominal 13 V would supply no more than 6.5 W to that output. The observed approximately 48 W beyond the fans is therefore not explained by that minimum alone without substantial other consumption/loss or a different output operating condition. The leading branches are a continued multiamp PI request, a current limiter that is not enforcing the request, or a mismatch between physical cold-sink temperature and the feedback used by the controller.

For scale, at a filtered -5 C the proportional contribution is -1.68 A. An integral still near 5 A would leave approximately a 3.3 A command after rounding. This is a possible mechanism, not a measurement of the user's integral or converter efficiency.

### Command-to-measurement checks

The STM32-reported temperature narrows the measurement question: in repository baseline 505d97f and current firmware, the CSV cold-temperature field reports converted **raw cold-sink temperature**, whereas PI feedback is the EMA-filtered cold-sink value. A transient raw/filtered difference remains possible; this clarification alone does not establish the filtered value or validate physical sensor accuracy.

Both versions already log useful values in the same first ten positions: column 2 is TEC voltage, column 3 is TEC current, column 8 is the provisional PI integral, column 9 is raw cold-sink temperature and column 10 is cold-temperature validity. Ten to twenty consecutive existing rows could reveal integral buildup, trends and delivered-power inconsistencies without changing firmware. Column 8 must not be mistaken for requested current or the exact accepted integral; filtered feedback and verified ILIM are absent from that CSV. If no old capture exists, a subsequent controlled capture needs those additional states to distinguish the remaining branches.

| Observation in a controlled check | What it indicates / next investigation |
| --- | --- |
| Request remains multiamp as filtered cold sink crosses target; matching ILIM readback | Integral history is keeping the software command high. Review cold coast, integral handling and plant tuning after actuator verification. |
| Request and readback fall to 0.5 A; measured TEC current also falls to about 0.5 A but temperature continues falling | The nonzero cooling floor and thermal dynamics are sufficient candidates. The current controller has no normal cold OFF policy. |
| Request/readback fall but measured TEC current remains high | Investigate current-limit mode, sense path, reference selection, compensation, jumper population and measurement calibration. PI gain changes do not establish current-limit operation. |
| Raw cold reading falls but filtered reading remains warm temporarily | EMA delay explains the temporary discrepancy; confirm decay and source/time consistency. |
| Only battery/bench-supply current appears unchanged | That includes other loads and converter effects. Measure/log TEC current and voltage separately before attributing the observation to a PI command failure. |
| `control_cycles` stops or ILIM verification reports errors | Trace the flashed image, scheduling/startup/fault state, I2C path and existing fault codes. Do not assume a running PI. |

Useful existing debugger variables are `flask_bottom_raw_c`, `flask_bottom_filt_c`, `pi_error`, `pi_integral`, `pi_requested_current_a`, `ilim_code_requested`, `rb_ilim`, `ilim_readback_match`, `control_cycles`, `tec_voltage`, `tec_current`, and `firmware_error`. Use live observations where supported; halting the MCU with the power stage active can itself disrupt supervision. No new UART schema or live hardware commands were introduced here.

## Executed software cases

Final full diagnostic run: [20261114T042508887460Z](results/20261114T042508887460Z/summary.json).

| Case | Characterization | Final result |
| --- | --- | --- |
| PI01 | 3 C setpoint, signed cooling gains and 1 s sample time | PASS, all four profiles |
| PI02 | Fresh-state command falls across a descending filtered-temperature table | PASS, all four profiles |
| PI03 | Constant 2 C for 86,401 calls never requests less than 0.5 A | PASS, limitation reproduced |
| PI04 | Constant -5 C for 3,601 calls retains the nonzero floor | PASS, limitation reproduced |
| PI05 | Positive integral retains 3.8 A at 2 C and 2.3 A at -5 C | PASS, limitation reproduced |
| PI06 | Supplied 25 to -5 C history accumulates integral and retains current below target | PASS, limitation reproduced |
| PI07 | Filtered control feedback lags a supplied raw 25 to 2 C step | PASS, behavior characterized |
| PI08 | Zero request still maps to minimum ILIM; all 131 normal current steps retained | PASS, limitation reproduced |

Four profiles are baseline/current at O0/O2. Actual production C functions were extracted and compiled; the PI equation was not rewritten in Python. All four CSV traces are byte-identical. **32 case executions, 367,852 assertions, zero runtime assertion failures in the final run.** PASS means a characterization assertion passed, not that temperature regulation passed acceptance. UBSan and stack protection were enabled; this does not model the analog regulator or thermal plant.

The first attempt was interrupted after sandbox compiler-worker AccessDenied errors. A second retained run completed three profiles but current O2 failed to start a compiler/linker worker. A repeat completed all four profiles. Earlier raw logs are retained; these launch errors are not PI runtime failures. Original six-feature results were not overwritten.

Run from repository root:

```powershell
& 'C:/Espressif/tools/idf-python/3.11.2/python.exe' Tests/pi_investigation_2026_11_14/investigate.py
```

The runner accepts `--baseline`, `--zig`, and optional `--netlist` paths. Executables/caches are ignored under `.build`; commands, extracted-function/config equality, source fingerprints, software traces and selected netlist evidence are retained under `results/`.

## Changes still requiring a control-policy decision

An appropriate correction must retain cold-sink feedback, define a true OFF/coast action and qualified restart, and review integral behavior with actual delivered current and the measured thermal response. Exact coast thresholds/hysteresis and any gain changes have not been selected or applied. The reported run does not justify blindly changing Ki, reversing signs, or setting ILIM to zero. Physical testing and the user's current-measurement/build details are still needed to identify which mechanisms caused that run.

A separate [logging-only diagnostic change](DIAGNOSTIC_PROPOSAL.md) was prepared, explicitly approved and applied on 15 November because the old capture is unavailable. Matching tests were updated; no board was flashed. The cold coast/integral policy remains a separate, unapplied change.

## Git transport on Windows

The saved patch intentionally contains mixed CRLF/LF context. This folder's `.gitattributes` preserves those bytes and permits the spaces required by diff context. Read-only patch checks ignore context whitespace differences so normal Git checkout line-ending conversion does not invalidate an otherwise matching diagnostic change. Long workspace paths can require Git's `-c core.longpaths=true` option when staging or checking out the retained test logs. No firmware behavior is changed by these portability settings.
