"""Build reviewed sources with trusted tools; run real C, never flash hardware."""
from pathlib import Path
from datetime import datetime, timezone
import ast
import difflib
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OLDTEST = ROOT/'Tests/feature_validation_2026_11_12'
BUILD = HERE/'.build'
SUPPLIED = BUILD/'supplied/STM_LM_SD_LOGGING_ITM'
CANDIDATE = BUILD/'candidate'
ARM = Path('C:/ST/STM32CubeIDE_1.18.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin/arm-none-eabi-gcc.exe')
ZIG = Path(tempfile.gettempdir())/'phloton_host_tools/zig-windows-x86_64-0.13.0/zig.exe'
RUN = HERE/'results'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
RUN.mkdir(parents=True)
ENV = os.environ.copy()
ENV['ZIG_GLOBAL_CACHE_DIR'] = str(OLDTEST/'.build/zig_global_cache')
ENV['ZIG_LOCAL_CACHE_DIR'] = str(OLDTEST/'.build/zig_local_cache')
checks = []

def invoke(name, command, cases=False, expected=0):
    print(name, flush=True)
    command = [str(x) for x in command]
    result = subprocess.run(command, cwd=ROOT, env=ENV, capture_output=True, text=True, timeout=240)
    (RUN/(name+'.stdout.txt')).write_text(result.stdout,encoding='utf-8')
    (RUN/(name+'.stderr.txt')).write_text(result.stderr,encoding='utf-8')
    passed = (result.returncode == 0) if expected == 0 else (result.returncode != 0)
    entry = dict(name=name,status='PASS' if passed else 'FAIL',returncode=result.returncode,
                 expected_success=expected==0,command=command,cases=[])
    if cases:
        entry['cases'] = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        if not entry['cases'] or any(c['status']!='PASS' for c in entry['cases']):
            entry['status']='FAIL';passed=False
    checks.append(entry)
    if not passed:
        print(result.stderr[-4000:],flush=True)
    return passed

def tokens(source):
    source = source.replace('\\\n','')
    pattern = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|\d+(?:\.\d*)?(?:[eE][+-]?\d+)?[A-Za-z0-9_]*|[^\s]'
    return [t for t in re.findall(pattern,source) if not t.startswith(('//','/*'))]

def function(source,name):
    found=re.search(r'^(?:static\s+)?(?:float|uint8_t|void)\s+'+name+r'\([^;]*?\)\s*\{',source,re.M)
    assert found,name
    end=source.index('{',found.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[found.start():end]

ours=(ROOT/'Core/Src/main.c').read_text(encoding='utf-8')
theirs=(SUPPLIED/'Core/Src/main.c').read_text(encoding='utf-8')
candidate=(CANDIDATE/'Core/Src/main.c').read_text(encoding='utf-8')
def contract(case_id,description,conditions):
    cases=[dict(feature='Review contract',id=case_id,description=description,
                status='PASS' if all(conditions) else 'FAIL',assertions=len(conditions))]
    checks.append(dict(name=case_id,status=cases[0]['status'],cases=cases))

contract('C01','Supplied PI differs only in local command variable name',[
    tokens(function(ours,'PI_Controller')) == tokens(re.sub(r'\btec_current_cmd\b','tec_current',function(theirs,'PI_Controller'))),
    tokens(function(ours,'PI_Reset'))==tokens(function(theirs,'PI_Reset')),
    tokens(function(ours,'CurrentToILIMCode'))==tokens(function(theirs,'CurrentToILIMCode')),
    tokens(function(ours,'LM51772_StartupStep'))==tokens(function(theirs,'LM51772_StartupStep')),
    tokens(function(ours,'TEC_ApplyIlimCode'))==tokens(function(theirs,'TEC_ApplyIlimCode'))])
contract('C02','Candidate preserves entire control block and all existing feature source bytes',[
    ours.split('#define PI_ENABLE',1)[1].split('/* END TEC_PI */',1)[0] == candidate.split('#define PI_ENABLE',1)[1].split('/* END TEC_PI */',1)[0],
    all((ROOT/'Core/Src'/p).read_bytes()==(CANDIDATE/'Core/Src'/p).read_bytes() for p in ('phloton_features.c','phloton_nvs.c','phloton_nvs_stm32.c','stm32l4xx_it.c')),
    (ROOT/'STM32L451RETX_FLASH.ld').read_bytes()==(CANDIDATE/'STM32L451RETX_FLASH.ld').read_bytes(),
    (ROOT/'STM32L451RETX_RAM.ld').read_bytes()==(CANDIDATE/'STM32L451RETX_RAM.ld').read_bytes()])
contract('C03','Candidate replaces UART calls, preserves CSV format, and services SWO in foreground',[
    'ESP_UART' not in candidate,'USART2->' not in candidate,
    function(ours,'Log_Sample').replace('ESP_UART_Write','(void)ph_swo_write').replace('Send the same CSV row to ESP and SD','Queue the same CSV row for SWO and SD')==function(candidate,'Log_Sample'),
    function(ours,'SD_LogInit')==function(candidate,'SD_LogInit'),
    '            ph_swo_service();' in candidate])
patch_check=['git','-c','core.longpaths=true','-c','safe.directory='+ROOT.as_posix(),'apply','--check','--ignore-space-change',str(HERE/'SWO_MIGRATION.patch')]
invoke('migration_patch_applies_read_only',patch_check)

host=[ZIG,'cc','-target','x86_64-windows-gnu','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function',
      '-fsanitize=undefined','-fno-sanitize-recover=all','-fstack-protector-strong','-g',
      '-I'+str(HERE/'support'),'-I'+str(OLDTEST),'-I'+str(HERE/'candidate'),'-I'+str(HERE),
      '-I'+str(CANDIDATE/'Core/Inc'),'-I'+str(ROOT/'Drivers/CMSIS/Device/ST/STM32L4xx/Include')]

config_names=('PI_KP','PI_KI','PI_TS','MIN_CURRENT','MAX_CURRENT','CURRENT_STEP','ILIM_CODE_MIN','ILIM_CODE_MAX',
              'TEC_TEMP_SETPOINT','TEMP_FILTER_ALPHA','CONTROL_PERIOD_MS','COLDSINK_TEMP_CHANNEL')
for label,source in (('repository',ours),('supplied',theirs)):
    folder=BUILD/label;folder.mkdir(exist_ok=True)
    config={n:re.search(r'^#define\s+'+n+r'\s+(\S+)',source,re.M)[1] for n in config_names}
    fragment='\n'.join('#define '+n+' '+v for n,v in config.items())+'\n'
    fragment+='\n'.join(function(source,n) for n in ('PI_Controller','CurrentToILIMCode','PI_Reset'))
    (folder/'pi_from_firmware.h').write_text(fragment,encoding='utf-8')
    for opt in ('O0','O2'):
        name=label+'_PI_'+opt;exe=folder/(name+'.exe')
        probe=(ROOT/'Tests/pi_investigation_2026_11_14/probe.c').read_text(encoding='utf-8')
        probe=probe.replace('    fclose(out);', '''    begin();
    PI_Reset();
    request = PI_Controller(TEC_TEMP_SETPOINT, 8.0f);
    CHECK(fabsf(request - 1.05f) < 0.0001f);
    trace(out, "eight_C_fresh_integral", 0, 8, 8, request);
    pi_integral = 4.0f;
    request = PI_Controller(TEC_TEMP_SETPOINT, 8.0f);
    CHECK(fabsf(request - 5.05f) < 0.0001f);
    trace(out, "eight_C_accumulated_integral", 0, 8, 8, request);
    end("PI09", "8 C request depends on integral history and has no new cutoff branch");
    fclose(out);''')
        probe_path=folder/'probe.c';probe_path.write_text(probe,encoding='utf-8')
        if invoke('compile_'+name,host+['-'+opt,'-I'+str(folder),probe_path,'-o',exe]):
            invoke(name,[exe,RUN/(name+'.csv')],cases=True)
traces=[RUN/(label+'_PI_'+opt+'.csv') for label in ('repository','supplied') for opt in ('O0','O2')]
contract('C04','All four actual C PI trace files are byte-identical',[all(p.exists() for p in traces),
         len({p.read_bytes() for p in traces if p.exists()})==1])

platform=(OLDTEST/'test_platform.c').read_text(encoding='utf-8')
platform=platform.replace('#define PH_UART_BYTE_HOOK capture_byte',
    '#define PH_SWO_READY() 1\n#define PH_SWO_BYTE_HOOK capture_byte\n#include "'+(CANDIDATE/'Core/Src/phloton_swo.c').as_posix()+'"')
platform=platform.replace('"../../Core/Src/main.c"','"'+(CANDIDATE/'Core/Src/main.c').as_posix()+'"')
platform=platform.replace('"../../Core/Src/phloton_nvs_stm32.c"','"'+(CANDIDATE/'Core/Src/phloton_nvs_stm32.c').as_posix()+'"')
platform=platform.replace('lid_pin(0);calibration_save_request=0;',
    'ph_swo_init();CoreDebug->DHCSR=1;lid_pin(0);calibration_save_request=0;')
platform=platform.replace('UART diagnostic row','SWO diagnostic row')
platform=re.sub(r'EXPECT\(Log_Sample\(([^)]*)\)\);',r'EXPECT(Log_Sample(\1));for(unsigned drain=0;drain<16;drain++)ph_swo_service();',platform)
platform_path=BUILD/'test_platform_candidate.c';platform_path.write_text(platform,encoding='utf-8')
for opt in ('O0','O2'):
    for label,test,extras in (
        ('features',OLDTEST/'test_features.c',[CANDIDATE/'Core/Src/phloton_features.c',CANDIDATE/'Core/Src/phloton_nvs.c']),
        ('swo',HERE/'test_swo.c',[])):
        name=label+'_'+opt;exe=BUILD/(name+'.exe')
        if invoke('compile_'+name,host+['-'+opt,test,*extras,'-o',exe]):invoke(name,[exe],cases=True)
    for lid in (0,1):
        name='platform_'+opt+'_lid'+str(lid);exe=BUILD/(name+'.exe')
        if invoke('compile_'+name,host+['-'+opt,'-DPH_LID_OPEN_LEVEL='+str(lid),platform_path,
                CANDIDATE/'Core/Src/phloton_features.c',CANDIDATE/'Core/Src/phloton_nvs.c','-o',exe]):
            invoke(name,[exe],cases=True)

disabled=BUILD/'test_disabled.c'
disabled.write_text('''#define main enabled_tests_main
#include "'''+(HERE/'test_swo.c').as_posix()+'''"
#undef main
int main(void){
if(VirtualAlloc((void*)0xE0000000,0x100000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)!=(void*)0xE0000000)return 2;
if(VirtualAlloc((void*)0x48000000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)!=(void*)0x48000000)return 2;
begin("SWO transport","S15","Unmodified-board build leaves PB3 and trace configuration untouched");
GPIOB->MODER=0x12345678;CoreDebug->DEMCR=0;ph_swo_init();
EXPECT(!ph_swo_initialized);EXPECT(GPIOB->MODER==0x12345678);EXPECT(!CoreDebug->DEMCR);
EXPECT(!ph_swo_write("x",1));finish();return failures?1:0;}
''',encoding='utf-8')
exe=BUILD/'swo_disabled.exe'
if invoke('compile_swo_disabled',host+['-O2','-DPH_SWO_PIN_APPROVED=0',disabled,'-o',exe]):
    invoke('swo_disabled',[exe],cases=True)

for label,project in (('supplied',SUPPLIED),('candidate',CANDIDATE)):
    driverroot=SUPPLIED if label=='supplied' else ROOT
    includes=[project/'Core/Inc',driverroot/'Drivers/STM32L4xx_HAL_Driver/Inc',driverroot/'Drivers/STM32L4xx_HAL_Driver/Inc/Legacy',
              driverroot/'Drivers/CMSIS/Device/ST/STM32L4xx/Include',driverroot/'Drivers/CMSIS/Include']
    flags=['-mcpu=cortex-m4','-mthumb','-mfpu=fpv4-sp-d16','-mfloat-abi=hard','-std=gnu11',
           '-DSTM32L451xx','-DUSE_HAL_DRIVER','-DDEBUG','-Wall','-Wextra','-Wno-unused-function']+['-I'+str(p) for p in includes]
    invoke(label+'_strict_syntax',[ARM,*flags,'-Werror','-fsyntax-only',project/'Core/Src/main.c']+
           ([project/'Core/Src/phloton_swo.c'] if label=='candidate' else []))
    sources=list((project/'Core/Src').glob('*.c'))+list((driverroot/'Drivers/STM32L4xx_HAL_Driver/Src').glob('*.c'))
    sources.append(project/'Core/Startup/startup_stm32l451retx.s')
    for opt in ('O0','Os'):
        invoke(label+'_full_link_'+opt,[ARM,*flags,'-'+opt,'-g','-ffunction-sections','-fdata-sections',
               '--specs=nano.specs','--specs=nosys.specs',*sources,'-T'+str(project/'STM32L451RETX_FLASH.ld'),
               '-Wl,-Map='+str(BUILD/(label+'_'+opt+'.map')),'-Wl,--gc-sections','-Wl,--print-memory-usage',
               '-static','-Wl,--start-group','-lc','-lm','-Wl,--end-group','-o',BUILD/(label+'_'+opt+'.elf')])
    if label=='candidate':
        invoke('invalid_swo_profile_rejected',[ARM,*flags,'-DPH_SWO_PIN_APPROVED=2','-fsyntax-only',project/'Core/Src/phloton_swo.c'],expected=1)
        invoke('unmodified_board_strict_syntax',[ARM,*flags,'-Werror','-DPH_SWO_PIN_APPROVED=0','-fsyntax-only',project/'Core/Src/phloton_swo.c'])

manifest=json.loads((HERE/'candidate_manifest.json').read_text(encoding='utf-8'))
contract('C05','Production source bytes were preserved during preparation and validation',[
    all((ROOT/p).exists()==(v['before'] is not None) for p,v in manifest.items()),
    all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==v['before'] for p,v in manifest.items() if v['before'])])
summary=dict(client_date='2026-11-21',executed_utc=datetime.now(timezone.utc).isoformat(),checks=checks,
             passed=sum(c['status']=='PASS' for c in checks),failed=sum(c['status']=='FAIL' for c in checks),
             physical_board_tests='NOT RUN',candidate_applied=False,
             source_sha256={'repository_main':hashlib.sha256((ROOT/'Core/Src/main.c').read_bytes()).hexdigest(),
                 'supplied_main':hashlib.sha256((SUPPLIED/'Core/Src/main.c').read_bytes()).hexdigest(),
                 'candidate_main':hashlib.sha256((CANDIDATE/'Core/Src/main.c').read_bytes()).hexdigest(),
                 'candidate_swo':hashlib.sha256((CANDIDATE/'Core/Src/phloton_swo.c').read_bytes()).hexdigest()})
(RUN/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
(HERE/'results/latest.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
rows=['# Update review and SWO candidate validation — 21 November 2026','',
      f"Checks: **{summary['passed']} PASS, {summary['failed']} FAIL**. Physical board tests: **NOT RUN**.",
      '','The SWO candidate is staged separately and has not been applied to production sources.',
      'The supplied project is compiled from source, not from its bundled ELF. Supplied PI replay uses its actual extracted C.',
      'The trace replay supplies temperatures; it does not model the thermal plant or verify bench current.',
      '',f'[Raw results]({RUN.relative_to(HERE).as_posix()}/summary.json)','',
      '| Check / profile | Result | Case executions | Assertions |','| --- | --- | ---: | ---: |']
for c in checks:
    rows.append(f"| {c['name']} | {c['status']} | {len(c['cases'])} | {sum(x['assertions'] for x in c['cases'])} |")
rows+=['','Each C case and its pass/fail result follows. Repeated cases are listed per optimization/lid profile.',
       '','| Profile | Case | Description | Result | Assertions |','| --- | --- | --- | --- | ---: |']
for c in checks:
    for x in c['cases']:
        rows.append(f"| {c['name']} | {x['id']} | {x['description']} | {x['status']} | {x['assertions']} |")
(HERE/'TEST_RESULTS.md').write_text('\n'.join(rows)+'\n',encoding='utf-8')
print('Results:',RUN,flush=True)
print('PASS',summary['passed'],'FAIL',summary['failed'],flush=True)
sys.exit(bool(summary['failed']))
