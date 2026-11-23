# Update review and SWO candidate validation — 21 November 2026

Historical candidate validation: the user subsequently approved the migration and it was applied on 21 November. See [installed application validation](APPLIED_TEST_RESULTS.md) for current results; the records below preserve the original pre-application review.

Checks: **40 PASS, 0 FAIL**. Physical board tests: **NOT RUN**.

The candidate is staged separately and has not been applied to production sources.
All firmware sources match the complete validation run. Only the corrected SWO host-test harness was rebuilt and rerun at O0/O2; earlier passing checks were retained with their original evidence links.

[Combined results](results/20261121T133840762620Z/summary.json)
[Complete prior run](results/20261121T133457365559Z/summary.json)

## Retained failed attempts

- `20261121T133056903291Z`: first host compilation failed because Zig could not spawn worker processes (`AccessDenied`); its raw compiler log is retained. The run was interrupted before a second compilation completed.
- `20261121T133457365559Z`: 38 checks passed; two direct SWO host executions crashed because the new harness had not mapped the fake RCC register memory. This was a test setup error. Adding the missing fake MMIO allocation corrected the harness; production/candidate firmware bytes did not change.

The complete supplied project and SWO candidate both link from source at ARM O0/Os. Strict changed-source syntax checks pass with warnings treated as errors. Full links retain existing unused `sd_verify` / RWX LOAD-segment warnings.
PI traces use the actual extracted functions with supplied temperatures, not a thermal plant model. No result verifies physical battery current or thermal stability.

| Check / profile | Result | Case executions | Assertions | Evidence |
| --- | --- | ---: | ---: | --- |
| C01 | PASS | 1 | 5 | [raw](results/20261121T133457365559Z/summary.json) |
| C02 | PASS | 1 | 4 | [raw](results/20261121T133457365559Z/summary.json) |
| C03 | PASS | 1 | 5 | [raw](results/20261121T133457365559Z/summary.json) |
| migration_patch_applies_read_only | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/migration_patch_applies_read_only.stdout.txt) |
| compile_repository_PI_O0 | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/compile_repository_PI_O0.stdout.txt) |
| repository_PI_O0 | PASS | 9 | 91965 | [stdout](results/20261121T133457365559Z/repository_PI_O0.stdout.txt) |
| compile_repository_PI_O2 | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/compile_repository_PI_O2.stdout.txt) |
| repository_PI_O2 | PASS | 9 | 91965 | [stdout](results/20261121T133457365559Z/repository_PI_O2.stdout.txt) |
| compile_supplied_PI_O0 | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/compile_supplied_PI_O0.stdout.txt) |
| supplied_PI_O0 | PASS | 9 | 91965 | [stdout](results/20261121T133457365559Z/supplied_PI_O0.stdout.txt) |
| compile_supplied_PI_O2 | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/compile_supplied_PI_O2.stdout.txt) |
| supplied_PI_O2 | PASS | 9 | 91965 | [stdout](results/20261121T133457365559Z/supplied_PI_O2.stdout.txt) |
| C04 | PASS | 1 | 2 | [raw](results/20261121T133457365559Z/summary.json) |
| compile_features_O0 | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/compile_features_O0.stdout.txt) |
| features_O0 | PASS | 35 | 113043 | [stdout](results/20261121T133457365559Z/features_O0.stdout.txt) |
| compile_swo_O0 | PASS | 0 | 0 | [stdout](results/20261121T133840762620Z/compile_swo_O0.stdout.txt) |
| swo_O0 | PASS | 14 | 8070 | [stdout](results/20261121T133840762620Z/swo_O0.stdout.txt) |
| compile_platform_O0_lid0 | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/compile_platform_O0_lid0.stdout.txt) |
| platform_O0_lid0 | PASS | 28 | 285 | [stdout](results/20261121T133457365559Z/platform_O0_lid0.stdout.txt) |
| compile_platform_O0_lid1 | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/compile_platform_O0_lid1.stdout.txt) |
| platform_O0_lid1 | PASS | 28 | 285 | [stdout](results/20261121T133457365559Z/platform_O0_lid1.stdout.txt) |
| compile_features_O2 | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/compile_features_O2.stdout.txt) |
| features_O2 | PASS | 35 | 113043 | [stdout](results/20261121T133457365559Z/features_O2.stdout.txt) |
| compile_swo_O2 | PASS | 0 | 0 | [stdout](results/20261121T133840762620Z/compile_swo_O2.stdout.txt) |
| swo_O2 | PASS | 14 | 8070 | [stdout](results/20261121T133840762620Z/swo_O2.stdout.txt) |
| compile_platform_O2_lid0 | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/compile_platform_O2_lid0.stdout.txt) |
| platform_O2_lid0 | PASS | 28 | 285 | [stdout](results/20261121T133457365559Z/platform_O2_lid0.stdout.txt) |
| compile_platform_O2_lid1 | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/compile_platform_O2_lid1.stdout.txt) |
| platform_O2_lid1 | PASS | 28 | 285 | [stdout](results/20261121T133457365559Z/platform_O2_lid1.stdout.txt) |
| compile_swo_disabled | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/compile_swo_disabled.stdout.txt) |
| swo_disabled | PASS | 1 | 4 | [stdout](results/20261121T133457365559Z/swo_disabled.stdout.txt) |
| supplied_strict_syntax | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/supplied_strict_syntax.stdout.txt) |
| supplied_full_link_O0 | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/supplied_full_link_O0.stdout.txt) |
| supplied_full_link_Os | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/supplied_full_link_Os.stdout.txt) |
| candidate_strict_syntax | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/candidate_strict_syntax.stdout.txt) |
| candidate_full_link_O0 | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/candidate_full_link_O0.stdout.txt) |
| candidate_full_link_Os | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/candidate_full_link_Os.stdout.txt) |
| invalid_swo_profile_rejected | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/invalid_swo_profile_rejected.stdout.txt) |
| unmodified_board_strict_syntax | PASS | 0 | 0 | [stdout](results/20261121T133457365559Z/unmodified_board_strict_syntax.stdout.txt) |
| C05 | PASS | 1 | 2 | [raw](results/20261121T133457365559Z/summary.json) |

## Individual cases

| Profile | Case | Description | Result | Assertions |
| --- | --- | --- | --- | ---: |
| C01 | C01 | Supplied PI differs only in local command variable name | PASS | 5 |
| C02 | C02 | Candidate preserves entire control block and all existing feature source bytes | PASS | 4 |
| C03 | C03 | Candidate replaces UART calls, preserves CSV format, and services SWO in foreground | PASS | 5 |
| repository_PI_O0 | PI01 | Actual configuration is 3 C setpoint with cooling controller sign | PASS | 2 |
| repository_PI_O0 | PI02 | Fresh-state current requests reduce as filtered temperature falls | PASS | 18 |
| repository_PI_O0 | PI03 | Persistent 2 C feedback still requests 0.5 A and never OFF | PASS | 86402 |
| repository_PI_O0 | PI04 | Persistent minus 5 C feedback still requests the nonzero floor | PASS | 3601 |
| repository_PI_O0 | PI05 | Accumulated integral can retain multiamp commands below the setpoint | PASS | 2 |
| repository_PI_O0 | PI06 | Synthetic cooling history reproduces continued current at 2 C and minus 5 C | PASS | 1803 |
| repository_PI_O0 | PI07 | EMA lag separates raw logged temperature from PI feedback after a fast step | PASS | 2 |
| repository_PI_O0 | PI08 | Current mapping has no zero-current OFF code and retains all 131 limit steps | PASS | 133 |
| repository_PI_O0 | PI09 | 8 C request depends on integral history and has no new cutoff branch | PASS | 2 |
| repository_PI_O2 | PI01 | Actual configuration is 3 C setpoint with cooling controller sign | PASS | 2 |
| repository_PI_O2 | PI02 | Fresh-state current requests reduce as filtered temperature falls | PASS | 18 |
| repository_PI_O2 | PI03 | Persistent 2 C feedback still requests 0.5 A and never OFF | PASS | 86402 |
| repository_PI_O2 | PI04 | Persistent minus 5 C feedback still requests the nonzero floor | PASS | 3601 |
| repository_PI_O2 | PI05 | Accumulated integral can retain multiamp commands below the setpoint | PASS | 2 |
| repository_PI_O2 | PI06 | Synthetic cooling history reproduces continued current at 2 C and minus 5 C | PASS | 1803 |
| repository_PI_O2 | PI07 | EMA lag separates raw logged temperature from PI feedback after a fast step | PASS | 2 |
| repository_PI_O2 | PI08 | Current mapping has no zero-current OFF code and retains all 131 limit steps | PASS | 133 |
| repository_PI_O2 | PI09 | 8 C request depends on integral history and has no new cutoff branch | PASS | 2 |
| supplied_PI_O0 | PI01 | Actual configuration is 3 C setpoint with cooling controller sign | PASS | 2 |
| supplied_PI_O0 | PI02 | Fresh-state current requests reduce as filtered temperature falls | PASS | 18 |
| supplied_PI_O0 | PI03 | Persistent 2 C feedback still requests 0.5 A and never OFF | PASS | 86402 |
| supplied_PI_O0 | PI04 | Persistent minus 5 C feedback still requests the nonzero floor | PASS | 3601 |
| supplied_PI_O0 | PI05 | Accumulated integral can retain multiamp commands below the setpoint | PASS | 2 |
| supplied_PI_O0 | PI06 | Synthetic cooling history reproduces continued current at 2 C and minus 5 C | PASS | 1803 |
| supplied_PI_O0 | PI07 | EMA lag separates raw logged temperature from PI feedback after a fast step | PASS | 2 |
| supplied_PI_O0 | PI08 | Current mapping has no zero-current OFF code and retains all 131 limit steps | PASS | 133 |
| supplied_PI_O0 | PI09 | 8 C request depends on integral history and has no new cutoff branch | PASS | 2 |
| supplied_PI_O2 | PI01 | Actual configuration is 3 C setpoint with cooling controller sign | PASS | 2 |
| supplied_PI_O2 | PI02 | Fresh-state current requests reduce as filtered temperature falls | PASS | 18 |
| supplied_PI_O2 | PI03 | Persistent 2 C feedback still requests 0.5 A and never OFF | PASS | 86402 |
| supplied_PI_O2 | PI04 | Persistent minus 5 C feedback still requests the nonzero floor | PASS | 3601 |
| supplied_PI_O2 | PI05 | Accumulated integral can retain multiamp commands below the setpoint | PASS | 2 |
| supplied_PI_O2 | PI06 | Synthetic cooling history reproduces continued current at 2 C and minus 5 C | PASS | 1803 |
| supplied_PI_O2 | PI07 | EMA lag separates raw logged temperature from PI feedback after a fast step | PASS | 2 |
| supplied_PI_O2 | PI08 | Current mapping has no zero-current OFF code and retains all 131 limit steps | PASS | 133 |
| supplied_PI_O2 | PI09 | 8 C request depends on integral history and has no new cutoff branch | PASS | 2 |
| C04 | C04 | All four actual C PI trace files are byte-identical | PASS | 2 |
| features_O0 | T01 | All five independent channels and nominal conversion | PASS | 8 |
| features_O0 | T02 | Each individual channel invalidates independently | PASS | 10 |
| features_O0 | T03 | Exhaustive 4096-code sweep for every sensor | PASS | 40960 |
| features_O0 | T04 | Open short near rails and invalid output pointer | PASS | 19 |
| features_O0 | T05 | Freshness boundary and unsigned wraparound | PASS | 3 |
| features_O0 | H01 | Exact trip and recovery with retained hysteresis | PASS | 6 |
| features_O0 | H02 | NaN infinity invalid flag and recovery | PASS | 5 |
| features_O0 | H03 | Repeated noisy temperature sweep stays inhibited until recovery | PASS | 30000 |
| features_O0 | L01 | Boot closed requires 30 ms qualification | PASS | 3 |
| features_O0 | L02 | Open immediate closure bounce resets timer | PASS | 3 |
| features_O0 | L03 | Closure qualification crosses millisecond rollover | PASS | 2 |
| features_O0 | B01 | Wake beep on-off-on boundaries | PASS | 8 |
| features_O0 | B02 | Sleep tone and preemption | PASS | 2 |
| features_O0 | B03 | Twenty second lid alarm and ten second repeat | PASS | 5 |
| features_O0 | B04 | Alarm cancellation on close and OFF resets delay | PASS | 4 |
| features_O0 | B05 | Buzzer and alarm wraparound | PASS | 4 |
| features_O0 | N01 | Blank storage nominal defaults without flash writes | PASS | 6 |
| features_O0 | N02 | Save reboot load newer record and previous copy retained | PASS | 8 |
| features_O0 | N03 | Every single-bit record corruption falls back to previous copy | PASS | 1536 |
| features_O0 | N04 | Power interruption at every write and partial double-word byte | PASS | 256 |
| features_O0 | N05 | Erase and read failure preserve usable calibration | PASS | 5 |
| features_O0 | N06 | Both corrupted records use defaults without writes | PASS | 3 |
| features_O0 | N07 | Invalid calibration cannot erase or program flash | PASS | 15 |
| features_O0 | N08 | Calibrated conversion fractional counts signed current and invalid raw | PASS | 5 |
| features_O0 | N09 | Persisted generation counter wraps from maximum to zero | PASS | 10 |
| features_O0 | N10 | Incomplete storage interface never erases calibration | PASS | 5 |
| features_O0 | N11 | Lost readback acknowledgment accepts only a complete committed copy | PASS | 7 |
| features_O0 | S01 | User mapping endpoints clamp and intermediate levels | PASS | 5 |
| features_O0 | S02 | Every LED threshold below exact and above | PASS | 17 |
| features_O0 | S03 | Missing invalid and nonfinite battery data never lights bars | PASS | 4 |
| features_O0 | S04 | Monotonic voltage sweep with five-bit-only output | PASS | 40002 |
| features_O0 | S05 | ESP32 sampling 25 current then 25 voltage at 10 ms and 50 ms rest | PASS | 105 |
| features_O0 | S06 | Fractional averages and no sample replay after delayed foreground | PASS | 5 |
| features_O0 | S07 | Acquisition failure resets complete batch and invalidates old average | PASS | 4 |
| features_O0 | S08 | Sampling deadline crosses unsigned rollover | PASS | 3 |
| swo_O0 | S01 | Approved board initializes PB3 AF0, async 2 MHz and ITM port 0 without changing other GPIO bits | PASS | 10 |
| swo_O0 | S02 | A missing debugger discards logging without waiting or touching UART registers | PASS | 6 |
| swo_O0 | S03 | Each disabled trace gate independently drops output | PASS | 12 |
| swo_O0 | S04 | Busy ITM leaves queued bytes intact and returns immediately | PASS | 5 |
| swo_O0 | S05 | Service sends at most 64 bytes per main-loop pass | PASS | 4 |
| swo_O0 | S06 | Queue accepts exact capacity and drops an overflowing message in full | PASS | 6 |
| swo_O0 | S07 | Messages wrap without reordering over thousands of queue cycles | PASS | 8000 |
| swo_O0 | S08 | Null oversized and zero-length writes cannot overrun queue storage | PASS | 4 |
| swo_O0 | S09 | Detaching before service discards queued data and reconnect accepts fresh messages | PASS | 5 |
| swo_O0 | S10 | Detaching during a service pass stops transmission and discards the remainder | PASS | 4 |
| swo_O0 | S11 | Reinitializing discards pending data rather than replaying stale output | PASS | 3 |
| swo_O0 | S12 | Incorrect core clock refuses initialization | PASS | 3 |
| swo_O0 | S13 | Printf character retarget uses the same queue and reports rejected writes | PASS | 3 |
| swo_O0 | S14 | Diagnostic counter overflow cannot corrupt queue or disable forward progress | PASS | 5 |
| platform_O0_lid0 | P01 | Real acquisition uses PC0-PC4 channel mapping and 16 samples each | PASS | 11 |
| platform_O0_lid0 | P02 | Real GPIO and sampling-time configuration covers every sensor | PASS | 8 |
| platform_O0_lid0 | P03 | ADC stuck conversion returns and requests direct shutdown | PASS | 5 |
| platform_O0_lid0 | P04 | ADC timeout also terminates with a frozen DWT clock | PASS | 2 |
| platform_O0_lid0 | P05 | ADC overrun and invalid channel rejected | PASS | 4 |
| platform_O0_lid0 | P06 | Any individual sensor failure prevents cooling and latches fault | PASS | 15 |
| platform_O0_lid0 | P07 | Hot cutoff pauses cooling keeps fans and retains user request | PASS | 5 |
| platform_O0_lid0 | P08 | Cooling restarts only after recovery with fresh qualification | PASS | 6 |
| platform_O0_lid0 | P09 | IRQ cuts power before foreground can process lid | PASS | 3 |
| platform_O0_lid0 | P10 | Close after IRQ still waits 30 ms then restarts configuration | PASS | 4 |
| platform_O0_lid0 | P11 | Every startup phase can be cancelled by lid without latching a fault | PASS | 21 |
| platform_O0_lid0 | P12 | Fault or user OFF prevents automatic resume | PASS | 4 |
| platform_O0_lid0 | P13 | IRQ stale-sample cutoff works during blocked foreground and rollover | PASS | 3 |
| platform_O0_lid0 | P14 | TIM1 CH3 PA10 5 kHz carrier leaves PA8 configuration alone | PASS | 6 |
| platform_O0_lid0 | P15 | Real interrupt services wake and sleep tone without advancing foreground | PASS | 5 |
| platform_O0_lid0 | P16 | IRQ raises lid alarm at twenty seconds without any foreground service | PASS | 5 |
| platform_O0_lid0 | P17 | Buzzer request preserves a preexisting interrupt mask | PASS | 1 |
| platform_O0_lid0 | P18 | Save request rejects active cooling invalid key and invalid coefficients | PASS | 3 |
| platform_O0_lid0 | P19 | Calibration region read checks and ECC recovery are restricted | PASS | 7 |
| platform_O0_lid0 | P20 | Real indicator uses calibrated voltage masks and clears on OFF | PASS | 3 |
| platform_O0_lid0 | P21 | Stale or missing battery sample clears indicator | PASS | 2 |
| platform_O0_lid0 | P22 | SWO diagnostic row preserves original fields and appends all ten PI states | PASS | 5 |
| platform_O0_lid0 | P23 | Original current-code mapping PI limits and reset remain intact | PASS | 134 |
| platform_O0_lid0 | P24 | Simultaneous hot and lid inhibit needs both to recover | PASS | 3 |
| platform_O0_lid0 | P25 | Lid pulse during ADC publication cannot be lost by foreground flag clearing | PASS | 5 |
| platform_O0_lid0 | P26 | Failed flash save retains active coefficients and reports failure | PASS | 2 |
| platform_O0_lid0 | P27 | Diagnostic row handles invalid floats and maximum counters without truncation | PASS | 5 |
| platform_O0_lid0 | P28 | Logging observes PI and converter state without changing control commands | PASS | 8 |
| platform_O0_lid1 | P01 | Real acquisition uses PC0-PC4 channel mapping and 16 samples each | PASS | 11 |
| platform_O0_lid1 | P02 | Real GPIO and sampling-time configuration covers every sensor | PASS | 8 |
| platform_O0_lid1 | P03 | ADC stuck conversion returns and requests direct shutdown | PASS | 5 |
| platform_O0_lid1 | P04 | ADC timeout also terminates with a frozen DWT clock | PASS | 2 |
| platform_O0_lid1 | P05 | ADC overrun and invalid channel rejected | PASS | 4 |
| platform_O0_lid1 | P06 | Any individual sensor failure prevents cooling and latches fault | PASS | 15 |
| platform_O0_lid1 | P07 | Hot cutoff pauses cooling keeps fans and retains user request | PASS | 5 |
| platform_O0_lid1 | P08 | Cooling restarts only after recovery with fresh qualification | PASS | 6 |
| platform_O0_lid1 | P09 | IRQ cuts power before foreground can process lid | PASS | 3 |
| platform_O0_lid1 | P10 | Close after IRQ still waits 30 ms then restarts configuration | PASS | 4 |
| platform_O0_lid1 | P11 | Every startup phase can be cancelled by lid without latching a fault | PASS | 21 |
| platform_O0_lid1 | P12 | Fault or user OFF prevents automatic resume | PASS | 4 |
| platform_O0_lid1 | P13 | IRQ stale-sample cutoff works during blocked foreground and rollover | PASS | 3 |
| platform_O0_lid1 | P14 | TIM1 CH3 PA10 5 kHz carrier leaves PA8 configuration alone | PASS | 6 |
| platform_O0_lid1 | P15 | Real interrupt services wake and sleep tone without advancing foreground | PASS | 5 |
| platform_O0_lid1 | P16 | IRQ raises lid alarm at twenty seconds without any foreground service | PASS | 5 |
| platform_O0_lid1 | P17 | Buzzer request preserves a preexisting interrupt mask | PASS | 1 |
| platform_O0_lid1 | P18 | Save request rejects active cooling invalid key and invalid coefficients | PASS | 3 |
| platform_O0_lid1 | P19 | Calibration region read checks and ECC recovery are restricted | PASS | 7 |
| platform_O0_lid1 | P20 | Real indicator uses calibrated voltage masks and clears on OFF | PASS | 3 |
| platform_O0_lid1 | P21 | Stale or missing battery sample clears indicator | PASS | 2 |
| platform_O0_lid1 | P22 | SWO diagnostic row preserves original fields and appends all ten PI states | PASS | 5 |
| platform_O0_lid1 | P23 | Original current-code mapping PI limits and reset remain intact | PASS | 134 |
| platform_O0_lid1 | P24 | Simultaneous hot and lid inhibit needs both to recover | PASS | 3 |
| platform_O0_lid1 | P25 | Lid pulse during ADC publication cannot be lost by foreground flag clearing | PASS | 5 |
| platform_O0_lid1 | P26 | Failed flash save retains active coefficients and reports failure | PASS | 2 |
| platform_O0_lid1 | P27 | Diagnostic row handles invalid floats and maximum counters without truncation | PASS | 5 |
| platform_O0_lid1 | P28 | Logging observes PI and converter state without changing control commands | PASS | 8 |
| features_O2 | T01 | All five independent channels and nominal conversion | PASS | 8 |
| features_O2 | T02 | Each individual channel invalidates independently | PASS | 10 |
| features_O2 | T03 | Exhaustive 4096-code sweep for every sensor | PASS | 40960 |
| features_O2 | T04 | Open short near rails and invalid output pointer | PASS | 19 |
| features_O2 | T05 | Freshness boundary and unsigned wraparound | PASS | 3 |
| features_O2 | H01 | Exact trip and recovery with retained hysteresis | PASS | 6 |
| features_O2 | H02 | NaN infinity invalid flag and recovery | PASS | 5 |
| features_O2 | H03 | Repeated noisy temperature sweep stays inhibited until recovery | PASS | 30000 |
| features_O2 | L01 | Boot closed requires 30 ms qualification | PASS | 3 |
| features_O2 | L02 | Open immediate closure bounce resets timer | PASS | 3 |
| features_O2 | L03 | Closure qualification crosses millisecond rollover | PASS | 2 |
| features_O2 | B01 | Wake beep on-off-on boundaries | PASS | 8 |
| features_O2 | B02 | Sleep tone and preemption | PASS | 2 |
| features_O2 | B03 | Twenty second lid alarm and ten second repeat | PASS | 5 |
| features_O2 | B04 | Alarm cancellation on close and OFF resets delay | PASS | 4 |
| features_O2 | B05 | Buzzer and alarm wraparound | PASS | 4 |
| features_O2 | N01 | Blank storage nominal defaults without flash writes | PASS | 6 |
| features_O2 | N02 | Save reboot load newer record and previous copy retained | PASS | 8 |
| features_O2 | N03 | Every single-bit record corruption falls back to previous copy | PASS | 1536 |
| features_O2 | N04 | Power interruption at every write and partial double-word byte | PASS | 256 |
| features_O2 | N05 | Erase and read failure preserve usable calibration | PASS | 5 |
| features_O2 | N06 | Both corrupted records use defaults without writes | PASS | 3 |
| features_O2 | N07 | Invalid calibration cannot erase or program flash | PASS | 15 |
| features_O2 | N08 | Calibrated conversion fractional counts signed current and invalid raw | PASS | 5 |
| features_O2 | N09 | Persisted generation counter wraps from maximum to zero | PASS | 10 |
| features_O2 | N10 | Incomplete storage interface never erases calibration | PASS | 5 |
| features_O2 | N11 | Lost readback acknowledgment accepts only a complete committed copy | PASS | 7 |
| features_O2 | S01 | User mapping endpoints clamp and intermediate levels | PASS | 5 |
| features_O2 | S02 | Every LED threshold below exact and above | PASS | 17 |
| features_O2 | S03 | Missing invalid and nonfinite battery data never lights bars | PASS | 4 |
| features_O2 | S04 | Monotonic voltage sweep with five-bit-only output | PASS | 40002 |
| features_O2 | S05 | ESP32 sampling 25 current then 25 voltage at 10 ms and 50 ms rest | PASS | 105 |
| features_O2 | S06 | Fractional averages and no sample replay after delayed foreground | PASS | 5 |
| features_O2 | S07 | Acquisition failure resets complete batch and invalidates old average | PASS | 4 |
| features_O2 | S08 | Sampling deadline crosses unsigned rollover | PASS | 3 |
| swo_O2 | S01 | Approved board initializes PB3 AF0, async 2 MHz and ITM port 0 without changing other GPIO bits | PASS | 10 |
| swo_O2 | S02 | A missing debugger discards logging without waiting or touching UART registers | PASS | 6 |
| swo_O2 | S03 | Each disabled trace gate independently drops output | PASS | 12 |
| swo_O2 | S04 | Busy ITM leaves queued bytes intact and returns immediately | PASS | 5 |
| swo_O2 | S05 | Service sends at most 64 bytes per main-loop pass | PASS | 4 |
| swo_O2 | S06 | Queue accepts exact capacity and drops an overflowing message in full | PASS | 6 |
| swo_O2 | S07 | Messages wrap without reordering over thousands of queue cycles | PASS | 8000 |
| swo_O2 | S08 | Null oversized and zero-length writes cannot overrun queue storage | PASS | 4 |
| swo_O2 | S09 | Detaching before service discards queued data and reconnect accepts fresh messages | PASS | 5 |
| swo_O2 | S10 | Detaching during a service pass stops transmission and discards the remainder | PASS | 4 |
| swo_O2 | S11 | Reinitializing discards pending data rather than replaying stale output | PASS | 3 |
| swo_O2 | S12 | Incorrect core clock refuses initialization | PASS | 3 |
| swo_O2 | S13 | Printf character retarget uses the same queue and reports rejected writes | PASS | 3 |
| swo_O2 | S14 | Diagnostic counter overflow cannot corrupt queue or disable forward progress | PASS | 5 |
| platform_O2_lid0 | P01 | Real acquisition uses PC0-PC4 channel mapping and 16 samples each | PASS | 11 |
| platform_O2_lid0 | P02 | Real GPIO and sampling-time configuration covers every sensor | PASS | 8 |
| platform_O2_lid0 | P03 | ADC stuck conversion returns and requests direct shutdown | PASS | 5 |
| platform_O2_lid0 | P04 | ADC timeout also terminates with a frozen DWT clock | PASS | 2 |
| platform_O2_lid0 | P05 | ADC overrun and invalid channel rejected | PASS | 4 |
| platform_O2_lid0 | P06 | Any individual sensor failure prevents cooling and latches fault | PASS | 15 |
| platform_O2_lid0 | P07 | Hot cutoff pauses cooling keeps fans and retains user request | PASS | 5 |
| platform_O2_lid0 | P08 | Cooling restarts only after recovery with fresh qualification | PASS | 6 |
| platform_O2_lid0 | P09 | IRQ cuts power before foreground can process lid | PASS | 3 |
| platform_O2_lid0 | P10 | Close after IRQ still waits 30 ms then restarts configuration | PASS | 4 |
| platform_O2_lid0 | P11 | Every startup phase can be cancelled by lid without latching a fault | PASS | 21 |
| platform_O2_lid0 | P12 | Fault or user OFF prevents automatic resume | PASS | 4 |
| platform_O2_lid0 | P13 | IRQ stale-sample cutoff works during blocked foreground and rollover | PASS | 3 |
| platform_O2_lid0 | P14 | TIM1 CH3 PA10 5 kHz carrier leaves PA8 configuration alone | PASS | 6 |
| platform_O2_lid0 | P15 | Real interrupt services wake and sleep tone without advancing foreground | PASS | 5 |
| platform_O2_lid0 | P16 | IRQ raises lid alarm at twenty seconds without any foreground service | PASS | 5 |
| platform_O2_lid0 | P17 | Buzzer request preserves a preexisting interrupt mask | PASS | 1 |
| platform_O2_lid0 | P18 | Save request rejects active cooling invalid key and invalid coefficients | PASS | 3 |
| platform_O2_lid0 | P19 | Calibration region read checks and ECC recovery are restricted | PASS | 7 |
| platform_O2_lid0 | P20 | Real indicator uses calibrated voltage masks and clears on OFF | PASS | 3 |
| platform_O2_lid0 | P21 | Stale or missing battery sample clears indicator | PASS | 2 |
| platform_O2_lid0 | P22 | SWO diagnostic row preserves original fields and appends all ten PI states | PASS | 5 |
| platform_O2_lid0 | P23 | Original current-code mapping PI limits and reset remain intact | PASS | 134 |
| platform_O2_lid0 | P24 | Simultaneous hot and lid inhibit needs both to recover | PASS | 3 |
| platform_O2_lid0 | P25 | Lid pulse during ADC publication cannot be lost by foreground flag clearing | PASS | 5 |
| platform_O2_lid0 | P26 | Failed flash save retains active coefficients and reports failure | PASS | 2 |
| platform_O2_lid0 | P27 | Diagnostic row handles invalid floats and maximum counters without truncation | PASS | 5 |
| platform_O2_lid0 | P28 | Logging observes PI and converter state without changing control commands | PASS | 8 |
| platform_O2_lid1 | P01 | Real acquisition uses PC0-PC4 channel mapping and 16 samples each | PASS | 11 |
| platform_O2_lid1 | P02 | Real GPIO and sampling-time configuration covers every sensor | PASS | 8 |
| platform_O2_lid1 | P03 | ADC stuck conversion returns and requests direct shutdown | PASS | 5 |
| platform_O2_lid1 | P04 | ADC timeout also terminates with a frozen DWT clock | PASS | 2 |
| platform_O2_lid1 | P05 | ADC overrun and invalid channel rejected | PASS | 4 |
| platform_O2_lid1 | P06 | Any individual sensor failure prevents cooling and latches fault | PASS | 15 |
| platform_O2_lid1 | P07 | Hot cutoff pauses cooling keeps fans and retains user request | PASS | 5 |
| platform_O2_lid1 | P08 | Cooling restarts only after recovery with fresh qualification | PASS | 6 |
| platform_O2_lid1 | P09 | IRQ cuts power before foreground can process lid | PASS | 3 |
| platform_O2_lid1 | P10 | Close after IRQ still waits 30 ms then restarts configuration | PASS | 4 |
| platform_O2_lid1 | P11 | Every startup phase can be cancelled by lid without latching a fault | PASS | 21 |
| platform_O2_lid1 | P12 | Fault or user OFF prevents automatic resume | PASS | 4 |
| platform_O2_lid1 | P13 | IRQ stale-sample cutoff works during blocked foreground and rollover | PASS | 3 |
| platform_O2_lid1 | P14 | TIM1 CH3 PA10 5 kHz carrier leaves PA8 configuration alone | PASS | 6 |
| platform_O2_lid1 | P15 | Real interrupt services wake and sleep tone without advancing foreground | PASS | 5 |
| platform_O2_lid1 | P16 | IRQ raises lid alarm at twenty seconds without any foreground service | PASS | 5 |
| platform_O2_lid1 | P17 | Buzzer request preserves a preexisting interrupt mask | PASS | 1 |
| platform_O2_lid1 | P18 | Save request rejects active cooling invalid key and invalid coefficients | PASS | 3 |
| platform_O2_lid1 | P19 | Calibration region read checks and ECC recovery are restricted | PASS | 7 |
| platform_O2_lid1 | P20 | Real indicator uses calibrated voltage masks and clears on OFF | PASS | 3 |
| platform_O2_lid1 | P21 | Stale or missing battery sample clears indicator | PASS | 2 |
| platform_O2_lid1 | P22 | SWO diagnostic row preserves original fields and appends all ten PI states | PASS | 5 |
| platform_O2_lid1 | P23 | Original current-code mapping PI limits and reset remain intact | PASS | 134 |
| platform_O2_lid1 | P24 | Simultaneous hot and lid inhibit needs both to recover | PASS | 3 |
| platform_O2_lid1 | P25 | Lid pulse during ADC publication cannot be lost by foreground flag clearing | PASS | 5 |
| platform_O2_lid1 | P26 | Failed flash save retains active coefficients and reports failure | PASS | 2 |
| platform_O2_lid1 | P27 | Diagnostic row handles invalid floats and maximum counters without truncation | PASS | 5 |
| platform_O2_lid1 | P28 | Logging observes PI and converter state without changing control commands | PASS | 8 |
| swo_disabled | S15 | Unmodified-board build leaves PB3 and trace configuration untouched | PASS | 4 |
| C05 | C05 | Production source bytes were preserved during preparation and validation | PASS | 2 |
