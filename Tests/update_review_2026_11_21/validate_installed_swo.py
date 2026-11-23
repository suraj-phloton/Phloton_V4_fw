"""Check the approved installed migration and unchanged control/safety contracts."""
from pathlib import Path
import ast
import hashlib
import json
import re
import subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
git=['git','-c','core.longpaths=true','-c','safe.directory='+ROOT.as_posix()]
def baseline(path):
    return subprocess.check_output(git+['show','03ba17c:'+path],cwd=ROOT,text=True)
def tokens(source):
    source=source.replace('\\\n','')
    pattern=r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|\d+(?:\.\d*)?(?:[eE][+-]?\d+)?[A-Za-z0-9_]*|[^\s]'
    return [x for x in re.findall(pattern,source) if not x.startswith(('//','/*'))]
def pi_block(source):
    return source.split('#define PI_ENABLE',1)[1].split('/* END TEC_PI */',1)[0]
def csv_header(source):
    literal=source.split('static const uint8_t header[] =',1)[1].split(';',1)[0]
    return ''.join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"',literal)).strip().split(',')
failures=0
def check(case,description,conditions):
    global failures
    passed=all(conditions)
    failures+=not passed
    print(json.dumps(dict(feature='SWO contract',id=case,description=description,
                          status='PASS' if passed else 'FAIL',assertions=len(conditions))))

main=(ROOT/'Core/Src/main.c').read_text(encoding='utf-8')
app=(ROOT/'Core/Src/phloton_feature_app.inc').read_text(encoding='utf-8')
old_main=baseline('Core/Src/main.c')
record=json.loads((HERE/'applied_manifest.json').read_text(encoding='utf-8'))
check('D01','Entire PI/control block matches the previous approved firmware after ignoring formatting',[
    tokens(pi_block(main))==tokens(pi_block(old_main))])
fields=csv_header(main)
check('D02','The 32-field CSV header and TEC_PI.CSV filename are preserved',[
    fields==csv_header(old_main),len(fields)==32,len(set(fields))==32,
    main.count('"TEC_PI  CSV"')==2])
check('D03','All four installed production files match the explicitly approved candidate',[
    hashlib.sha256((ROOT/path).read_bytes().replace(b'\r\n',b'\n')).hexdigest()==expected
    for path,expected in record['normalized_source_sha256'].items()])
check('D04','SWO replaces UART output and the current build identity is correct',[
    'ESP_UART' not in main,'USART2->' not in main,'PH_UART_BYTE_HOOK' not in main,
    'ph_swo_service();' in main,
    'const char firmware_build_id[] = "PHV4-SWO-DIAGNOSTICS-20261121-A";' in app])
protected=['Core/Src/phloton_features.c','Core/Src/phloton_nvs.c','Core/Src/phloton_nvs_stm32.c',
           'Core/Src/stm32l4xx_it.c','Core/Inc/phloton_features.h','Core/Inc/phloton_nvs.h',
           'STM32L451RETX_FLASH.ld','STM32L451RETX_RAM.ld']
conditions=[(ROOT/path).read_text(encoding='utf-8')==baseline(path) for path in protected]
conditions.append(tokens(app.replace('PHV4-SWO-DIAGNOSTICS-20261121-A','PHV4-PI-DIAGNOSTICS-20261115-A'))
                  ==tokens(baseline('Core/Src/phloton_feature_app.inc')))
check('D05','Existing feature/NVS/interrupt code and both calibration reservations are unchanged',conditions)
check('D06','Saved migration patch matches the user-approved application record',[
    hashlib.sha256((HERE/'SWO_MIGRATION.patch').read_bytes()).hexdigest()==record['patch_sha256'],
    record['authorization']=='User: Apply the tested SWO migration'])
raise SystemExit(bool(failures))
