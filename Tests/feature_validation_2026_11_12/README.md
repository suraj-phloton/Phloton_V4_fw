# Firmware behavior and validation — updated 21 November 2026

Current build identifier: `PHV4-SWO-DIAGNOSTICS-20261121-A`. The six features were implemented on 12 November; diagnostic logging was extended on 15 November, and the explicitly approved SWO migration was applied on 21 November. Cold-sink feedback and the PI settings remain unchanged.

This folder contains executable tests of the actual C sources, retained test-run results, and the physical-board acceptance checklist. See the [current applied SWO results](../update_review_2026_11_21/APPLIED_TEST_RESULTS.md) for the latest per-feature verdict and case-by-case table, and [SWO setup](../update_review_2026_11_21/SWO_SETUP.md) for the debugger configuration. The [15 November report](../pi_investigation_2026_11_14/APPLIED_TEST_RESULTS.md) and [original 12 November report](TEST_RESULTS.md) retain their historical UART validation. Failed runs are retained even after correction. Physical tests are **NOT RUN**, never software passes.

## Implemented behavior

| Feature | Implementation |
| --- | --- |
| Five temperatures | Ambient PC0/ADC1_IN1; cold PC1/IN2; hot PC2/IN3; flask top PC3/IN4; flask bottom PC4/IN13. Each has 16 ADC readings per acquisition and independent validity. Acquisition runs approximately every 100 ms. All five must be valid and no older than 250 ms for cooling. |
| Hot cutoff | TEC converter reset asserted at hot temperature >=60 C. Recovery requires <=50 C, fresh valid sensors, closed lid, retained ON request, and no latched fault. The full converter configuration/startup is rerun. Fans remain ON during a hot pause, including after user OFF, until recovery. |
| Lid interlock | PC7 HIGH is the configured open level (`PH_LID_OPEN_LEVEL`, selectable 0/1). Opening cuts converter reset in the 1 ms interrupt independently of foreground SD/trace work. Closing must qualify for 30 ms. Automatic restart requires a retained ON request and all other permissives. A fault or user OFF cancels automatic restart. |
| Buzzer | PA10, AF1, TIM1_CH3, 5 kHz/50% carrier. Wake: ON 200 ms, OFF 100 ms, ON 400 ms. Sleep: ON 500 ms. Lid warning: wake pattern after 20 seconds continuously open while ON is requested, repeated every 10 seconds. Envelopes are serviced by SysTick without blocking delays. Closing the lid/OFF cancels a lid warning. |
| Calibration persistence | STM32 flash records hold battery voltage slope/offset and current slope/offset, expressed as physical units per raw ADC count and physical offset. Boot loads the latest valid committed record. No valid record means explicit nominal coefficients, not zero readings. No automatic flash writes at boot. |
| Voltage-only SOC | `SOC = clamp(10 + 25*(Vbat-9), 0, 100)`. Thus 9.0 V =10%, 12.6 V =100%, and <=8.6 V =0%. This is a voltage indication, not coulomb counting or a validated battery fuel gauge. |
| Battery sampling | 25 current samples followed by 25 voltage samples, each spaced 10 ms; then the ESP32-equivalent 50 ms task rest. No blocking sample delays. A full normal cycle is approximately 550 ms. Missed sample slots are not replayed. Fractional raw averages are preserved for calibration. |

The cold-sink PI selection, 3 C setpoint, Kp/Ki, EMA coefficient, current limits, converter voltage request, and existing fault restart lockout are retained. The acquisition phase and averaging resolution differ slightly from the former independently sampled logger/control paths; physical regression tests are required.

### SOC LED table

Patterns use the low five bits, matching the previous all-ON `0x1F` convention. LED order/polarity must be checked on the assembly.

| Voltage | Indicated band | Lit LEDs | Mask |
| --- | --- | --- | --- |
| <=8.60 V or invalid/stale sample | 0% / unavailable | 0 | 0x00 |
| >8.60 to 9.40 V | >0–20% | 1 | 0x01 |
| >9.40 to 10.20 V | >20–40% | 2 | 0x03 |
| >10.20 to 11.00 V | >40–60% | 3 | 0x07 |
| >11.00 to 11.80 V | >60–80% | 4 | 0x0F |
| >11.80 V | >80–100% | 5 | 0x1F |

The indicator is dark while the user request is OFF or a fault is latched. Hot/lid pauses retain the requested-ON indication. A battery sample older than 1000 ms clears the bars.

## Hardware evidence and required confirmation

The V4 KiCad schematic and PCB under `Designs/Phloton_CB_V4/Phloton_CB_V4` were inspected; a temporary XML netlist was exported with KiCad 10. U1 pads 8/9/10/11/24 carry the five temperature nets; pad 43 is PA10 `/BUZZER_CTRL`; pad 38 is PC7 `/LID_OPEN_DETECT`. Q1 is the buzzer low-side driver. Q21 is the detector's open-collector NPN and R126 its 3.3 V pull-up.

HIGH=open is the fail-open default: an undriven detector line inhibits cooling. Measure PC7 with the actual lid open/closed before release; the schematic alone does not establish mechanical switch orientation. Both polarity profiles are compiled and exercised by the platform suite. Do not use the old ESP32 pin numbers/polarity as STM32 wiring evidence.

The BOM marks H1 as DNP. Verify that a buzzer is actually populated or externally connected before expecting sound. The tests model GPIO/timer registers; they do not prove gate voltages, buzzer loudness or sensor calibration.

## Calibration NVS and provisioning

The STM32 has its own flash-backed format; ESP32 `Preferences` data are not binary compatible. The transferable values are the four measured coefficients.

| Region | Address | Size |
| --- | --- | --- |
| Application flash | 0x08000000–0x0807EFFF | 508 KiB |
| Calibration copy 0, page 254 | 0x0807F000 | 2 KiB |
| Calibration copy 1, page 255 | 0x0807F800 | 2 KiB |

Both linker scripts reserve the final 4 KiB. Each record has a version, sequence, four coefficients, CRC32 and a last-programmed commit double word. Updates erase/program only the inactive copy, retaining the old valid record. Reads with an uncorrectable ECC event are rejected through a narrowly restricted NMI handler for the reserved region. Physical interrupted-write/ECC/reset behavior still needs board qualification.

To provision measured coefficients through the debugger:

1. Leave the user request OFF and verify converter reset PB13 is LOW. Do not stop the processor with the power stage active.
2. Write `calibration_pending.voltage_slope`, `.voltage_offset`, `.current_slope`, `.current_offset`.
3. Write `calibration_save_request = 0x43414C31` and let the foreground execute.
4. Check `calibration_status` and `calibration_sequence`, then reset and verify loading and physical measurements.

| Status | Meaning |
| --- | --- |
| 0 | Nominal defaults; no persisted valid record |
| 1 | Persisted record loaded at boot |
| 2 | New record saved and read back |
| 3 | Invalid request key or invalid coefficient set rejected |
| 4 | Save rejected because cooling is requested, startup is active, or reset is not asserted |
| 5 | Storage write/readback failed; active coefficients retained |

Nominal defaults are `voltage_slope=3.3*11/4095`, `voltage_offset=0`, `current_slope=3.3/(4095*0.132)`, `current_offset=-1.65/0.132`. They are not measured calibration. Coefficients are range/finite checked; no calibration-writing UI or UART command parser was added. Flash save requests are one-shot, never periodic.

Firmware updates must preserve the reserved pages. A programmer's mass erase still deletes calibration; linker reservation cannot prevent that. Ordinary power loss does not preserve a record until its final commit is valid. Flash wear, brownout and physical power-cut recovery remain acceptance tests.

## Logging compatibility

Console output now uses **SWO at 2 MHz / ITM stimulus port 0**, with an 80 MHz core clock. USART2/PA2 logging to the ESP32 has been removed. The 12 November version had 22 fields; the approved 15 November diagnostic version added **32 columns**, preserved by this SWO migration. The appended values are filtered cold-sink feedback, accepted PI integral, requested current, requested/readback ILIM codes, verification flag, control-cycle count, startup state, and low/high saturation flags. See the [diagnostic field table](../pi_investigation_2026_11_14/DIAGNOSTIC_PROPOSAL.md) for exact order and interpretation.

SD output continues using `TEC_PI.CSV` to avoid mixing diagnostic records with `TEC_V2.CSV` or `TEC_LOG.CSV`. Existing files are retained. ITM starts with the banner/build ID followed by rows; the matching CSV header is written to SD. The former ESP32 serial bridge receives no firmware log output. Fixed-column consumers must retain the 32-column schema. A foreground queue holds 1,024 bytes, dropping complete new messages on overflow, and services at most 64 bytes per loop pass without waiting for ITM readiness. Missing debugger/trace gates cause output to be dropped without blocking cooling or SD logging. This migration adds no cold-temperature shutdown or PI tuning.

The user confirmed R113 and C124 removed and PB3 connected to the SWO input of an ST-LINK V2. `PH_SWO_PIN_APPROVED=1` defaults to that modified unit. Set the build flag to `0` for an unmodified board where PB3 still connects to HSFAN_ISNS; no trace pin is configured in that profile. The existing fan current sensing remains disabled. The SWO writer and `printf` retarget are foreground-only and must not be called from interrupts or fault handlers.

New latched diagnostic codes: 110 = required temperature invalid; 112 = stale temperature/supervision gap; 113 = ADC peripheral failure. Lid/hot pauses are recoverable interlocks and are reported separately; they do not clear or replace an existing latched fault.

## Reproduce software validation

Run from the repository root with Python 3:

```powershell
& 'C:/Espressif/tools/idf-python/3.11.2/python.exe' Tests/feature_validation_2026_11_12/bootstrap_compiler.py
& 'C:/Espressif/tools/idf-python/3.11.2/python.exe' Tests/feature_validation_2026_11_12/run_tests.py --client-date 2026-11-21
```

`bootstrap_compiler.py` uses a temporary SHA-256-verified Zig 0.13.0 download, not a global install. `run_tests.py --zig PATH --arm-gcc PATH` supports alternate tool locations. Compiler caches and ELF images live in this folder's ignored `.build/`. The command above defaults to `../update_review_2026_11_21/applied_results/` and its `APPLIED_TEST_RESULTS.md`, preserving earlier reports. Each run records commands, stdout, stderr, cases, build identity and source fingerprints. The older `pi_investigation_2026_11_14/validate_contract.py` checks its historical UART-era patch; the maintained runner uses `update_review_2026_11_21/validate_installed_swo.py` for current firmware.

Host suites use the real feature/NVS C sources and include the actual application integration and SWO transport code with real STM32 peripheral definitions. Windows `VirtualAlloc` supplies emulated MMIO regions; a minimal Cortex intrinsic shim replaces ARM-only instructions. An ADC polling hook models completion/timeout/overrun and an SWO-byte hook captures exact formatter output after queue service. These hooks expand to no-ops in production. Separate transport cases exercise backpressure, disabled trace, overflow, wraparound, disconnect/reconnect and printf routing. NVS power-cut tests use an explicit flash model with 1-to-0 programming and injected interrupted double-word writes.

Tests run at O0/O2 with undefined-behavior sanitizer, stack protection and warnings-as-errors. The full target is checked with GNU Arm at O0/Os. The strict changed-source check suppresses only unused-function warnings from retained legacy helpers. A separate build must reject an invalid lid-polarity profile.

The old tracked `Debug` ELF/list/map are not refreshed by this runner. Use the newly built `.build/phloton_O0.elf` or a clean CubeIDE build after refreshing all added Core/Src files. Do not mistake an older Debug artifact for this source revision. No board was flashed in this session.

Primary flash references: [ST RM0394](https://www.st.com/resource/en/reference_manual/dm00151940.pdf), [ST AN4894](https://www.st.com/resource/en/application_note/an4894-how-to-use-eeprom-emulation-on-stm32-mcus-stmicroelectronics.pdf). This small two-copy calibration store is application code, not a claim to implement the full X-CUBE-EEPROM library.
