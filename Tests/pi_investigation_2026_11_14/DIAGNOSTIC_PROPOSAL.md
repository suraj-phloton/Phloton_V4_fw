# Approved PI diagnostic logging — 15 November 2026

**Explicitly approved and applied to production firmware on 15 November. No board was flashed.**

The reported run has no saved log. Known observations are STM32-reported -5 C, bench input 12.5 V / 4.2 A, both fans ON, and reported fan consumption 350 mA. These do not identify the PI request or prove that the converter enforced it. The next useful measurement must connect feedback, integral, requested limit, verified register value and measured output.

The user approved: “Apply the diagnostic logging change.” The change gathers evidence; it does not correct the original controller's nonzero current floor or integral behavior.

## Exact applied change

[PROPOSED_PI_DIAGNOSTICS.patch](PROPOSED_PI_DIAGNOSTICS.patch) changes only:

1. Firmware build ID to `PHV4-PI-DIAGNOSTICS-20261115-A`.
2. UART/SD CSV from 22 to 32 columns, appending the ten diagnostic values below.
3. SD filename to `TEC_PI.CSV`, so diagnostic records are kept separate from the 22-column file.

The entire PI/control block, including cold-sink selection, 3 C setpoint, gains, filter, limits, current mapping and ILIM transactions, remains text-identical to the pre-diagnostic firmware. It adds no control commands or extra I2C reads. Foreground formatting/transport takes slightly more time because rows are longer; actual timing and SD behavior remain physical acceptance items.

| CSV column | New field | Meaning |
| --- | --- | --- |
| 23 | cold_feedback_filtered_C | Actual EMA-filtered cold-sink temperature passed to the PI |
| 24 | pi_integral_A | Accepted integral state, distinct from the existing provisional integral |
| 25 | pi_requested_current_A | Output request after existing limits/rounding |
| 26 | ilim_code_requested | Requested register code, printed in decimal |
| 27 | ilim_code_readback | Verified ILIM register value, printed in decimal |
| 28 | ilim_verified | Last write/readback matched |
| 29 | control_cycles | Number of executed PI control steps |
| 30 | startup_state | Existing converter state; RUN=7 |
| 31 | pi_sat_low | Existing low saturation indicator |
| 32 | pi_sat_high | Existing high saturation indicator |

The first 22 fields retain their order and meanings, including time, measured TEC voltage/current, raw cold-sink temperature and validity. The SD file receives the matching 32-name header. UART retains its startup banner/build ID followed by data rows; it does not gain a CSV-header line. The existing ESP32 byte-forwarding sketch can forward the longer rows without parsing changes. Fixed-column PC/cloud parsers must select the diagnostic schema.

These are foreground log snapshots at the existing approximate 1 Hz rate. They do not measure emergency cutoff latency or prove sensor/current calibration. Requested/verified ILIM is not a physical current measurement. After pause/OFF some PI diagnostics may retain an earlier value, so evaluate them together with time, request, fault, startup state and advancing control_cycles.

## Validation performed on the isolated candidate

Final run: [20261115T145552145748Z](diagnostic_results/20261115T145552145748Z/summary.json).

| Check | Result |
| --- | --- |
| Proposed patch applies to the existing working files (`git apply --check`) | PASS |
| Host integration compile/run at O0, UBSan and stack protection | PASS |
| Host integration compile/run at O2, UBSan and stack protection | PASS |
| Real formatter emits 32 fields, preserves original values and appends the ten states in order | PASS, both host profiles |
| Diagnostic SD header has exactly 32 fields in matching order | PASS |
| GNU Arm strict syntax check | PASS |
| Full STM32 GNU Arm link at O0 and Os | PASS |
| Whole PI/control source block unchanged | PASS |
| Production main.c/application include SHA-256 unchanged | PASS |
| Physical flashing, UART capture, thermal response and current limiting | NOT RUN |

The runner recorded **8 command checks passed, 0 failed**, with **52 integration case executions and 544 assertions**. The separate header-count check also passed. Existing target-link warnings about an unused sd_verify buffer and an RWX LOAD segment remain visible in raw stderr. These software passes do not qualify the thermal controller or analog current regulator.

The earlier candidate run passed seven compile/run/build checks, but an additional packaging check exposed mixed line endings in the patch: main.c is CRLF and the application include is LF. Patch generation was corrected to preserve each file's line endings, and the final patch check passed. An initial preparation-script string syntax error was also corrected before candidate compilation. No production changes were made during those corrections.

The preparation runner is [prepare_diagnostics.py](prepare_diagnostics.py). It reproduces the historical candidate from Git baseline 37c9970, creating sources, executables and ELF images only under ignored `.build/diagnostic_candidate`. It detects whether that candidate is already installed and checks the appropriate patch direction. Logs and fingerprints are retained under `diagnostic_results/`; the earlier candidate records above remain historical evidence.

```powershell
& 'C:/Espressif/tools/idf-python/3.11.2/python.exe' Tests/pi_investigation_2026_11_14/prepare_diagnostics.py
```

It does not apply the patch or flash hardware. Matching project tests now expect the 32-column diagnostic schema, verify invalid-value/counter boundaries, and assert that logging leaves PI/converter state unchanged. The [applied regression report](APPLIED_TEST_RESULTS.md) records the complete current-source run; original 22-column evidence is retained separately. Reproduce the applied run using the command in the [firmware README](../feature_validation_2026_11_12/README.md).

## Capture procedure

1. Clean-build the updated project, retaining the 508 KiB application flash reservation. The existing .ioc does not require editing or regeneration. Fresh test ELFs are also generated under `../feature_validation_2026_11_12/.build/` by the applied regression runner.
2. Begin with TEC power disabled. Confirm build ID, stable sensor readings and the 32-field data format through the existing STM32-to-ESP32-to-USB path at 115200 baud.
3. Enable terminal logging to a file **before** a powered control test. Retain boot messages, all rows, the exact flashed build and contemporaneous bench voltage/current readings. Record whether 350 mA is the combined fan current.
4. Any powered test needs an external/manual stop before unwanted continued cooling. This candidate retains the original 0.5 A floor and has no new cold-temperature shutdown; reproducing -5 C is not needed to obtain the diagnostic states. Avoid halting the MCU with an energized power stage.
5. Inspect filtered feedback, integral, request, requested/readback ILIM, measured TEC voltage/current and advancing control_cycles together. A high request points to the control history; a low verified request with high measured TEC power points to the actuation/sensing path.

No serial port was opened, no commands were sent to a board, and no automatic capture was performed. The approved logging change is applied locally; this task does not commit or push it to GitHub.
