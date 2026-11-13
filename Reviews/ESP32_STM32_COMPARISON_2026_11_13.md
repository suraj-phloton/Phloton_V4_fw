# ESP32 and STM32 firmware comparison — 13 November 2026

The STM32 now implements all six features authorized on 12 November: five temperatures, hot-sink cutoff/recovery, lid inhibition/resume, buzzer events, persistent battery calibration, and voltage-derived SOC LED bars. These features have specific behavior differences from the ESP32 application. Cellular/cloud, GNSS, Wi-Fi, OTA, fan-speed/feedback functions, and several application policies remain absent or partial.

This is a source comparison, not a new hardware test or a request to implement every difference. In particular, voltage-only SOC is the requested current policy; omitting coulomb counting and the OCV load pause is intentional. No firmware, settings, or existing test results were changed for this comparison.

## Sources and scope

| Source | Identity |
| --- | --- |
| Full ESP32 application | `C:/Suraj J R Folder/Other consultancy projects/Enhanced Innovations Pvt. Ltd/Design/Firmware/INTEGARATION_P6_V15_U13/INTEGARATION_P6_V15_U13.ino`, 2,873 lines |
| ESP32 SHA-256 | `5d7d1119b5e887e0a25c3feda719af7dd1d80e503b60a3ae54fb81572278ed34` |
| STM32 source revision | `6e450c242aa38d3c708fea2e2abc41b05a9cf8fc` |
| STM32 build ID | `PHV4-SIX-FEATURES-20261112-A` |
| Main application | [Core/Src/main.c](../Core/Src/main.c), SHA-256 `e0aba0c2b6c825e5c2c13bad2397b7351d619b2b017468f91e1ccd37b8d11edc` |
| Feature logic | [phloton_features.c](../Core/Src/phloton_features.c), [phloton_features.h](../Core/Inc/phloton_features.h) |
| Application integration | [phloton_feature_app.inc](../Core/Src/phloton_feature_app.inc) |
| Persistent calibration | [phloton_nvs.c](../Core/Src/phloton_nvs.c), [phloton_nvs_stm32.c](../Core/Src/phloton_nvs_stm32.c), and both linker scripts |
| Prior comparison | [CODEX_CHAT_ARCHIVE.md, CH-009](../CODEX_CHAT_ARCHIVE.md#ch-009-full-esp32-application-feature-comparison); historical comparison predates the six-feature implementation |

The full ESP32 product application above is separate from the small UART-to-USB forwarding sketch supplied in chat. The forwarding sketch alone does not supply the product application's modem, web, GNSS, OTA, or device-state functions. A separate ESP32 factory calibration program was described by the user but was not provided; its contents are not assumed here. Identity/credential values are intentionally not reproduced.

References below use `E` for ESP32 sketch lines, `M` for STM32 main.c, `F` for phloton_features.c, and `A` for phloton_feature_app.inc. Line numbers refer to the source identities above. Function bodies and active call paths were checked; names, comments, declarations and commented-out code alone were not counted as working features.

## Feature matrix

Numbers 01–32 correspond to the earlier CH-009 comparison so existing references remain useful.

| ID | Feature | ESP32 implementation | Current STM32 implementation / difference | Status and evidence |
| --- | --- | --- | --- | --- |
| 01 | RTOS and ownership | Separate pinned tasks for sensing, SOC and SD/cloud, with a data mutex. | Cooperative foreground loop plus 1 ms interrupt; no FreeRTOS scheduler, queues or task allocation. SD/UART work can delay foreground acquisition/control. | **Missing RTOS.** E:1325; M:3256. |
| 02 | Watchdog | Task-watchdog initialization and refresh calls exist. Configuration and coverage still require qualification. | No active IWDG/WWDG configuration or refresh. Temperature-age supervision exists in SysTick, but is not an independent watchdog. | **Missing watchdog.** E:1317, 2568; M:1156; A:33. |
| 03 | Five temperatures | PCB, cold, hot, top, bottom; one ADC reading per sensor per Temperature_reading() call. | All five measured and logged; 16 readings per sensor about every 100 ms. Different conversion reference and validity rules, detailed below. | **Implemented, different acquisition.** E:1509; A:97; F:5. |
| 04 | Fault reporting | Per-sensor flags and NTC/TEC/HS-fan/CS-fan bitmask. Setting a bit does not itself guarantee shutdown. | Five-sensor validity mask, first numeric fault latch, ADC/time/communication diagnostics; no equivalent combined component bitmask or active fan-failure diagnostics. | **Partial / different diagnostics.** E:73, 1519, 1665; M:1216; A:178. |
| 05 | Temperature used for cooling | PI_ALGO() uses Flask_avg_temp, which actually equals the top sensor because the average adds top to itself. | Cold-sink sensor feeds a genuine PI controller with 3 C setpoint. Flask top/bottom are monitored and logged, not used as the regulation variable. | **Different control policy.** E:1529, 1555; M:1110, 1236, 1315. |
| 06 | Normal cold-temperature coast | Zero TEC PWM at <=2 C, restart region >=2.5 C, proportional PWM mapping up to 4.5 C. | Active PI requests 0.5–7 A; no equivalent normal cold-temperature zero-current coast/restart band. Interlocks and OFF can still shut the converter down. | **Missing equivalent coast policy.** E:1558; M:1116, 1236. |
| 07 | Hot-sink cutoff | TEC PWM zero at >=60 C; inhibition retained until <=50 C. Fan duty still follows other ESP control logic. | Same 60/50 C thresholds; converter reset asserted, PI/startup state reset, fans held ON for valid hot inhibition, full converter startup rerun on recovery. | **Implemented, different output/restart behavior.** E:1594; F:39; A:64, 80. |
| 08 | Independent fan PWM | Separate 5 kHz fan channels, with temperature-dependent HS-fan duty and separately assigned CS-fan duty. | Both fans commanded together as GPIO ON/OFF. No independent speed command, PWM fan control, or equivalent cold-coast fan policy. | **Partial: fan switching only.** E:1273, 1561; M:1356. |
| 09 | Fan current sensing and filtering | HS/CS fan currents measured; 100-reading averaging and alpha=0.1 EMA for fan/TEC currents on sampleNow. | FAN_ISNS_ENABLE=0; actual fan readings disabled and CSV uses NA. Old placeholder pins conflict with temperature inputs and enabling the flag is explicitly rejected. TEC current is measured, without equivalent current EMA. | **Missing active fan feedback.** E:1704, 1714; M:86, 932, 944, 3150. |
| 10 | Fan/TEC open-load diagnostics | Routines compare current-sense voltage against 0.03 V, but normal duty values do not satisfy their ==4095 call guards. | No equivalent fan-stall/open-load or TEC disconnected-load detection. Converter I2C/readback checks are a different diagnostic. | **Missing; ESP path defective too.** E:1613, 1632, 1652, 1665; M:1288. |
| 11 | Lid interlock and resume | LOW=open; checked in the foreground state machine. Lid state disables TEC and both fans; HIGH resumes discharge processing. | Default HIGH=open, selectable profile; SysTick asserts converter reset on an open input. Closing qualifies for 30 ms; restart requires fresh valid sensors, hot recovery, retained ON request and no latched fault. | **Implemented, different polarity/timing/fan policy.** E:2477, 2584, 2593; A:33, 57, 149. |
| 12 | Buzzer | 5 kHz, about 50%; 200 ms ON/100 OFF/400 ON wake/lid pattern and 500 ms sleep. Lid alarm begins after 20 s plus a loop-count condition. | Same carrier and basic envelopes; interrupt-serviced without sound delays. Lid alarm after 20 s, repeat every 10 s; cancellation on close/OFF. Wi-Fi/OTA-specific sounds are absent with those modes. | **Implemented, deterministic timing differs.** E:1461, 1679; F:70, 86; A:12, 33. |
| 13 | SOC calculation | Voltage initial estimate plus current integration for 35 Ah capacity; 8.5/12.6 V clamps and periodic OCV blending. | Requested voltage-only formula: clamp(10+25*(V-9),0,100). 9 V=10%, 12.6 V=100%; no battery-capacity/current integration. | **Implemented under intentional new policy.** E:987, 1052; F:128. |
| 14 | Battery presence/reconnection | Presence requires >9 V; absence skips SOC integration and reconnection reseeds SOC from voltage. | No separate presence/reconnection state. SOC follows each completed calibrated voltage batch; indicator suppresses invalid/stale batches. This does not inhibit cooling for battery removal or low voltage. | **No equivalent presence state.** E:846, 1001; A:165, 192. |
| 15 | OCV correction/load pause | Every 60 s requests TEC pause, waits 5 s and blends 20% voltage-derived SOC; charger condition is commented out. Mutex remains held during the wait. | No periodic OCV pause or blend, consistent with requested voltage-only SOC. | **Intentionally omitted.** E:1019; F:128; A:149. |
| 16 | Persistent battery calibration | Preferences namespaces isnscal and batcal load current/voltage slope/offset. Missing calibration makes the corresponding measured value zero. The shared app only loads; no coefficient-writing utility is included. | Four coefficients in two reserved flash pages with version, sequence, CRC and final commit. Boot loads a valid copy or nominal defaults. Debugger save request available while cooling is OFF; no dedicated production utility. | **Implemented, different format/fallback/provisioning.** E:826, 851; A:125; phloton_nvs.c:32,47. |
| 17 | SOC shift-register indication | SOC bands choose repeated 0x00/0x01 byte transmissions with 50 ms delays; update scheduling uses loop counts. No cumulative multi-bit masks are emitted. | Explicit five-LED cumulative masks 01/03/07/0F/1F; updated on change using voltage bands. Dark OFF/fault/invalid/stale. Exact bands differ from ESP's >=20/40/60/80 conditions. | **Implemented, materially different pattern generation.** E:1745–1813; F:140; A:192. |
| 18 | RGB temperature indication | Top-derived temperature: green <=7.5 C, red >=8 C, retain prior color between. Orange lid-open; blue Wi-Fi entry indication. | Filtered cold-sink temperature: green <=8 C, red above; no 7.5/8 hysteresis, no lid-orange or Wi-Fi-blue policy. Pauses/faults do not explicitly replace the previous RGB color. | **Partial / different indication.** E:1532, 2495, 1161; M:128, 1568; A:64. |
| 19 | Button gestures | Release classifies <3 s short press, 3–9 s Wi-Fi request when idle/inactive, >=9 s branch; separate 10 s OTA handler is not called. | Debounced press toggles ON/OFF after 30 ms; no duration classification or Wi-Fi/OTA gesture. A long hold still begins as an ordinary ON/OFF press. | **Partial: ON/OFF only.** E:1216, 2330; M:1641. |
| 20 | Application modes and boot | INIT/discharge/lid/idle/fault/Wi-Fi/FOTA cases; boot proceeds toward discharge with deviceActive=true. Several cases/transitions are incomplete. | Boots OFF. Converter startup/RUN plus recoverable hot/lid pauses and latched faults; no charging, Wi-Fi or FOTA application mode. Button handling runs globally in the foreground. | **Partial / different state model.** E:257, 1331, 2500; M:232, 1592, 3265. |
| 21 | EC200U integration | UART AT helpers, modem power/reset setup, cellular/HTTP configuration and certificate-upload helper. | USART2 currently sends CSV to the ESP32; no modem AT request/response implementation or modem lifecycle. | **Missing.** E:327, 762, 1258, 1308; M:3074. |
| 22 | Network supervision | AT+CSQ signal check and network_stage select upload/defer paths. | No signal-strength, network-stage, modem retry or upload schedule. | **Missing.** E:665, 710; M:3189–3317. |
| 23 | Cloud/identity/heartbeat | JSON telemetry and device-status posts; UNIT_ID supplied in the sketch and used by the cloud/UI. | Local CSV/UART only; no product unit-ID field, authentication/configuration record, cloud encoder or heartbeat. Build ID is a firmware revision, not a per-unit ID. | **Missing.** E:83, 437, 654; M:3138; A:2. |
| 24 | SD upload backlog | Reads stored rows in batches and saves per-file progress cursors, with retry helpers. Progress is not reliably tied to acknowledged delivery. | Local append logging only. No pending-record queue, upload acknowledgments, stored cursor or resend policy. | **Missing.** E:476, 498, 600, 610; M:2943, 3138. |
| 25 | GNSS/location | EC200U fix queries; retry 5 s without fix, update 60 s with fix while GPS_flag permits; optional CSV/cloud coordinates. | No GNSS driver, fix state, coordinates or location telemetry. | **Missing.** E:1817, 1901; M:3138. |
| 26 | Real date/time | Modem clock parsing and settimeofday; epoch time in logs/cloud. | Milliseconds since boot; no active RTC/calendar synchronization or epoch timestamps. | **Missing calendar time.** E:406, 1409, 2751; M:1156, 3138. |
| 27 | Telemetry content/protocol | Normal CSV has 20 fields plus optional two GPS fields: component fault mask, SOC, five temperatures, flask average, load states and PWM duties. | Fixed 22-field CSV has five temperatures, valid mask, SOC, hot/lid/request state, numeric fault, calibration status and sample sequence. No flask average, individual fan/TEC status fields, PWM duties, GPS or unit ID. It additionally records TEC voltage and PI provisional integral. | **Partial / incompatible schemas.** E:1336, 2696; M:3050, 3138. |
| 28 | Logging modes/files | Active interval 5 s; idle interval 10 s; two files selected through network/charger-related flags. | One TEC_V2.CSV plus UART, every 1 s in ON/OFF/pause states. Custom FAT32/SPI implementation rather than Arduino SD. No equivalent split-file or idle-rate policy. | **Implemented logging, different policy.** E:190, 2689, 2820; M:2925, 3008, 3307. |
| 29 | Wi-Fi AP | AP naming from UNIT_ID, five-minute timeout, idle-button entry code. Activation has a Sleep_state control-flow defect. | No Wi-Fi radio management, AP mode or timeout. This needs radio/companion support in the selected system architecture. | **Missing; ESP activation needs repair.** E:1131, 1175, 1216, 2416. |
| 30 | Browser UI/file download | Dashboard for battery/temperature/unit, HTTP streaming of discharge/charging logs, wifiDownloadActive gating for logging. | No web server, dashboard, HTTP download or download/storage arbitration. A USB serial log stream is not this feature. | **Missing.** E:1066, 1112; M:3074, 3138. |
| 31 | OTA availability check | Deployment query/parser; scheduled one-hour check when SD upload processing reaches the idle-state check. Manual trigger is disconnected. | No deployment query, update schedule, OTA state or update-status reporting. | **Missing.** E:628, 1928, 1950, 2290. |
| 32 | Firmware installation | Download to EC200U UFS, ESP32 update-partition size check, Update.write/end and ESP.restart. Updates the ESP32 itself. | No STM32 application update protocol, staging layout, bootloader/rollback or companion-controlled installation flow. ESP32 OTA code cannot update STM32 unchanged. | **Missing.** E:2080, 2118; STM32 linker/application layout. |

## Material differences in the six recently implemented features

### Temperature readings and control are not numerically interchangeable

- ESP32 uses a 3.3 V divider/reference model and t0=296.15 K (23 C). Its source comment says 4 C, which does not match that constant. STM32 uses 3.3 V ADC full scale, 3.0 V NTC excitation and t0=298.15 K (25 C). Both use 10 kOhm nominal resistance and beta=3977.
- STM32 rejects near-rail or non-finite/out-of-range readings and requires all five channels valid and no older than 250 ms for cooling. ESP32 flags NTC resistance outside 100 Ohm–1 MOhm, with sticky per-sensor flags; those flags are not equivalent to the STM32 shutdown path.
- ESP32 stops normal temperature acquisition in its lid-open case. STM32 continues acquisition in OFF/pause states, which allows hot recovery to remain observable.
- STM32 PI constants remain Kp=-0.21, Ki=-0.0003, Ts=1 s, EMA alpha=0.25, setpoint=3 C, requested current 0.5–7 A in 0.05 A steps. The ESP32 function named PI_ALGO is a piecewise PWM/coast controller, not the same PI equation. The STM32 comment claiming the same PI mathematics as ESP32 does not describe this supplied ESP32 sketch.

Consequently, equal displayed temperatures, regulation behavior or load current cannot be assumed from feature-name parity. The board sensor model, selected control temperature and physical power architecture must be considered separately.

### Hot/lid shutdown and fan behavior

The 60/50 C hot thresholds match. STM32 interrupts assert converter reset; the foreground later processes the pause and restarts the complete verified LM51772 sequence (20/20/50/10 ms waits plus transactions). ESP32 directly suppresses TEC PWM and later resumes its duty-control path.

Lid polarity is deliberately different: ESP32 LOW=open, STM32 default HIGH=open. The STM32 profile is selectable and both software profiles were tested, but assembly polarity still needs measurement. The 1 ms interrupt is the software service interval, not a measured worst-case electrical cutoff guarantee.

ESP32's lid case turns both fans OFF. STM32 normally stops fans for a lid-only pause, but retains both ON if a valid hot-sink reading is inhibited. With the user OFF, valid hot inhibition can still keep the fans ON. If the hot channel itself is invalid, the current STM32 pause condition does not force fans ON; that fault-state fan policy needs explicit board qualification.

### Buzzer timing

The basic sound envelopes match. ESP32 uses blocking delay calls and, after the 20-second lid delay, counts 14 calls before each warning. STM32 uses tick-driven envelopes, a fixed 10-second repeat, and cancels lid warnings on closure/OFF. These are intentional timing improvements rather than an exact reproduction of the ESP32 loop-count cadence.

### Calibration, unit ID and factory programming

Both applications use physical_value = slope * averaged_raw_count + offset for battery voltage/current. Coefficients must be measured on the relevant assembly; matching equations do not make ADC coefficients transferable between boards.

The shared ESP32 app reads two Preferences namespaces; it does not contain the earlier factory writing utility. Its UNIT_ID is initialized in source, not loaded from Preferences in this sketch. The STM32 stores only the four calibration floats; per-unit ID and factory metadata are absent. Its build ID does not identify a manufactured unit.

The STM32 save path is a debugger mailbox, accepted with user request OFF, startup idle and converter reset LOW. The two 2 KiB pages are at 0x0807F000 and 0x0807F800; mass erase removes them. A separate provisioning image/tool including unit ID remains to be implemented if that manufacturing workflow is required.

### Voltage-only SOC and LED bars

The STM32 policy is the user's requested change from the ESP32 estimate. It does not perform 35 Ah coulomb counting, presence-state reseeding, or five-second OCV relaxation pauses.

| Calibrated voltage | STM32 LED mask | Lit LEDs |
| --- | --- | ---: |
| <=8.60 V, invalid/stale, OFF or fault | 0x00 | 0 |
| >8.60 to 9.40 V | 0x01 | 1 |
| >9.40 to 10.20 V | 0x03 | 2 |
| >10.20 to 11.00 V | 0x07 | 3 |
| >11.00 to 11.80 V | 0x0F | 4 |
| >11.80 V | 0x1F | 5 |

SOC itself is 10% at 9 V, 100% at 12.6 V, and clamps to zero at <=8.6 V. The bars use upper-inclusive 20% bands; the ESP32 selects bands at >=20/40/60/80%. At an exact 20% boundary, for example, STM32 retains one bar while ESP32 chooses its next onCount branch. In the ESP32 code each shiftOut sends a whole 0x00 or 0x01 byte; all SOC bands end with 0x01, so it does not explicitly produce a cumulative five-bit bar. Hardware LED wiring still needs verification.

The STM32 clears stale bars after 1000 ms, but does not shut cooling down because SOC/voltage is low. A zero or dark indication is not an implemented battery undervoltage cutoff.

## ESP32 paths that are not a verified reference implementation

| ID | Finding | Source evidence / implication |
| --- | --- | --- |
| E-I01 | Flask average uses top twice. | E:1529; regulation and RGB use top alone, not a top/bottom average. |
| E-I02 | Low-SOC shutdown is disabled. | SOC_CHECK_F() returns 0 (E:2461). LOW_SOC_F exists, but normal state transitions do not activate its intended low-SOC policy. |
| E-I03 | Battery overcurrent/undervoltage enforcement absent. | OVER_CURRENT_A=8 and UNDER_VOLTAGE_V=8.9 are declarations without active comparisons; related flags do not drive shutdown. STM32 also lacks equivalent battery-level enforcement. |
| E-I04 | Charger state is unfinished. | CHARGER_DOCK setup exists; relevant read is commented out. STATE_CHARGING is empty; Charger_Connected is set in LOW_SOC_F, whose normal entry is disabled. Separate charging filenames do not establish working charger detection. |
| E-I05 | Manual OTA gesture is disconnected. | OTA_Initiate_Process() has a definition but no call site. The >=9 s release branch only clears a button flag. Scheduled idle OTA checks are a separate active path. |
| E-I06 | Wi-Fi entry can remain in the idle loop. | Sleep_state() at E:2424 delays/continues when STATE_WIFI_AP is selected; while deviceActive remains false, it does not return to loop() to run the AP handler. |
| E-I07 | Open-load guards do not match ordinary duty commands. | LOAD_FAULT_CHECK() requires exactly 4095; PI_ALGO ordinarily assigns/caps at 4090 (and initialization uses other values). Existence of the diagnostic functions does not prove execution. |
| E-I08 | OCV pause holds the sensing mutex. | TaskSOC holds dataMutex across the five-second wait (E:999,1027), blocking TaskSense publication. The post-wait voltage may be stale. Charger gating is commented out. |
| E-I09 | Upload cursor is not acknowledgment-safe. | read_send_data advances/saves progress before a robust delivery acknowledgment; sendJsonData returns void. Tail batches below five rows are not visibly flushed after the loop. Data delivery is not guaranteed by this code. |
| E-I10 | Modem low-power calls are inactive. | enterPSMMode/wakeUpFromPSM exist, but operating-path calls are commented out. Neither application implements MCU deep sleep in these active paths. |
| E-I11 | Fault state is not wired to the component flags. | NTC/load routines set FAULT_CODE bits; no active assignment to STATE_FAULT was found. The case alone does not establish a component-fault shutdown policy. |
| E-I12 | SD mount failure returns before later initialization. | E:1297 returns from setup before modem setup, watchdog setup and task creation. This is a different failure path from STM32, which records SD failure and continues control/UART initialization. |
| E-I13 | LED bytes do not encode cumulative bars. | E:1781/1786 send only 0x01/0x00; onCount changes repeated transmissions, not final byte content. |

These are static findings, not newly reproduced ESP32 runtime failures. Neither ESP32 compilation nor physical operation was executed for this comparison.

## Other current STM32 limits relevant to integration

- **Runtime converter fault supervision:** LM51772_Readback samples nFAULT/status during startup; no equivalent continuous runtime nFAULT/status-to-shutdown check was found. Verified ILIM transfers during RUN are not full converter-fault supervision. Evidence: M:850,879,1288,1617.
- **CPU exception shutdown:** general HardFault/MemManage/BusFault/UsageFault handlers loop without explicitly asserting converter reset. NVS ECC has a narrow NMI recovery path. No active hardware watchdog covers a general CPU fault. Evidence: [stm32l4xx_it.c](../Core/Src/stm32l4xx_it.c).
- **No two-way companion protocol:** USART2 enables TE/UE, not a receive/parser path. The small ESP32 sketch only forwards STM32 bytes to USB. Neither full cloud integration nor PC-driven calibration/test commands are provided by that forwarding loop. Evidence: M:3074; archived bridge CH-008.
- **CSV parser compatibility:** STM32 fixed 22 fields and ESP32's 20-plus-optional-GPS fields have different orders and meanings. Matching field counts in a GPS-equipped ESP row do not make the parsers interchangeable. Unit ID, wall time and cloud schema also need an explicit mapping.
- **Fault schema compatibility:** ESP32 component bits and STM32 numeric first-fault reasons are different representations. Sensor-validity bits now exist on STM32, but a combined fan/TEC/NTC fault-mask contract is absent.
- **Unavailable fan current must stay distinct from zero current:** getters currently return zero with sensing disabled, while the CSV substitutes NA. A future protocol must retain that distinction.
- **RGB does not explicitly announce hot/lid/fault pauses:** the prior cold-temperature color may remain until a later update/OFF. SOC bars and fault fields are separate indicators.
- **Filesystem and hardware acceptance:** the earlier full FAT32/RTOS/watchdog review is not completed by the six-feature patch. Existing software test passes do not establish SD power-loss behavior, real sensor accuracy, fan operation, buzzer population or emergency output latency.

## Validation status

This comparison checked the source files and active call paths at the identities listed above. It does not claim new execution or hardware results. The prior [six-feature test report](../Tests/feature_validation_2026_11_12/TEST_RESULTS.md) records 61 unique cases, 174 case executions, 227,170 assertions, and successful STM32 builds. [Physical acceptance](../Tests/feature_validation_2026_11_12/BOARD_ACCEPTANCE.md) remains NOT RUN.

Firmware behavior is unchanged by this review. The report is a new local repository document; it has not been committed or pushed as part of this comparison.
