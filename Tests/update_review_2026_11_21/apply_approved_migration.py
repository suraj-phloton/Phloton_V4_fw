"""Install the exact SWO candidate explicitly approved by the user."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
EXPECTED_PATCH = 'cab9cc1dbebc9c3ba6114eb5beedd24f98a75dc06671e91605ef462613b28f2e'
manifest = json.loads((HERE/'candidate_manifest.json').read_text(encoding='utf-8'))
assert hashlib.sha256((HERE/'SWO_MIGRATION.patch').read_bytes()).hexdigest() == EXPECTED_PATCH
prepared = {}
for name, record in manifest.items():
    target = ROOT/name
    candidate = HERE/'.build/candidate'/name
    data = candidate.read_bytes()
    assert hashlib.sha256(data).hexdigest() == record['candidate'], 'Candidate changed: '+name
    actual = hashlib.sha256(target.read_bytes()).hexdigest() if target.exists() else None
    assert actual in (record['before'], record['candidate']), 'Local source changed: '+name
    prepared[name] = data
for name, data in prepared.items():
    (ROOT/name).write_bytes(data)
    print('Installed', name, flush=True)
record = dict(client_date='2026-11-21', applied_utc=datetime.now(timezone.utc).isoformat(),
              authorization='User: Apply the tested SWO migration', patch_sha256=EXPECTED_PATCH,
              source_sha256={name: hashlib.sha256(data).hexdigest() for name,data in prepared.items()},
              normalized_source_sha256={name: hashlib.sha256(data.replace(b'\r\n',b'\n')).hexdigest()
                                        for name,data in prepared.items()},
              flashed=False, pushed=False)
(HERE/'applied_manifest.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
