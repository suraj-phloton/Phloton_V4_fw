# SWO / ITM setup — applied 21 November 2026

The approved migration is installed in the production sources. Build ID: **`PHV4-SWO-DIAGNOSTICS-20261121-A`**. Firmware output uses PB3 / ITM stimulus port 0. The former USART2 TX logging path is removed. The same 32-column sample is still written independently to `TEC_PI.CSV`.

## Open the VS Code project in STM32CubeIDE

1. Save your files in VS Code and open STM32CubeIDE. When it asks for a workspace, choose a separate folder for CubeIDE's settings. Both editors can use the same firmware files. End any active VS Code debug session before connecting to ST-LINK from CubeIDE.
2. In CubeIDE, use **File → Import → General → Existing Projects into Workspace**. Choose **Select root directory** and browse to the `Phloton_V4_fw` folder containing `.project` and `.cproject`.
3. Select **STM_LM_SD_LOGGING**, clear **Copy projects into workspace**, and click **Finish**. If the project is already imported, check its location under **Properties → Resource** and press **F5** to refresh it.
4. Select **Project → Build Configurations → Set Active → Debug**, then **Project → Clean** and **Build Project**. Check the build console for zero errors and compilation of `phloton_swo.c`. The new executable is `Debug/STM_LM_SD_LOGGING.elf`. Use the existing project settings without regenerating code from the `.ioc` file.

## Flash and start the ITM console

The user confirmed **R113 and C124 removed**, PB3 wired to the **SWO input of ST-LINK V2**, and hot-fan current sensing unused. This isolates the intended HSFAN_ISNS connection in the reviewed schematic. Confirm the actual unit and retain common ground, SWDIO, SWCLK and target voltage reference connections for the probe. `PH_SWO_PIN_APPROVED` defaults to 1 for this modified unit; use 0 for an unmodified board.

1. Power the board with the TEC power stage disabled and connect ST-LINK. In **Run → Debug Configurations**, select or create an **STM32 C/C++ Application** for **STM_LM_SD_LOGGING**. On **Main**, select the fresh `Debug/STM_LM_SD_LOGGING.elf`.
2. On **Debugger**, select **ST-LINK (ST-LINK GDB server)** with interface **SWD**. Enable **Serial Wire Viewer (SWV)** and enter **Core Clock = 80 MHz**. Set the SWO rate or rate limit to **2 MHz** (2,000 kHz) where shown. Under **Startup**, enable image download for this flash and set the initial breakpoint at `main`. If the unit has stored calibration, use an erase mode that preserves `0x0807F000–0x0807FFFF` and avoid full chip erase.
3. Click **Debug** to program the board and stop at `main`. Open **Window → Show View → Other → SWV → SWV ITM Data Console**. Click **Configure Trace**, verify **2 MHz SWO**, enable **ITM stimulus port 0**, click **Start Trace**, then press **F8** to resume.
4. Look for `Build: PHV4-SWO-DIAGNOSTICS-20261121-A`, then timestamped CSV rows. There is a banner/build line before the CSV. The CSV column header is in the SD file; see the [field table](../pi_investigation_2026_11_14/DIAGNOSTIC_PROPOSAL.md) for the 32 positions.
5. Save the console output before a supervised cooling test, then compare it with the SD file and bench measurements. A running trace console alone does not save a historical log.

Boot text is queued during initialization and normally drains when the main loop starts. If recording begins later, the boot banner may already be missed; subsequent 1 Hz rows should still arrive. To capture boot, arrange the debug session's reset/run and trace start while the power stage is disabled. A stalled early initialization can prevent the queued boot message from draining.

Use the current `.build/phloton_O0.elf` produced by the maintained test runner, or the newly built CubeIDE image; old tracked Debug artifacts predate this change. Nothing was flashed during software validation. Some ST-LINK clones do not expose usable SWV even with an SWO wire; confirm this probe supports the selected IDE trace mode if output is absent.

The [STM32L451 datasheet](https://www.st.com/resource/en/datasheet/stm32l451re.pdf) documents PB3 TRACESWO. The [STM32CubeIDE user guide](https://www.st.com/resource/en/user_manual/um2609-stm32cubeide-user-manual-stmicroelectronics.pdf) describes SWV and the ITM console.

## Behavior when trace is unavailable

Logging uses a 1,024-byte foreground queue. Each CSV message is accepted completely or rejected completely when space is insufficient. Each main-loop pass services at most 64 bytes and never waits for the ITM stimulus FIFO. Disconnecting/disabling trace discards queued bytes; standalone cooling and SD logging remain independent of a connected debugger. A physical disconnect can interrupt a row already being transmitted, so discard incomplete captured lines.

Watch these debugger variables when diagnosing missing output:

| Variable | Meaning |
| --- | --- |
| `ph_swo_initialized` | Pin/trace initialization completed with the approved pin setting and an 80 MHz clock. Does not prove PC reception. |
| `ph_swo_bytes_sent` | Bytes written to ITM; does not prove the PC saved them. |
| `ph_swo_bytes_dropped` | Bytes rejected or discarded because trace was unavailable or the queue was full. |
| `ph_swo_writes_dropped` | New write calls rejected. |
| `ph_swo_queue_high_water` | Largest queued byte count. |

The existing newlib `_write` calls `__io_putchar`, now routed to this same queue. Use `printf` only from foreground code. No interrupt/fault-handler/concurrent-task logging is supported. Existing newlib-nano float printf support is unchanged; the CSV formatter already handles values without `%f`.

PI gains, anti-windup, cold-sink feedback, setpoint, interlocks, ADC bounds, NVS reservation, buzzer, SOC indicators and SD format are unchanged. This transport migration does not establish a physical overcooling fix. Physical checks remain [NOT RUN](../feature_validation_2026_11_12/BOARD_ACCEPTANCE.md); installed software results are in [APPLIED_TEST_RESULTS.md](APPLIED_TEST_RESULTS.md).

## Reproduce current validation

```powershell
& 'C:/Espressif/tools/idf-python/3.11.2/python.exe' Tests/feature_validation_2026_11_12/run_tests.py --client-date 2026-11-21
```

The runner tests the installed production files, not the saved candidate copy. It keeps the original review/candidate reports intact and writes current results to `applied_results/` and `APPLIED_TEST_RESULTS.md` in this folder. The archive-inspection/preparation scripts and candidate `TEST_RESULTS.md` retain the earlier review evidence; do not use them to overwrite the installed application or reinterpret historical checks as new hardware tests.
