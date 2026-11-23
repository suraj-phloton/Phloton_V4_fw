"""Retest only the affected transport harness and preserve earlier validation."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import os
import subprocess
import sys

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
previous=HERE/'results/20261121T133457365559Z'
summary=json.loads((previous/'summary.json').read_text(encoding='utf-8'))
assert {c['name'] for c in summary['checks'] if c['status']=='FAIL'} == {'swo_O0','swo_O2'}
for label,path in [('repository_main',ROOT/'Core/Src/main.c'),('supplied_main',HERE/'.build/supplied/STM_LM_SD_LOGGING_ITM/Core/Src/main.c'),
                   ('candidate_main',HERE/'.build/candidate/Core/Src/main.c'),('candidate_swo',HERE/'.build/candidate/Core/Src/phloton_swo.c')]:
    assert hashlib.sha256(path.read_bytes()).hexdigest()==summary['source_sha256'][label],label
run=HERE/'results'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
run.mkdir()
env=os.environ.copy()
env['ZIG_GLOBAL_CACHE_DIR']=str(ROOT/'Tests/feature_validation_2026_11_12/.build/zig_global_cache')
env['ZIG_LOCAL_CACHE_DIR']=str(ROOT/'Tests/feature_validation_2026_11_12/.build/zig_local_cache')
for check in summary['checks']:
    if check['name'] not in ('compile_swo_O0','swo_O0','compile_swo_O2','swo_O2'):
        check['evidence_run']=previous.name
        continue
    print(check['name'],flush=True)
    result=subprocess.run(check['command'],cwd=ROOT,env=env,capture_output=True,text=True,timeout=180)
    (run/(check['name']+'.stdout.txt')).write_text(result.stdout,encoding='utf-8')
    (run/(check['name']+'.stderr.txt')).write_text(result.stderr,encoding='utf-8')
    check['returncode']=result.returncode
    check['cases']=[json.loads(x) for x in result.stdout.splitlines() if x.startswith('{')]
    check['status']='PASS' if result.returncode==0 else 'FAIL'
    check['evidence_run']=run.name
    if check['name'].startswith('swo_') and (not check['cases'] or any(x['status']!='PASS' for x in check['cases'])):
        check['status']='FAIL'
    if check['status']=='FAIL':print(result.stderr[-3000:],flush=True)
summary.update(executed_utc=datetime.now(timezone.utc).isoformat(),
               passed=sum(x['status']=='PASS' for x in summary['checks']),
               failed=sum(x['status']=='FAIL' for x in summary['checks']),
               previous_full_validation=previous.name,
               retest_scope='Only transport test harness changed: added its missing fake RCC MMIO mapping and a UART-register preservation assertion. Firmware bytes are unchanged.',
               transport_test_sha256=hashlib.sha256((HERE/'test_swo.c').read_bytes()).hexdigest())
(run/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
(HERE/'results/latest.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
rows=['# Update review and SWO candidate validation — 21 November 2026','',
      f"Checks: **{summary['passed']} PASS, {summary['failed']} FAIL**. Physical board tests: **NOT RUN**.",
      '', 'The candidate is staged separately and has not been applied to production sources.',
      'All firmware sources match the complete validation run. Only the corrected SWO host-test harness was rebuilt and rerun at O0/O2; earlier passing checks were retained with their original evidence links.',
      '',f'[Combined results](results/{run.name}/summary.json)',
      f'[Complete prior run](results/{previous.name}/summary.json)',
      '', '## Retained failed attempts','',
      '- `20261121T133056903291Z`: first host compilation failed because Zig could not spawn worker processes (`AccessDenied`); its raw compiler log is retained. The run was interrupted before a second compilation completed.',
      '- `20261121T133457365559Z`: 38 checks passed; two direct SWO host executions crashed because the new harness had not mapped the fake RCC register memory. This was a test setup error. Adding the missing fake MMIO allocation corrected the harness; production/candidate firmware bytes did not change.',
      '', 'The complete supplied project and SWO candidate both link from source at ARM O0/Os. Strict changed-source syntax checks pass with warnings treated as errors. Full links retain existing unused `sd_verify` / RWX LOAD-segment warnings.',
      'PI traces use the actual extracted functions with supplied temperatures, not a thermal plant model. No result verifies physical battery current or thermal stability.',
      '', '| Check / profile | Result | Case executions | Assertions | Evidence |',
      '| --- | --- | ---: | ---: | --- |']
for c in summary['checks']:
    evidence=c['evidence_run']
    link=f'[raw](results/{evidence}/summary.json)' if 'command' not in c else f'[stdout](results/{evidence}/{c["name"]}.stdout.txt)'
    rows.append(f"| {c['name']} | {c['status']} | {len(c['cases'])} | {sum(x['assertions'] for x in c['cases'])} | {link} |")
rows+=['','## Individual cases','','| Profile | Case | Description | Result | Assertions |','| --- | --- | --- | --- | ---: |']
for c in summary['checks']:
    for x in c['cases']:
        rows.append(f"| {c['name']} | {x['id']} | {x['description']} | {x['status']} | {x['assertions']} |")
(HERE/'TEST_RESULTS.md').write_text('\n'.join(rows)+'\n',encoding='utf-8')
print('PASS',summary['passed'],'FAIL',summary['failed'],flush=True)
sys.exit(bool(summary['failed']))
