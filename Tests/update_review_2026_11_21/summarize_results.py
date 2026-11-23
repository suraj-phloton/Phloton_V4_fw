"""Print saved validation totals and verify the approved production bytes."""
from pathlib import Path
import hashlib
import json

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
latest=HERE/'applied_results/latest.json'
if not latest.exists(): latest=HERE/'results/latest.json'
summary=json.loads(latest.read_text(encoding='utf-8'))
cases=[x for check in summary['checks'] for x in check['cases']]
print('CHECKS',summary['passed'],'PASS',summary['failed'],'FAIL')
print('CASE_EXECUTIONS',len(cases),'ASSERTIONS',sum(x['assertions'] for x in cases))
print('UNIQUE_CASES',len({(x.get('feature','PI characterization'),x['id']) for x in cases}))
manifest=json.loads((HERE/'candidate_manifest.json').read_text(encoding='utf-8'))
applied=(HERE/'applied_manifest.json').exists()
for name,record in manifest.items():
    path=ROOT/name
    original=record['candidate'] if applied else record['before']
    matches=hashlib.sha256(path.read_bytes()).hexdigest()==original if original else not path.exists()
    print('PRODUCTION_MATCHES_APPROVED' if applied else 'PRODUCTION_PRESERVED',name,matches)
    assert matches
print('PATCH_SHA256',hashlib.sha256((HERE/'SWO_MIGRATION.patch').read_bytes()).hexdigest())
print('PHYSICAL_TESTS',summary['physical_board_tests'])
