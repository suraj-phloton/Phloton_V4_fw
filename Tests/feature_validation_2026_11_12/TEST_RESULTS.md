# Six-feature software test results — 12 November 2026

Final run: [20261112T151513047912Z](results/20261112T151513047912Z/summary.json). Completed UTC: `2026-11-12T15:16:08.588268+00:00`.

**16 driver checks PASS; 0 FAIL.** 61 unique executable cases, 174 case executions and 227,170 assertions.

**Physical board tests: NOT RUN.** No firmware was flashed and no STM32/ESP32 hardware loop was exercised. The 21 pending physical cases are itemized in [BOARD_ACCEPTANCE.md](BOARD_ACCEPTANCE.md).

## Per-feature verdict

| Feature | Unique cases | Executions | Assertions | PASS executions | FAIL executions |
| --- | ---: | ---: | ---: | ---: | ---: |
| F1 temperatures | 11 | 34 | 82,180 | 34 | 0 |
| F2 hot protection | 6 | 18 | 60,078 | 18 | 0 |
| F3 lid interlock | 9 | 30 | 176 | 30 | 0 |
| F4 buzzer | 9 | 26 | 114 | 26 | 0 |
| F5 calibration NVS | 14 | 34 | 3,760 | 34 | 0 |
| F6 voltage SOC LEDs | 10 | 24 | 80,310 | 24 | 0 |
| Integration telemetry | 1 | 4 | 16 | 4 | 0 |
| Regression | 1 | 4 | 536 | 4 | 0 |

## Execution profiles and scope

- Logic cases (T/H/L/B/N/S IDs): actual production feature and NVS modules at O0 and O2.
- Platform cases (P IDs): actual main/application integration and STM32 storage adapter at O0/O2, each with lid HIGH=open and LOW=open. MMIO, ADC completion and UART output are modeled on Windows.
- Both host suites enable undefined-behavior sanitizer, stack protection and warnings-as-errors. They do not simulate electrical characteristics, real flash ECC, thermal dynamics or instruction timing.
- Target checks compile the changed sources strictly and link the full GNU Arm image at O0/Os. Rejecting lid profile 2 is an expected negative compile check.
- Exhaustive sweeps cover 4,096 ADC codes for each temperature, every single-bit corruption in a 64-byte calibration record, all eight write positions with zero through seven interrupted bytes, and 20,001 battery voltages. Rollover cases cover unsigned millisecond and NVS sequence counters.

## Every executed case

PASS/FAIL below covers all listed profiles for that case. Physical validation remains separate.

### F1 temperatures

| ID | Test | Profiles | Result |
| --- | --- | ---: | --- |
| T01 | All five independent channels and nominal conversion | 2 | PASS |
| T02 | Each individual channel invalidates independently | 2 | PASS |
| T03 | Exhaustive 4096-code sweep for every sensor | 2 | PASS |
| T04 | Open short near rails and invalid output pointer | 2 | PASS |
| T05 | Freshness boundary and unsigned wraparound | 2 | PASS |
| P01 | Real acquisition uses PC0-PC4 channel mapping and 16 samples each | 4 | PASS |
| P02 | Real GPIO and sampling-time configuration covers every sensor | 4 | PASS |
| P03 | ADC stuck conversion returns and requests direct shutdown | 4 | PASS |
| P04 | ADC timeout also terminates with a frozen DWT clock | 4 | PASS |
| P05 | ADC overrun and invalid channel rejected | 4 | PASS |
| P06 | Any individual sensor failure prevents cooling and latches fault | 4 | PASS |

### F2 hot protection

| ID | Test | Profiles | Result |
| --- | --- | ---: | --- |
| H01 | Exact trip and recovery with retained hysteresis | 2 | PASS |
| H02 | NaN infinity invalid flag and recovery | 2 | PASS |
| H03 | Repeated noisy temperature sweep stays inhibited until recovery | 2 | PASS |
| P07 | Hot cutoff pauses cooling keeps fans and retains user request | 4 | PASS |
| P08 | Cooling restarts only after recovery with fresh qualification | 4 | PASS |
| P13 | IRQ stale-sample cutoff works during blocked foreground and rollover | 4 | PASS |

### F3 lid interlock

| ID | Test | Profiles | Result |
| --- | --- | ---: | --- |
| L01 | Boot closed requires 30 ms qualification | 2 | PASS |
| L02 | Open immediate closure bounce resets timer | 2 | PASS |
| L03 | Closure qualification crosses millisecond rollover | 2 | PASS |
| P09 | IRQ cuts power before foreground can process lid | 4 | PASS |
| P10 | Close after IRQ still waits 30 ms then restarts configuration | 4 | PASS |
| P11 | Every startup phase can be cancelled by lid without latching a fault | 4 | PASS |
| P12 | Fault or user OFF prevents automatic resume | 4 | PASS |
| P24 | Simultaneous hot and lid inhibit needs both to recover | 4 | PASS |
| P25 | Lid pulse during ADC publication cannot be lost by foreground flag clearing | 4 | PASS |

### F4 buzzer

| ID | Test | Profiles | Result |
| --- | --- | ---: | --- |
| B01 | Wake beep on-off-on boundaries | 2 | PASS |
| B02 | Sleep tone and preemption | 2 | PASS |
| B03 | Twenty second lid alarm and ten second repeat | 2 | PASS |
| B04 | Alarm cancellation on close and OFF resets delay | 2 | PASS |
| B05 | Buzzer and alarm wraparound | 2 | PASS |
| P14 | TIM1 CH3 PA10 5 kHz carrier leaves PA8 configuration alone | 4 | PASS |
| P15 | Real interrupt services wake and sleep tone without advancing foreground | 4 | PASS |
| P16 | IRQ raises lid alarm at twenty seconds without any foreground service | 4 | PASS |
| P17 | Buzzer request preserves a preexisting interrupt mask | 4 | PASS |

### F5 calibration NVS

| ID | Test | Profiles | Result |
| --- | --- | ---: | --- |
| N01 | Blank storage nominal defaults without flash writes | 2 | PASS |
| N02 | Save reboot load newer record and previous copy retained | 2 | PASS |
| N03 | Every single-bit record corruption falls back to previous copy | 2 | PASS |
| N04 | Power interruption at every write and partial double-word byte | 2 | PASS |
| N05 | Erase and read failure preserve usable calibration | 2 | PASS |
| N06 | Both corrupted records use defaults without writes | 2 | PASS |
| N07 | Invalid calibration cannot erase or program flash | 2 | PASS |
| N08 | Calibrated conversion fractional counts signed current and invalid raw | 2 | PASS |
| N09 | Persisted generation counter wraps from maximum to zero | 2 | PASS |
| N10 | Incomplete storage interface never erases calibration | 2 | PASS |
| N11 | Lost readback acknowledgment accepts only a complete committed copy | 2 | PASS |
| P18 | Save request rejects active cooling invalid key and invalid coefficients | 4 | PASS |
| P19 | Calibration region read checks and ECC recovery are restricted | 4 | PASS |
| P26 | Failed flash save retains active coefficients and reports failure | 4 | PASS |

### F6 voltage SOC LEDs

| ID | Test | Profiles | Result |
| --- | --- | ---: | --- |
| S01 | User mapping endpoints clamp and intermediate levels | 2 | PASS |
| S02 | Every LED threshold below exact and above | 2 | PASS |
| S03 | Missing invalid and nonfinite battery data never lights bars | 2 | PASS |
| S04 | Monotonic voltage sweep with five-bit-only output | 2 | PASS |
| S05 | ESP32 sampling 25 current then 25 voltage at 10 ms and 50 ms rest | 2 | PASS |
| S06 | Fractional averages and no sample replay after delayed foreground | 2 | PASS |
| S07 | Acquisition failure resets complete batch and invalidates old average | 2 | PASS |
| S08 | Sampling deadline crosses unsigned rollover | 2 | PASS |
| P20 | Real indicator uses calibrated voltage masks and clears on OFF | 4 | PASS |
| P21 | Stale or missing battery sample clears indicator | 4 | PASS |

### Integration telemetry

| ID | Test | Profiles | Result |
| --- | --- | ---: | --- |
| P22 | UART row contains all 22 fields with five sensor values | 4 | PASS |

### Regression

| ID | Test | Profiles | Result |
| --- | --- | ---: | --- |
| P23 | Original current-code mapping PI limits and reset remain intact | 4 | PASS |

## Build and runner checks

| Check | Result |
| --- | --- |
| compile_features_O0 | PASS |
| features_O0 | PASS |
| compile_platform_O0_lid0 | PASS |
| platform_O0_lid0 | PASS |
| compile_platform_O0_lid1 | PASS |
| platform_O0_lid1 | PASS |
| compile_features_O2 | PASS |
| features_O2 | PASS |
| compile_platform_O2_lid0 | PASS |
| platform_O2_lid0 | PASS |
| compile_platform_O2_lid1 | PASS |
| platform_O2_lid1 | PASS |
| arm_strict_syntax | PASS |
| invalid_lid_profile_rejected | PASS |
| arm_full_link_O0 | PASS |
| arm_full_link_Os | PASS |

Target linker memory reports (reserved calibration flash remains unused by the image):

GNU Arm O0:

```text
Memory region         Used Size  Region Size  %age Used
             RAM:        3680 B       128 KB      2.81%
            RAM2:           0 B        32 KB      0.00%
           FLASH:       27256 B       508 KB      5.24%
         CAL_NVS:           0 B         4 KB      0.00%
```

GNU Arm Os:

```text
Memory region         Used Size  Region Size  %age Used
             RAM:        3624 B       128 KB      2.76%
            RAM2:           0 B        32 KB      0.00%
           FLASH:       18248 B       508 KB      3.51%
         CAL_NVS:           0 B         4 KB      0.00%
```

The full target links retain an unused legacy `sd_verify` buffer warning and a linker RWX LOAD-segment warning with the STM32 RAM-function layout. Strict changed-source checks have no diagnostics. See the raw stderr files for the exact messages; successful linking does not qualify the physical image.

## Earlier failed runs and corrections

Earlier runs are retained so failures are not erased by a successful rerun. The recorded failures were tool access or harness compilation failures; no runtime feature case failed in these retained runs.

| Run | PASS checks | FAIL checks | Cause and correction |
| --- | ---: | ---: | --- |
| [20261112T144804942745Z](results/20261112T144804942745Z/summary.json) | 0 | 2 | Zig cache access denied; moved caches into the workspace and completed compiler bootstrap. |
| [20261112T145453169032Z](results/20261112T145453169032Z/summary.json) | 0 | 2 | Warnings-as-errors rejected misleading indentation in the test harness; added explicit loop braces. |
| [20261112T145856461042Z](results/20261112T145856461042Z/summary.json) | 8 | 4 | Windows NEAR/LPTR macros and host pointer width conflicted with the firmware harness; isolated macros and used uintptr_t for MMIO pointer casts. |
| [20261112T150829635370Z](results/20261112T150829635370Z/summary.json) | 12 | 0 | All host checks passed; additional edge and regression cases were then added before the final full run. |
| [20261112T151513047912Z](results/20261112T151513047912Z/summary.json) | 16 | 0 | Final full run. |

Review also caught two restart races before final validation: a paused converter needed a full configuration restart, and a lid pulse during ADC publication had to survive foreground flag clearing. Integration cases P08/P10/P25 exercise the corrected behavior.

## Remaining physical checks and limits

- Confirm the fitted sensor models, VDDA/excitation, independent channel wiring and calibrated temperatures.
- Measure lid polarity and real converter cutoff/current decay during startup, RUN and slow SD operations.
- Verify H1 or an external buzzer is populated (the reviewed BOM marks H1 DNP); measure the carrier and sound envelopes.
- Provision measured calibration coefficients; defaults are nominal. Test real interrupted flash erase/program, ECC/NMI recovery and the programmer update workflow. Mass erase deletes calibration.
- Check LED order/polarity and the voltage thresholds on a current-limited battery substitute.
- Validate the new TEC_V2.CSV 22-column schema with the physical ESP32 bridge and downstream tools. Old tracked Debug images are stale; use the current test ELF or a clean CubeIDE build.
- Existing FAT32, watchdog, general CPU-fault and RTOS work from the earlier review remains outside these six changes.

See [README.md](README.md) for exact behavior, pin assignments, calibration provisioning and reproduction commands. The retained run summary includes production-source SHA-256 fingerprints.
