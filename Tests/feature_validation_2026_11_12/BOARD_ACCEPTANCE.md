# Physical board acceptance — NOT RUN

No physical STM32/ESP32 rig was connected, flashed or exercised during this software validation. Record instruments, source/build identity, measured values and pass/fail in a new dated run before qualification. Software-model passes below do not substitute for these measurements.

| ID | Feature | Test procedure / expected result | Result |
| --- | --- | --- | --- |
| HW-T01 | Five temperatures | Independently stimulate PCB/ambient, cold, hot, top and bottom; verify each reported channel and units against calibrated references. | NOT RUN |
| HW-T02 | Five temperatures | Open and short each sensor with the power stage safely current limited; verify invalid mask, code 110, reset LOW and restart lockout. | NOT RUN |
| HW-T03 | Five temperatures | Measure VDDA and 3.0 V excitation; test tolerance and operating extremes; confirm NTC model on actual fitted sensors. | NOT RUN |
| HW-T04 | Five temperatures | Inject ADC ready/conversion failure and stale foreground; measure reset cutoff rather than relying on 1 Hz CSV. | NOT RUN |
| HW-H01 | Hot limit | Sweep hot sensor across just below/at/above 60 C. Verify TEC current decay, reset level and continuing fan operation. | NOT RUN |
| HW-H02 | Hot limit | While inhibited, sweep 59/55/50.1/50/49.9 C. Resume only at <=50 C with all permissives; check known load and fresh startup. | NOT RUN |
| HW-H03 | Hot limit | Open lid or request OFF during hot cooldown. No automatic TEC restart after OFF; verify fans until cooldown. | NOT RUN |
| HW-L01 | Lid | Measure PC7 for open/closed and disconnected detector. Confirm configured polarity before enabling a load. | NOT RUN |
| HW-L02 | Lid | Open during every startup phase and normal RUN; scope PB13/output and verify cutoff with slow/absent/full SD conditions. | NOT RUN |
| HW-L03 | Lid | Bounce closure around 29/30/31 ms, reopen during recovery; verify fresh startup and no resume after fault or user OFF. | NOT RUN |
| HW-B01 | Buzzer | Verify H1/external buzzer is populated; scope PA10 5 kHz/50% carrier and 200/100/400 ms wake pattern. | NOT RUN |
| HW-B02 | Buzzer | Verify 500 ms sleep sound, 20 s lid delay, 10 s repeats, immediate cancellation on close/OFF, and no control jitter from sound. | NOT RUN |
| HW-N01 | NVS | Provision measured two-point voltage/current slopes and offsets; reset/power-cycle and verify coefficients and actual units. | NOT RUN |
| HW-N02 | NVS | Interrupt power/programming at each erase/write/commit phase; recover old or new complete record, never mixed coefficients. Verify ECC/NMI behavior. | NOT RUN |
| HW-N03 | NVS | Verify reserved pages survive the intended firmware update workflow; separately confirm mass erase results in nominal-default status. | NOT RUN |
| HW-N04 | NVS | Try save during ON/startup; no flash write. Check flash status, protection faults, clock tolerance, wear policy and both copies. | NOT RUN |
| HW-S01 | SOC LEDs | Apply measured battery voltages at/below/above 8.6, 9.0, 9.4, 10.2, 11.0, 11.8, 12.6 V; verify physical bar order/polarity. | NOT RUN |
| HW-S02 | SOC LEDs | Verify 25 samples per channel, 10 ms spacing and approximately 550 ms batches under normal/slow SD load. | NOT RUN |
| HW-S03 | SOC LEDs | Exercise noise near each threshold, disconnect/reconnect battery, invalid/stale data and OFF; compare calibrated voltage with a meter. | NOT RUN |
| HW-I01 | Integration | Compare identical 32-column ITM/SWO and TEC_PI.CSV rows, including all five temperatures, interlocks and PI diagnostics. Verify build PHV4-SWO-DIAGNOSTICS-20261121-A; no former USART2/ESP32 log output. | NOT RUN |
| HW-I02 | Integration | Clean CubeIDE build, verify build ID, flashed image, stack headroom and temperature-control response with original PI gains. | NOT RUN |
| HW-I03 | PI investigation | Save a complete diagnostic capture including raw/filtered cold-sink temperature, integral, requested/verified ILIM, control cycles and measured TEC voltage/current; record bench input and combined fan current separately. Start with TEC disabled and stop a powered test before unwanted cooling; diagnostic logging does not add a cold cutoff. | NOT RUN |
| HW-I04 | SWO wiring | On the user's modified unit, verify removal of R113/C124, PB3 continuity to ST-LINK V2 SWO, analog-source isolation and common ground. Use SWV with core 80 MHz, SWO 2 MHz, ITM port 0; capture build banner and subsequent timestamped CSV. | NOT RUN |
| HW-I05 | SWO failure handling | With cooling disabled, stop/restart trace and detach/reattach the probe. Confirm controller time still advances, no transport wait, drop counters reflect unavailable trace and fresh rows resume. Inspect incomplete lines after a disconnect rather than treating them as full records. | NOT RUN |
| HW-I06 | SWO standalone | Boot without a debugger; verify normal feature behavior and independent SD logging. Check foreground-only printf routing with trace enabled during a disabled-power-stage test. SWO capture does not qualify physical thermal response. | NOT RUN |

Known residual scope: this change does not finish the prior RTOS/watchdog/complete bridge/FAT32 review. Measure the actual cutoff path and do not infer emergency latency from the 1 Hz telemetry sample rate.
