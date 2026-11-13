"""Run real C feature tests and full GNU Arm builds; retain every run's results."""
from pathlib import Path
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from write_report import write_report

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--zig', default=str(Path(tempfile.gettempdir()) / 'phloton_host_tools/zig-windows-x86_64-0.13.0/zig.exe'))
parser.add_argument('--arm-gcc', default='C:/ST/STM32CubeIDE_1.18.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin/arm-none-eabi-gcc.exe')
parser.add_argument('--host-only', action='store_true')
args = parser.parse_args()
build = HERE / '.build'
build.mkdir(exist_ok=True)
runs = HERE / 'results'
runs.mkdir(exist_ok=True)
run = runs / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
run.mkdir()
checks = []
tool_env = os.environ.copy()
tool_env['ZIG_GLOBAL_CACHE_DIR'] = str(build / 'zig_global_cache')
tool_env['ZIG_LOCAL_CACHE_DIR'] = str(build / 'zig_local_cache')

def invoke(name, cmd, cases=False, expect_success=True):
    print(name, flush=True)
    result = subprocess.run([str(v) for v in cmd], cwd=ROOT, env=tool_env, capture_output=True, text=True, timeout=240)
    (run / (name + '.stdout.txt')).write_text(result.stdout, encoding='utf-8')
    (run / (name + '.stderr.txt')).write_text(result.stderr, encoding='utf-8')
    parsed = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')] if cases else []
    passed = (result.returncode == 0) if expect_success else (result.returncode != 0)
    checks.append(dict(name=name, status='PASS' if passed else 'FAIL', returncode=result.returncode,
                       expected_success=expect_success, cases=parsed, command=[str(v) for v in cmd]))
    if not passed:
        print(result.stderr[-5000:], flush=True)
    return passed

host_base = [args.zig, 'cc', '-target', 'x86_64-windows-gnu', '-std=c11', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-function', '-ICore/Inc', '-I' + str(HERE), '-g', '-fstack-protector-strong',
             '-fsanitize=undefined', '-fno-sanitize-recover=all']
for opt in ('O0', 'O2'):
    exe = build / ('features_' + opt + '.exe')
    if invoke('compile_features_' + opt, host_base + ['-' + opt, HERE / 'test_features.c',
                                                    ROOT / 'Core/Src/phloton_features.c', ROOT / 'Core/Src/phloton_nvs.c', '-o', exe]):
        invoke('features_' + opt, [exe], cases=True)
    platform = HERE / 'test_platform.c'
    if platform.exists():
        for lid in (0, 1):
            name = f'platform_{opt}_lid{lid}'
            exe = build / (name + '.exe')
            flags = ['-' + opt, '-DPH_LID_OPEN_LEVEL=' + str(lid), '-I' + str(HERE / 'support'),
                     '-IDrivers/CMSIS/Device/ST/STM32L4xx/Include']
            if invoke('compile_' + name, host_base + flags + [platform, ROOT / 'Core/Src/phloton_features.c',
                         ROOT / 'Core/Src/phloton_nvs.c', '-o', exe]):
                invoke(name, [exe], cases=True)

if not args.host_only:
    includes = ['Core/Inc', 'Drivers/STM32L4xx_HAL_Driver/Inc', 'Drivers/STM32L4xx_HAL_Driver/Inc/Legacy',
                'Drivers/CMSIS/Device/ST/STM32L4xx/Include', 'Drivers/CMSIS/Include']
    flags = ['-mcpu=cortex-m4', '-mthumb', '-mfpu=fpv4-sp-d16', '-mfloat-abi=hard', '-std=gnu11',
             '-DSTM32L451xx', '-DUSE_HAL_DRIVER', '-DDEBUG', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-function'] + ['-I' + p for p in includes]
    changed = [ROOT / ('Core/Src/' + p) for p in ('main.c', 'phloton_features.c', 'phloton_nvs.c',
               'phloton_nvs_stm32.c', 'stm32l4xx_it.c')]
    invoke('arm_strict_syntax', [args.arm_gcc] + flags + ['-fsyntax-only'] + changed)
    invoke('invalid_lid_profile_rejected', [args.arm_gcc] + flags + ['-DPH_LID_OPEN_LEVEL=2', '-fsyntax-only', changed[0]], expect_success=False)
    sources = list((ROOT / 'Core/Src').glob('*.c')) + list((ROOT / 'Drivers/STM32L4xx_HAL_Driver/Src').glob('*.c'))
    sources.append(ROOT / 'Core/Startup/startup_stm32l451retx.s')
    for opt in ('O0', 'Os'):
        elf = build / ('phloton_' + opt + '.elf')
        # Legacy runtime stubs are unchanged; strict syntax above covers every changed source.
        link_flags = [v for v in flags if v not in ('-Wextra', '-Werror')]
        invoke('arm_full_link_' + opt, [args.arm_gcc] + link_flags + ['-' + opt, '-g', '-ffunction-sections',
               '-fdata-sections', '--specs=nano.specs', '--specs=nosys.specs'] + sources +
               ['-T' + str(ROOT / 'STM32L451RETX_FLASH.ld'), '-Wl,-Map=' + str(build / (opt + '.map')),
                '-Wl,--gc-sections', '-Wl,--print-memory-usage', '-static', '-Wl,--start-group',
                '-lc', '-lm', '-Wl,--end-group', '-o', elf])

manifest = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in list((ROOT / 'Core/Src').glob('*')) + list((ROOT / 'Core/Inc').glob('phloton*'))
            + [ROOT / 'STM32L451RETX_FLASH.ld', ROOT / 'STM32L451RETX_RAM.ld'] if p.is_file()}
summary = dict(client_date='2026-11-12', executed_utc=datetime.now(timezone.utc).isoformat(), checks=checks,
               source_sha256=manifest, physical_board_tests='NOT RUN',
               passed=sum(c['status']=='PASS' for c in checks), failed=sum(c['status']=='FAIL' for c in checks))
(run / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
(runs / 'latest.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
write_report(run)
print('Results:', run, flush=True)
print('Checks passed:', summary['passed'], 'failed:', summary['failed'], flush=True)
sys.exit(bool(summary['failed']))
