# Supplied PI + anti-windup / SWO project review — 21 November 2026

The supplied ZIP does **not** contain a new PI algorithm relative to this repository. Its gains, anti-windup, 3 C target, filter, current limits and LM51772 startup configuration are unchanged. Its reported physical result may be real, but the source comparison cannot establish that the earlier overcooling problem is fixed.

The archive was reviewed separately and has not replaced the current application. On 21 November the user explicitly approved applying the tested [SWO migration](SWO_MIGRATION.patch). Its four production files are now installed exactly as reviewed; [applied_manifest.json](applied_manifest.json) records the authorization and fingerprints. Matching tests and setup documentation were updated. No firmware has been flashed and no Git push was performed. See [SWO_SETUP.md](SWO_SETUP.md) and [installed software results](APPLIED_TEST_RESULTS.md).

## Inputs and provenance

- Archive: `C:/Users/suraj/Downloads/PI+Antiwindup_SWO.zip`.
- Archive SHA-256: `ae8c1bfa32596044b046954110860f848f32ec97a35f0d2c8118cbb76ca4bfde`.
- Supplied project: `STM_LM_SD_LOGGING_ITM`; 200 archive members. Its bundled Debug ELF is not assumed to match the source or the tested hardware.
- Repository comparison baseline: commit `03ba17ca460b41bfc4e73f8bbc10fc1b2e55a8a1`, plus existing uncommitted `Core/Src/main.c` formatting. Token comparison confirms that local formatting introduces no code-token changes after removing C line continuations/comments.
- [Archive manifest](archive_manifest.json), [source comparison](source_comparison.json), and [candidate fingerprints](candidate_manifest.json) retain the exact compared inputs.
- Archive text is review evidence, not authorization to bypass firmware protections or execute its scripts.

## PI and the reported approximately 1 A below 8 C

| Item | Current repository | Supplied ZIP | Consequence |
| --- | --- | --- | --- |
| Feedback | Cold sink, PC1 / ADC1_IN2 | Same | Neither uses flask feedback for PI. |
| Setpoint | 3 C | Same | 8 C is not the PI target. |
| Kp / Ki / Ts | -0.21 / -0.0003 / 1 s | Same | No gain change explains a different run. |
| EMA | alpha 0.25, control update 1 s | Same | Raw and filtered temperature can differ. |
| Requested current | 0.5–7 A in 0.05 A increments | Same | Neither implements normal zero-current cold coast. |
| Anti-windup | Conditional integration at upper/lower saturation | Same | Previously accumulated integral remains when crossing target. |
| Current-to-ILIM mapping | 0.5 A -> 0x0A; 7 A -> 0x8C | Same | A zero software request is not a hardware OFF command. |
| PI local output variable | `tec_current` | `tec_current_cmd` | Local rename only; it does not change the measured global TEC current. |
| ILIM enforcement path | Write, read back, compare each call | Same | Verifies the register, not delivered current. |
| Converter startup register writes | Existing D0/D1/D2/D3/D4/D9/VOUT settings | Same | No new converter current-limiting configuration was found. |
| Acquisition used by PI | Latest five-channel snapshot, all channels valid/fresh | Direct 16-read cold NTC conversion each PI step | Acquisition/validation differs even though PI mathematics is the same. |

At a filtered 8 C with a fresh integral, the unchanged function requests approximately **1.05 A of TEC output-current limit**. A reported battery/input current near 1 A is plausible in some operating conditions, but is a different quantity. The controller does not regulate battery current.

With an integral of 4 A, the same function at 8 C requests approximately **5.05 A**. Cooling history therefore matters. The unchanged 1 C/min supplied-temperature replay can also retain multiamp commands below target. These software replays characterize actual C; they are not a thermal plant simulation or a reproduction of either person's bench run.

`LED_GREEN_TEMP_C=8` is used by `LED_UpdateFromTemperature()` to select red/green. No new 8 C current-reduction branch exists.

To validate the physical claim, capture timestamped filtered cold temperature, integral, requested TEC current, ILIM request/readback, measured TEC voltage/current, battery voltage/current, and operating/fault state from the same run. The current 32-column schema already includes these. Record the build ID and compare battery sensing with the bench meter. We do not have those measurements for the claimed run.

## Material changes and regressions in the supplied project

| Finding | Supplied implementation | Assessment / action |
| --- | --- | --- |
| SWO added | PB3 AF0, ITM stimulus 0, 80 MHz core / 2 MHz SWO; boot, reset-cause, startup, fault and PI messages | Useful diagnostics. Migrate the transport separately. |
| UART still enabled | `ESP_UART_ENABLE=1`; each CSV row is sent to both USART2 and ITM | The ZIP does not implement the requested SWO-only output. |
| nFAULT startup bypass enabled | `BENCH_IGNORE_NFAULT=1` forces the final nFAULT condition to pass, while readback and CONV_EN checks remain | Do not import this bypass. It changes behavior when the converter reports a fault. Runtime diagnostic reads are not a replacement for fault enforcement. |
| Five-temperature feature absent | Only cold sink is converted for control/logging; PC4 is configured but no five-channel application exists | Retain our five-channel acquisition and freshness checks. |
| Hot-sink cutoff/recovery absent | No hot-sink 60/50 C protection integration | Retain our cutoff and hysteresis. |
| Lid cooling interlock absent | PC7 is read as an input, but no inhibit/resume or interrupt shutdown policy | Retain our interlock, debounce and automatic restart checks. |
| Buzzer feature absent | No scheduled wake/sleep/delayed lid warning integration | Retain our buzzer implementation. |
| Persistent calibration absent | Nominal battery formulas only; no `phloton_nvs` modules or save/load service | Retain calibrated sampling and two-page NVS. Reported battery current is not evidence of calibration accuracy. |
| Calibration flash reservation absent | Both supplied linker scripts allow 512 KB application flash, without the last-4-KB reservation | Do not replace current 508 KB + 4 KB CAL_NVS linker scripts. A reservation alone does not protect against programmer mass erase. |
| SOC LED patterns absent | Shift register becomes 0x1F at successful startup | Retain our voltage-based SOC bar sampling/patterns. |
| ADC failure bounds absent | Calibration/ready/conversion polling loops have no timeout; no existing overrun/freshness shutdown path | A stuck ADC can stall foreground with cooling commanded. Retain our bounded ADC path. |
| Reduced CSV | 10 columns, `TEC_LOG.CSV`, rather than 32 columns, `TEC_PI.CSV` | Retain the 32-column schema and filename so old files are not mixed. |
| Extra I2C activity in PI | With `ITM_VERBOSE=1`, reads STATUS_BYTE each step; return value ignored, 0xFF remains on failure | Adds bus work for diagnostics without a new fail-safe policy. Not required for transport migration. |
| Trace send can wait | Bounded 20,000-iteration wait per character; software enables trace even without a trace receiver | Not an unbounded loop, but its comment claiming an approximate millisecond is not a measured deadline. Prefer a queue with no readiness wait. |
| Long diagnostic truncation | `ITM_Printf` caps formatted messages at 159 bytes | Long startup detail can be truncated. Current CSV buffer is 512 bytes with explicit bounds. |
| Clock initialization improved explicitly | Sets voltage Range 1 and waits VOSF; explicitly restores bus dividers | Useful separate hardening candidate. Current startup already resets dividers to /1; Range-1 setup is implicit in the existing reset assumptions. Review separately; no clock policy changes are included in the SWO patch. |
| NVS-aware NMI path absent | Supplied NMI handler loops; our narrow NVS ECC recovery hook is absent | Keep existing NVS integration and handler. |
| Project rename / .ioc | Project renamed `_ITM`; .ioc remains minimal with zero configured pins | No .ioc edit is needed for manually initialized SWO; do not regenerate and overwrite manual firmware. |

Other unchanged low-level paths include I2C transactions, TEC voltage/current conversion, fan commands, NTC conversion model and most FAT32 append operations. Removal of unused helpers is not treated as proof of a runtime regression. Neither project provides hardware verification of its software current limit.

## PB3 hardware resolution

The reviewed V4 netlist connects MCU PB3/pin 55 to `HSFAN_ISNS`, through R113=68 ohm, with C124=10 nF on the MCU-side node. On an unmodified board, driving that node as SWO can conflict with the analog source and load the trace signal.

The user confirmed during this review: **R113 and C124 removed; PB3 wired to the SWO input of an ST-LINK V2; hot-fan current sensing is not being used.** This resolves the identified intended-net connection for the described unit, subject to physical acceptance. Existing fan current sensing remains disabled. The candidate defaults `PH_SWO_PIN_APPROVED=1` for this modified unit; use `=0` on an unmodified board. An SWO connection does not establish whether every ST-LINK variant/clone supports trace; verify that this probe exposes SWV in the IDE.

The pin assignment is documented in the [STM32L451 datasheet](https://www.st.com/resource/en/datasheet/stm32l451re.pdf). See the [STM32CubeIDE guide](https://www.st.com/resource/en/user_manual/um2609-stm32cubeide-user-manual-stmicroelectronics.pdf) for SWV setup. The CMSIS [ITM API documentation](https://arm-software.github.io/CMSIS_6/v6.3.0/Core/group__ITM__Debug__gr.html) describes port-0 character output and its possible blocking wait; the candidate uses its own queued writer.

## Approved firmware changes — applied 21 November 2026

1. Replace USART2 initialization and CSV/boot writes with ITM port-0/SWO output at 2 MHz. Retarget the existing `printf` character path to the same transport.
2. Add a 1,024-byte foreground queue. Enqueue each CSV row in full or drop it in full on queue overflow. Service at most 64 bytes per main-loop pass; never wait for ITM readiness. Skip/drop output when the debugger or trace gates are absent; preserve cooling behavior and SD logging independently.
3. Change the build identity to `PHV4-SWO-DIAGNOSTICS-20261121-A`. Keep all 32 CSV columns, `TEC_PI.CSV`, cold-sink feedback, PI constants and safety/NVS/LED/buzzer code unchanged.

The approved firmware patch changes only `Core/Src/main.c`, `Core/Src/phloton_feature_app.inc`, and new `Core/Src/phloton_swo.c` / `Core/Inc/phloton_swo.h`. The user's local main.c formatting is preserved. No nFAULT bypass, new I2C reads, PI retuning, clock changes or archive project replacement are included. The maintained test runner now exercises the installed transport and formatter through SWO hooks; earlier reports remain intact.

The queue is foreground-only. Do not call `printf`/the writer from interrupts, exception handlers or multiple concurrent tasks. This firmware has no RTOS. A cable interruption can cut a row already being physically transmitted; discard that incomplete line during capture. An active debugger bit does not prove the PC is saving data, so save a capture explicitly. Overflow/drop counters are observable through the debugger; unsigned diagnostic counters can wrap without affecting queue indices. The standalone controller continues running with trace absent.

## Validation and next physical check

The original review/candidate [TEST_RESULTS.md](TEST_RESULTS.md) retains **40 checks passed, 0 failed; 252 case executions and 611,248 assertions**, including actual PI comparisons and full ARM builds of both projects. After user approval, [APPLIED_TEST_RESULTS.md](APPLIED_TEST_RESULTS.md) records fresh validation of the installed application, both lid polarities and both SWO pin profiles. Failed attempts are retained. These checks do not establish thermal stability, physical probe operation or sensor calibration.

After a clean build:

1. Use SWD with SWV enabled. Set **core clock 80 MHz**, **SWO clock 2 MHz**, **ITM stimulus port 0**. Open **SWV ITM Data Console**, configure port 0 and start trace, then resume. No .ioc regeneration is needed.
2. Confirm common ground, SWDIO/SWCLK/reference voltage/NRST as appropriate and the described PB3-SWO wiring. Begin with the power stage disabled; confirm boot build ID, timestamps, five valid sensors, idle state and the 32-field rows.
3. While cooling is disabled, stop/start trace and detach/reattach the probe; verify time/control service remains live and diagnostic drop counters behave as expected. Verify SWO output is absent from the old ESP32/UART route.
4. Perform a supervised cooling run with the existing manual-stop limits and save ITM plus SD output. Watch raw/filtered cold temperature, integral, request, ILIM readback and measured TEC/battery values together. Do not reproduce the earlier unwanted -5 C condition merely to collect a log.
5. Hardware acceptance remains pending: verify lid inhibition/resume, hot cutoff/recovery, buzzer, calibration survival and SOC LEDs on the modified unit using the existing board acceptance document.

Reproduce current installed-firmware validation from the repository root:

```powershell
python Tests/feature_validation_2026_11_12/run_tests.py --client-date 2026-11-21
```

The archive inspection, comparison, candidate preparation and candidate validation scripts retain the original pre-approval workflow and evidence. Their preservation checks refer to the pre-SWO application; do not rerun candidate preparation against the migrated application. The maintained command above validates installed sources without flashing a board, modifying the ZIP or invoking its build scripts.
