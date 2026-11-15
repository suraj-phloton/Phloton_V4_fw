"""Prepare and validate a logging-only patch; never edit production sources."""
from datetime import datetime, timezone
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', default='37c9970', help='Pre-diagnostic Git revision to reproduce')
args = parser.parse_args()
git = ['git', '-c', 'safe.directory=' + ROOT.as_posix()]
def baseline_text(relative_path):
    return subprocess.check_output(git + ['show', args.baseline + ':' + relative_path], cwd=ROOT, text=True)
OLD_TESTS = ROOT / 'Tests/feature_validation_2026_11_12'
BUILD = HERE / '.build/diagnostic_candidate'
BUILD.mkdir(parents=True, exist_ok=True)
RUN = HERE / 'diagnostic_results' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
RUN.mkdir(parents=True)
ZIG = Path(tempfile.gettempdir()) / 'phloton_host_tools/zig-windows-x86_64-0.13.0/zig.exe'
ARM = Path('C:/ST/STM32CubeIDE_1.18.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin/arm-none-eabi-gcc.exe')
env = os.environ.copy()
env['ZIG_GLOBAL_CACHE_DIR'] = str(HERE / '.build/zig_global_cache')
env['ZIG_LOCAL_CACHE_DIR'] = str(HERE / '.build/zig_local_cache')


def once(source, old, new):
    assert source.count(old) == 1, 'Unexpected source layout: ' + old[:80]
    return source.replace(old, new, 1)


paths = [ROOT / 'Core/Src/main.c', ROOT / 'Core/Src/phloton_feature_app.inc']
fingerprints = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
originals = {p.name: baseline_text(p.relative_to(ROOT).as_posix()) for p in paths}
main = originals['main.c']
assert main.count('TEC_V2  CSV') == 2
main = main.replace('TEC_V2  CSV', 'TEC_PI  CSV')
main = once(main, '"cooling_requested,fault,calibration_status,temp_sequence\\r\\n";',
    '''"cooling_requested,fault,calibration_status,temp_sequence,"
            "cold_feedback_filtered_C,pi_integral_A,pi_requested_current_A,"
            "ilim_code_requested,ilim_code_readback,ilim_verified,control_cycles,"
            "startup_state,pi_sat_low,pi_sat_high\\r\\n";''')
main = once(main, '    char line[512];', '''    char pi_diag[3][16];
    LogValue(pi_diag[0], flask_bottom_filt_c);
    LogValue(pi_diag[1], pi_integral);
    LogValue(pi_diag[2], pi_requested_current_a);
    char line[512];''')
old_format = '%lu,%s,%s,%s,%s,%s,%s,%s,%s,%u,%s,%s,%s,%s,%lu,%s,%u,%u,%lu,%lu,%lu,%lu\\r\\n'
new_format = old_format.removesuffix('\\r\\n') + ',%s,%s,%s,%u,%u,%u,%lu,%u,%u,%u\\r\\n'
main = once(main, old_format, new_format)
main = once(main, '(unsigned long)calibration_status, (unsigned long)temperatures.sequence);',
    '''(unsigned long)calibration_status, (unsigned long)temperatures.sequence,
                     pi_diag[0], pi_diag[1], pi_diag[2],
                     (unsigned)ilim_code_requested, (unsigned)rb_ilim, (unsigned)ilim_readback_match,
                     (unsigned long)control_cycles, (unsigned)startup_state,
                     (unsigned)pi_sat_low, (unsigned)pi_sat_high);''')
app = once(originals['phloton_feature_app.inc'], 'PHV4-SIX-FEATURES-20261112-A', 'PHV4-PI-DIAGNOSTICS-20261115-A')
prepared = {'main.c': main, 'phloton_feature_app.inc': app}
pi_start, pi_end = '#define PI_ENABLE', '/* END TEC_PI */'
old_pi = originals['main.c'].split(pi_start, 1)[1].split(pi_end, 1)[0]
new_pi = main.split(pi_start, 1)[1].split(pi_end, 1)[0]
assert old_pi == new_pi, 'PI/control block must remain identical'
patch = ''
for path in paths:
    name = path.name
    (BUILD / name).write_text(prepared[name], encoding='utf-8')
    # Match each production file's line endings; this repository mixes CRLF and LF.
    crlf = b'\r\n' in path.read_bytes()
    for line in difflib.unified_diff(originals[name].splitlines(keepends=True),
                    prepared[name].splitlines(keepends=True),
                    fromfile='a/' + path.relative_to(ROOT).as_posix(),
                    tofile='b/' + path.relative_to(ROOT).as_posix()):
        is_body = line[:1] in (' ', '+', '-') and not line.startswith(('--- ', '+++ '))
        patch += line.replace('\n', '\r\n') if crlf and is_body else line
(HERE / 'PROPOSED_PI_DIAGNOSTICS.patch').write_bytes(patch.encode('utf-8'))

# Reuse the actual integration harness against the isolated candidate source.
platform = baseline_text('Tests/feature_validation_2026_11_12/test_platform.c')
platform = once(platform, '#include "../../Core/Src/main.c"', '#include "main.c"')
platform = once(platform, '#include "../../Core/Src/phloton_nvs_stm32.c"',
                '#include "' + (ROOT / 'Core/Src/phloton_nvs_stm32.c').as_posix() + '"')
platform = once(platform, 'UART row contains all 22 fields with five sensor values',
                'UART diagnostic row preserves original fields and appends all ten PI states')
platform = once(platform, '    EXPECT(Log_Sample(ms_ticks));unsigned commas=0;',
    '''    flask_bottom_filt_c=-4.5f;pi_integral=5.0f;pi_requested_current_a=3.3f;
    ilim_code_requested=66;rb_ilim=66;ilim_readback_match=1;control_cycles=88;
    startup_state=STARTUP_RUN;pi_sat_low=pi_sat_high=0;
    EXPECT(Log_Sample(ms_ticks));unsigned commas=0;''')
platform = once(platform, 'EXPECT(commas==21);', 'EXPECT(commas==31);')
platform = once(platform, "    EXPECT(captured_length>2&&captured[captured_length-2]=='\\r'&&captured[captured_length-1]=='\\n');finish();",
    '''    EXPECT(strstr(captured, ",-4.500,5.000,3.300,66,66,1,88,7,0,0\\r\\n")!=0);
    EXPECT(captured_length>2&&captured[captured_length-2]=='\\r'&&captured[captured_length-1]=='\\n');finish();''')
(BUILD / 'test_platform.c').write_text(platform, encoding='utf-8')
checks = []


def invoke(name, command, cases=False):
    command = [str(v) for v in command]
    print(name, flush=True)
    execution = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=180)
    (RUN / (name + '.stdout.txt')).write_text(execution.stdout, encoding='utf-8')
    (RUN / (name + '.stderr.txt')).write_text(execution.stderr, encoding='utf-8')
    checks.append(dict(name=name, command=command, returncode=execution.returncode,
                       status='PASS' if execution.returncode == 0 else 'FAIL',
                       cases=[json.loads(line) for line in execution.stdout.splitlines() if line.startswith('{')] if cases else []))
    if execution.returncode:
        print(execution.stderr[-3000:], flush=True)
    return execution.returncode == 0


installed = all(p.read_text(encoding='utf-8') == prepared[p.name] for p in paths)
patch_check = git + ['apply', '--check', '--ignore-space-change'] + (['--reverse'] if installed else [])
invoke('diagnostic_patch_installed' if installed else 'diagnostic_patch_applies',
       patch_check + [HERE / 'PROPOSED_PI_DIAGNOSTICS.patch'])
host_flags = ['cc', '-target', 'x86_64-windows-gnu', '-std=c11', '-Wall', '-Wextra', '-Werror',
              '-Wno-unused-function', '-fsanitize=undefined', '-fno-sanitize-recover=all', '-fstack-protector-strong',
              '-ICore/Inc', '-I' + str(OLD_TESTS), '-I' + str(OLD_TESTS / 'support'),
              '-IDrivers/CMSIS/Device/ST/STM32L4xx/Include']
for opt in ('O0', 'O2'):
    exe = BUILD / ('platform_' + opt + '.exe')
    if invoke('compile_diagnostic_' + opt, [ZIG] + host_flags + ['-' + opt, BUILD / 'test_platform.c',
                   ROOT / 'Core/Src/phloton_features.c', ROOT / 'Core/Src/phloton_nvs.c', '-o', exe]):
        invoke('diagnostic_' + opt, [exe], cases=True)

includes = ['Core/Inc', 'Drivers/STM32L4xx_HAL_Driver/Inc', 'Drivers/STM32L4xx_HAL_Driver/Inc/Legacy',
            'Drivers/CMSIS/Device/ST/STM32L4xx/Include', 'Drivers/CMSIS/Include']
flags = ['-mcpu=cortex-m4', '-mthumb', '-mfpu=fpv4-sp-d16', '-mfloat-abi=hard', '-std=gnu11',
         '-DSTM32L451xx', '-DUSE_HAL_DRIVER', '-DDEBUG', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function']
flags += ['-I' + path for path in includes]
invoke('diagnostic_arm_syntax', [ARM] + flags + ['-fsyntax-only', BUILD / 'main.c'])
sources = [p for p in (ROOT / 'Core/Src').glob('*.c') if p.name != 'main.c']
sources += [BUILD / 'main.c'] + list((ROOT / 'Drivers/STM32L4xx_HAL_Driver/Src').glob('*.c'))
sources += [ROOT / 'Core/Startup/startup_stm32l451retx.s']
for opt in ('O0', 'Os'):
    link_flags = [v for v in flags if v not in ('-Wextra', '-Werror')]
    invoke('diagnostic_arm_link_' + opt, [ARM] + link_flags + ['-' + opt, '-g', '-ffunction-sections',
           '-fdata-sections', '--specs=nano.specs', '--specs=nosys.specs'] + sources +
           ['-T' + str(ROOT / 'STM32L451RETX_FLASH.ld'), '-Wl,-Map=' + str(BUILD / (opt + '.map')),
            '-Wl,--gc-sections', '-Wl,--print-memory-usage', '-static', '-Wl,--start-group', '-lc', '-lm',
            '-Wl,--end-group', '-o', BUILD / ('phloton_pi_diagnostics_' + opt + '.elf')])

preserved = all(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest for name, digest in fingerprints.items())
summary = dict(client_date='2026-11-15', completed_utc=datetime.now(timezone.utc).isoformat(), checks=checks,
               baseline_ref=args.baseline, candidate_already_installed=installed,
               source_sha256=fingerprints, production_sources_unchanged=preserved, pi_block_identical=old_pi == new_pi,
               patch_sha256=hashlib.sha256((HERE / 'PROPOSED_PI_DIAGNOSTICS.patch').read_bytes()).hexdigest(),
               physical_tests='NOT RUN', applied_to_production=installed,
               passed=sum(c['status'] == 'PASS' for c in checks), failed=sum(c['status'] == 'FAIL' for c in checks))
(RUN / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
(HERE / 'diagnostic_results/latest.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print('Production sources unchanged:', preserved, 'PI block identical:', old_pi == new_pi, flush=True)
print('Checks:', summary['passed'], 'PASS,', summary['failed'], 'FAIL', flush=True)
print('Results:', RUN, flush=True)
raise SystemExit(0 if preserved and summary['failed'] == 0 else 1)
