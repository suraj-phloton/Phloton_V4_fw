"""Source-level comparison; do not import or execute code from the ZIP."""
from pathlib import Path, PurePosixPath
import difflib
import hashlib
import json
import re
import subprocess
import xml.etree.ElementTree as ET
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SUPPLIED = HERE / '.build/supplied/STM_LM_SD_LOGGING_ITM'
ARCHIVE = Path(r'C:\Users\suraj\Downloads\PI+Antiwindup_SWO.zip')
git = ['git', '-c', 'core.longpaths=true', '-c', 'safe.directory=' + ROOT.as_posix()]
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|\d+(?:\.\d*)?(?:[eE][+-]?\d+)?[A-Za-z0-9_]*|[^\s]')
def tokens(source):
    source = source.replace('\\\n', '')
    return [t for t in TOKEN.findall(source) if not t.startswith(('//', '/*'))]
def function(source, name):
    pattern = r'^(?:(?:static|inline|__attribute__\(\(always_inline\)\))\s+)*(?:void|float|uint\d+_t|int\d+_t|int)\s+' + re.escape(name) + r'\([^;]*?\)\s*\{'
    match = re.search(pattern, source, re.M)
    if not match:
        return None
    depth = 1
    end = source.index('{', match.start()) + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]

current = (ROOT / 'Core/Src/main.c').read_text(encoding='utf-8')
old = (SUPPLIED / 'Core/Src/main.c').read_text(encoding='utf-8')
head = subprocess.check_output(git + ['show', 'HEAD:Core/Src/main.c'], cwd=ROOT, text=True)
summary = {'local_main_tokens_equal_HEAD': tokens(current) == tokens(head), 'functions': {}, 'config': {}}
print('LOCAL MAIN TOKENS EQUAL HEAD', summary['local_main_tokens_equal_HEAD'])
names = re.findall(r'^(?:static\s+)?(?:void|float|uint\d+_t|int\d+_t|int)\s+(\w+)\([^;]*?\)\s*\{', current, re.M)
for name in dict.fromkeys(names):
    ours, theirs = function(current, name), function(old, name)
    equal = theirs is not None and tokens(ours) == tokens(theirs)
    summary['functions'][name] = 'IDENTICAL_TOKENS' if equal else ('MISSING' if theirs is None else 'DIFFERENT')
    if not equal:
        print(name, summary['functions'][name])
    if name == 'PI_Controller':
        renamed = re.sub(r'\btec_current_cmd\b', 'tec_current', theirs)
        summary['pi_equal_after_local_rename'] = tokens(ours) == tokens(renamed)
        print('PI EQUAL AFTER LOCAL VARIABLE RENAME', summary['pi_equal_after_local_rename'])
    if not equal and theirs and name in ('PI_Controller', 'LM51772_Configure', 'LM51772_StartupStep', 'LM51772_Readback', 'BATT_ReadVoltage', 'BATT_ReadCurrent', 'ADC1_Init', 'Startup_Task', 'TEC_ControlStep'):
        diff = ''.join(difflib.unified_diff(ours.splitlines(True), theirs.splitlines(True), fromfile='repository/'+name, tofile='supplied/'+name))
        (HERE / ('.build/diff_' + name + '.txt')).write_text(diff, encoding='utf-8')
for name in ('PI_KP','PI_KI','PI_TS','MIN_CURRENT','MAX_CURRENT','CURRENT_STEP','ILIM_CODE_MIN','ILIM_CODE_MAX','TEC_TEMP_SETPOINT','TEMP_FILTER_ALPHA','CONTROL_PERIOD_MS','COLDSINK_TEMP_CHANNEL','LM51772_ADDR','TARGET_VOUT_V'):
    values = [re.search(r'^#define\s+'+name+r'\s+(\S+)', s, re.M)[1] for s in (current,old)]
    summary['config'][name] = {'repository': values[0], 'supplied': values[1], 'same': values[0] == values[1]}
print('CONFIG', json.dumps(summary['config']))
netlist = Path(r'C:\Users\suraj\AppData\Local\Temp\phloton_board.xml')
if netlist.exists():
    board = ET.parse(netlist).getroot()
    for net in board.findall('./nets/net'):
        if any(n.attrib.get('pinfunction') == 'PB3' or 'PB3' in n.attrib.get('pinfunction','') for n in net.findall('node')):
            print('PB3 NET', ET.tostring(net, encoding='unicode'))
            summary['PB3_net'] = ET.tostring(net, encoding='unicode')
with zipfile.ZipFile(ARCHIVE) as archive:
    for info in archive.infolist():
        name = info.filename.replace('\\','/')
        member = PurePosixPath(name)
        if member.is_absolute() or '..' in member.parts or ':' in name:
            raise ValueError('Unsafe archive path: ' + name)
        if info.is_dir():
            continue
        if '/Drivers/' in name or '/Core/Startup/' in name:
            dest = HERE / '.build/supplied' / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(archive.read(info))
    print('ARCHIVE BUILD OUTPUTS', [i.filename for i in archive.infolist() if i.filename.endswith(('.elf','.map','.list','.bin','.hex'))])
    for path in ('Core/Src/stm32l4xx_it.c','Core/Src/stm32l4xx_hal_msp.c','Core/Src/syscalls.c','Core/Src/sysmem.c','Core/Src/system_stm32l4xx.c','STM32L451RETX_FLASH.ld','STM32L451RETX_RAM.ld'):
        a = (ROOT/path).read_text(encoding='utf-8'); b = (SUPPLIED/path).read_text(encoding='utf-8')
        print('FILE', path, 'SAME_TOKENS' if tokens(a)==tokens(b) else 'DIFFERENT')
(HERE/'source_comparison.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
