"""Prepare a reviewable migration against local sources; never apply it."""
from pathlib import Path
import difflib
import hashlib
import json
import shutil

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DEST = HERE / '.build/candidate'
for name in ('Core/Src', 'Core/Inc', 'Core/Startup'):
    shutil.copytree(ROOT/name, DEST/name, dirs_exist_ok=True)
for name in ('STM32L451RETX_FLASH.ld', 'STM32L451RETX_RAM.ld'):
    shutil.copyfile(ROOT/name, DEST/name)

source = (ROOT/'Core/Src/main.c').read_text(encoding='utf-8')
changed = source.replace('#include "phloton_nvs.h"', '#include "phloton_nvs.h"\n#include "phloton_swo.h"')
changed = changed.replace('#ifndef PH_UART_BYTE_HOOK\n#define PH_UART_BYTE_HOOK(byte) ((void)0)\n#endif\n', '')
start, end = changed.index('#define ESP_UART_BAUD'), changed.index('static int32_t LogMilli')
changed = changed[:start] + changed[end:]
changed = changed.replace('ESP_UART_Init(); /* PA2 -> ESP32 GPIO16 */', 'ph_swo_init(); /* PB3 / ITM port 0; guarded by PH_SWO_PIN_APPROVED. */')
changed = changed.replace('ESP_UART_Write(', '(void)ph_swo_write(')
changed = changed.replace('Send the same CSV row to ESP and SD', 'Queue the same CSV row for SWO and SD')
changed = changed.replace('            uint32_t now;\n', '            uint32_t now;\n            ph_swo_service();\n')
assert 'ESP_UART' not in changed and 'PH_UART' not in changed
assert changed.count('ph_swo_service();') == 1
(DEST/'Core/Src/main.c').write_bytes(changed.replace('\n','\r\n').encode())
app_path = Path('Core/Src/phloton_feature_app.inc')
app = (ROOT/app_path).read_text(encoding='utf-8')
assert app.count('PHV4-PI-DIAGNOSTICS-20261115-A') == 1
(DEST/app_path).write_text(app.replace('PHV4-PI-DIAGNOSTICS-20261115-A','PHV4-SWO-DIAGNOSTICS-20261121-A'), encoding='utf-8', newline='\n')
for name, folder in (('phloton_swo.c','Core/Src'),('phloton_swo.h','Core/Inc')):
    shutil.copyfile(HERE/'candidate'/name,DEST/folder/name)

changes = ['Core/Src/main.c', str(app_path).replace('\\','/'), 'Core/Src/phloton_swo.c','Core/Inc/phloton_swo.h']
patch = ''
fingerprints = {}
for path in changes:
    before = (ROOT/path).read_text(encoding='utf-8') if (ROOT/path).exists() else ''
    after = (DEST/path).read_text(encoding='utf-8')
    patch += ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                     fromfile='a/'+path if before else '/dev/null', tofile='b/'+path))
    fingerprints[path] = {'before': hashlib.sha256((ROOT/path).read_bytes()).hexdigest() if before else None,
                          'candidate':hashlib.sha256((DEST/path).read_bytes()).hexdigest()}
(HERE/'SWO_MIGRATION.patch').write_text(patch, encoding='utf-8', newline='\n')
(HERE/'candidate_manifest.json').write_text(json.dumps(fingerprints,indent=2)+'\n',encoding='utf-8')
print('Prepared:', DEST)
print('Production source files were not changed.')
