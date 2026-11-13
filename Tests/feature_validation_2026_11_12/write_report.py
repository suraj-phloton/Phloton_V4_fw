"""Render a case-by-case Markdown report from a retained test run."""
from collections import defaultdict
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent


def write_report(run):
    run = Path(run)
    data = json.loads((run / 'summary.json').read_text(encoding='utf-8'))
    grouped = defaultdict(dict)
    executions = assertions = 0
    for check in data['checks']:
        for case in check['cases']:
            executions += 1
            assertions += case['assertions']
            item = grouped[case['feature']].setdefault(case['id'], dict(case, profiles=[]))
            item['profiles'].append((check['name'], case['status'], case['assertions']))
    unique = sum(len(cases) for cases in grouped.values())
    lines = [
        '# Six-feature software test results — 12 November 2026', '',
        f"Final run: [{run.name}](results/{run.name}/summary.json). Completed UTC: `{data['executed_utc']}`.", '',
        f"**{data['passed']} driver checks PASS; {data['failed']} FAIL.** "
        f"{unique} unique executable cases, {executions} case executions and {assertions:,} assertions.", '',
        '**Physical board tests: NOT RUN.** No firmware was flashed and no STM32/ESP32 hardware loop was exercised. '
        'The 21 pending physical cases are itemized in [BOARD_ACCEPTANCE.md](BOARD_ACCEPTANCE.md).', '',
        '## Per-feature verdict', '',
        '| Feature | Unique cases | Executions | Assertions | PASS executions | FAIL executions |',
        '| --- | ---: | ---: | ---: | ---: | ---: |',
    ]
    for feature, cases in grouped.items():
        profiles = [p for case in cases.values() for p in case['profiles']]
        lines.append(f"| {feature} | {len(cases)} | {len(profiles)} | "
                     f"{sum(p[2] for p in profiles):,} | "
                     f"{sum(p[1] == 'PASS' for p in profiles)} | {sum(p[1] == 'FAIL' for p in profiles)} |")
    lines += [
        '', '## Execution profiles and scope', '',
        '- Logic cases (T/H/L/B/N/S IDs): actual production feature and NVS modules at O0 and O2.',
        '- Platform cases (P IDs): actual main/application integration and STM32 storage adapter at O0/O2, '
        'each with lid HIGH=open and LOW=open. MMIO, ADC completion and UART output are modeled on Windows.',
        '- Both host suites enable undefined-behavior sanitizer, stack protection and warnings-as-errors. '
        'They do not simulate electrical characteristics, real flash ECC, thermal dynamics or instruction timing.',
        '- Target checks compile the changed sources strictly and link the full GNU Arm image at O0/Os. '
        'Rejecting lid profile 2 is an expected negative compile check.',
        '- Exhaustive sweeps cover 4,096 ADC codes for each temperature, every single-bit corruption in a '
        '64-byte calibration record, all eight write positions with zero through seven interrupted bytes, '
        'and 20,001 battery voltages. Rollover cases cover unsigned millisecond and NVS sequence counters.',
        '', '## Every executed case', '',
        'PASS/FAIL below covers all listed profiles for that case. Physical validation remains separate.', '',
    ]
    for feature, cases in grouped.items():
        lines += [f'### {feature}', '', '| ID | Test | Profiles | Result |', '| --- | --- | ---: | --- |']
        for case_id, case in cases.items():
            verdict = 'PASS' if all(p[1] == 'PASS' for p in case['profiles']) else 'FAIL'
            description = case['description'].replace('|', '\\|')
            lines.append(f"| {case_id} | {description} | {len(case['profiles'])} | {verdict} |")
        lines.append('')
    lines += ['## Build and runner checks', '', '| Check | Result |', '| --- | --- |']
    for check in data['checks']:
        lines.append(f"| {check['name']} | {check['status']} |")
    lines += ['', 'Target linker memory reports (reserved calibration flash remains unused by the image):', '']
    for opt in ('O0', 'Os'):
        report = run / f'arm_full_link_{opt}.stdout.txt'
        if report.exists():
            lines += [f'GNU Arm {opt}:', '', '```text', report.read_text(encoding='utf-8').strip(), '```', '']
    lines += [
        'The full target links retain an unused legacy `sd_verify` buffer warning and a linker RWX LOAD-segment '
        'warning with the STM32 RAM-function layout. Strict changed-source checks have no diagnostics. '
        'See the raw stderr files for the exact messages; successful linking does not qualify the physical image.', '',
        '## Earlier failed runs and corrections', '',
        'Earlier runs are retained so failures are not erased by a successful rerun. The recorded failures were '
        'tool access or harness compilation failures; no runtime feature case failed in these retained runs.', '',
        '| Run | PASS checks | FAIL checks | Cause and correction |', '| --- | ---: | ---: | --- |',
    ]
    notes = {
        '20261112T144804942745Z': 'Zig cache access denied; moved caches into the workspace and completed compiler bootstrap.',
        '20261112T145453169032Z': 'Warnings-as-errors rejected misleading indentation in the test harness; added explicit loop braces.',
        '20261112T145856461042Z': 'Windows NEAR/LPTR macros and host pointer width conflicted with the firmware harness; '
                                  'isolated macros and used uintptr_t for MMIO pointer casts.',
        '20261112T150829635370Z': 'All host checks passed; additional edge and regression cases were then added before the final full run.',
    }
    for previous in sorted((HERE / 'results').glob('*/summary.json')):
        earlier = json.loads(previous.read_text(encoding='utf-8'))
        name = previous.parent.name
        note = notes.get(name, 'Final full run.' if previous.parent == run else 'See retained commands, case results and stderr.')
        lines.append(f"| [{name}](results/{name}/summary.json) | {earlier['passed']} | {earlier['failed']} | {note} |")
    lines += [
        '', 'Review also caught two restart races before final validation: a paused converter needed a full '
        'configuration restart, and a lid pulse during ADC publication had to survive foreground flag clearing. '
        'Integration cases P08/P10/P25 exercise the corrected behavior.', '',
        '## Remaining physical checks and limits', '',
        '- Confirm the fitted sensor models, VDDA/excitation, independent channel wiring and calibrated temperatures.',
        '- Measure lid polarity and real converter cutoff/current decay during startup, RUN and slow SD operations.',
        '- Verify H1 or an external buzzer is populated (the reviewed BOM marks H1 DNP); measure the carrier and sound envelopes.',
        '- Provision measured calibration coefficients; defaults are nominal. Test real interrupted flash erase/program, '
        'ECC/NMI recovery and the programmer update workflow. Mass erase deletes calibration.',
        '- Check LED order/polarity and the voltage thresholds on a current-limited battery substitute.',
        '- Validate the new TEC_V2.CSV 22-column schema with the physical ESP32 bridge and downstream tools. '
        'Old tracked Debug images are stale; use the current test ELF or a clean CubeIDE build.',
        '- Existing FAT32, watchdog, general CPU-fault and RTOS work from the earlier review remains outside these six changes.',
        '', 'See [README.md](README.md) for exact behavior, pin assignments, calibration provisioning and reproduction commands. '
        'The retained run summary includes production-source SHA-256 fingerprints.', '',
    ]
    (HERE / 'TEST_RESULTS.md').write_text('\n'.join(lines), encoding='utf-8')
    return unique, executions, assertions


if __name__ == '__main__':
    selected = Path(sys.argv[1]) if len(sys.argv) > 1 else max((HERE / 'results').glob('*/summary.json')).parent
    print('Report written:', write_report(selected))
