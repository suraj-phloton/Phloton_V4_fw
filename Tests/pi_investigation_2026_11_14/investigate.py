"""Extract and run actual old/current PI functions without modifying firmware."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--baseline', default='505d97f')
parser.add_argument('--zig', default=str(Path(tempfile.gettempdir()) / 'phloton_host_tools/zig-windows-x86_64-0.13.0/zig.exe'))
parser.add_argument('--netlist', default=str(Path(tempfile.gettempdir()) / 'phloton_board.xml'))
args = parser.parse_args()
git = ['git', '-c', 'safe.directory=' + ROOT.as_posix()]
baseline = subprocess.check_output(git + ['show', args.baseline + ':Core/Src/main.c'], cwd=ROOT, text=True)
current = (ROOT / 'Core/Src/main.c').read_text(encoding='utf-8')
head = subprocess.check_output(git + ['rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
config_names = ('PI_KP', 'PI_KI', 'PI_TS', 'MIN_CURRENT', 'MAX_CURRENT', 'CURRENT_STEP',
                'ILIM_CODE_MIN', 'ILIM_CODE_MAX', 'TEC_TEMP_SETPOINT', 'TEMP_FILTER_ALPHA',
                'CONTROL_PERIOD_MS', 'COLDSINK_TEMP_CHANNEL')
function_names = ('PI_Controller', 'CurrentToILIMCode', 'PI_Reset')


def extract_function(source, name):
    match = re.search(r'^(?:static\s+)?(?:float|uint8_t|void)\s+' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise ValueError('Function not found: ' + name)
    start = match.start()
    opening = source.index('{', match.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def config(source):
    return {name: re.search(r'^#define\s+' + name + r'\s+(\S+)', source, re.M)[1] for name in config_names}


old_config, new_config = config(baseline), config(current)
old_functions = {name: extract_function(baseline, name) for name in function_names}
new_functions = {name: extract_function(current, name) for name in function_names}
equality = dict(configuration=old_config == new_config,
                functions={name: old_functions[name] == new_functions[name] for name in function_names})
result_dir = HERE / 'results' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
result_dir.mkdir(parents=True)
build = HERE / '.build'
build.mkdir(exist_ok=True)
env = os.environ.copy()
env['ZIG_GLOBAL_CACHE_DIR'] = str(build / 'zig_global_cache')
env['ZIG_LOCAL_CACHE_DIR'] = str(build / 'zig_local_cache')
checks = []

for label, source in (('baseline', baseline), ('current', current)):
    fragment_dir = build / label
    fragment_dir.mkdir(exist_ok=True)
    fragment = '\n'.join('#define ' + name + ' ' + value for name, value in config(source).items())
    fragment += '\n\n' + '\n\n'.join(extract_function(source, name) for name in function_names) + '\n'
    (fragment_dir / 'pi_from_firmware.h').write_text(fragment, encoding='utf-8')
    for opt in ('O0', 'O2'):
        name = label + '_' + opt
        exe = fragment_dir / (name + '.exe')
        command = [args.zig, 'cc', '-target', 'x86_64-windows-gnu', '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=undefined', '-fno-sanitize-recover=all', '-fstack-protector-strong', '-' + opt,
                   '-I' + str(fragment_dir), str(HERE / 'probe.c'), '-o', str(exe)]
        print('Compile:', name, flush=True)
        compiled = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=180)
        (result_dir / (name + '.compile.stderr.txt')).write_text(compiled.stderr, encoding='utf-8')
        check = dict(name=name, command=command, compile_returncode=compiled.returncode, cases=[])
        if compiled.returncode == 0:
            print('Execute:', name, flush=True)
            execution = subprocess.run([str(exe), str(result_dir / (name + '.csv'))], cwd=ROOT,
                                       capture_output=True, text=True, timeout=30)
            (result_dir / (name + '.stdout.jsonl')).write_text(execution.stdout, encoding='utf-8')
            (result_dir / (name + '.stderr.txt')).write_text(execution.stderr, encoding='utf-8')
            check.update(returncode=execution.returncode,
                         cases=[json.loads(line) for line in execution.stdout.splitlines() if line.startswith('{')])
        else:
            print(compiled.stderr[-3000:], flush=True)
        checks.append(check)

trace_identical = False
if all(c.get('returncode', -1) == 0 for c in checks):
    trace_identical = len({(result_dir / (c['name'] + '.csv')).read_bytes() for c in checks}) == 1

board = {}
netlist = Path(args.netlist)
if netlist.exists():
    xml = ET.parse(netlist).getroot()
    components = {c.attrib['ref']: c for c in xml.findall('./components/comp')}
    for reference in ('U4', 'R55', 'R58', 'R57', 'JP2', 'R31', 'C38'):
        component = components[reference]
        pins = []
        for net in xml.findall('./nets/net'):
            for node in net.findall('node'):
                if node.attrib['ref'] == reference and (reference != 'U4' or node.attrib['pin'] in ('9', '14', '16', '19', '20')):
                    pins.append(dict(pin=node.attrib['pin'], function=node.attrib.get('pinfunction', ''), net=net.attrib['name']))
        board[reference] = dict(value=component.findtext('value'), pins=pins)

summary = dict(client_date='2026-11-14', executed_utc=datetime.now(timezone.utc).isoformat(),
               current_head=head, baseline=args.baseline, equality=equality, identical_traces=trace_identical,
               config=new_config, checks=checks, physical_board_tests='NOT RUN',
               source_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in (ROOT / 'Core/Src/main.c', HERE / 'probe.c', HERE / 'investigate.py')},
               schematic_netlist=board)
(result_dir / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
(HERE / 'results/latest.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print('Function/config equality:', equality, flush=True)
print('Identical old/current/O0/O2 traces:', trace_identical, flush=True)
print('Board trace:', json.dumps(board), flush=True)
print('Results:', result_dir, flush=True)
success = equality['configuration'] and all(equality['functions'].values()) and trace_identical
sys.exit(0 if success else 1)
