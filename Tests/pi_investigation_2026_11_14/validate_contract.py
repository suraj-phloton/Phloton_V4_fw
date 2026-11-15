"""Validate the installed diagnostic schema and unchanged PI without hardware."""
import ast
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
main = (ROOT / 'Core/Src/main.c').read_text(encoding='utf-8')
app = (ROOT / 'Core/Src/phloton_feature_app.inc').read_text(encoding='utf-8')
git = ['git', '-c', 'safe.directory=' + ROOT.as_posix()]
baseline = subprocess.check_output(git + ['show', '37c9970:Core/Src/main.c'], cwd=ROOT, text=True)
failures = 0


def check(case_id, description, condition, assertions=1):
    global failures
    if not condition:
        failures += 1
    print(json.dumps(dict(feature='Diagnostic contract', id=case_id, description=description,
                          status='PASS' if condition else 'FAIL', assertions=assertions)))


def pi_block(source):
    return source.split('#define PI_ENABLE', 1)[1].split('/* END TEC_PI */', 1)[0]


def header(source):
    literal = source.split('static const uint8_t header[] =', 1)[1].split(';', 1)[0]
    return ''.join(ast.literal_eval(value) for value in re.findall(r'"(?:[^"\\]|\\.)*"', literal)).strip().split(',')


check('D01', 'Entire PI control block identical to the pre-diagnostic firmware', pi_block(main) == pi_block(baseline))
fields, old_fields = header(main), header(baseline)
expected = ['cold_feedback_filtered_C', 'pi_integral_A', 'pi_requested_current_A', 'ilim_code_requested',
            'ilim_code_readback', 'ilim_verified', 'control_cycles', 'startup_state', 'pi_sat_low', 'pi_sat_high']
conditions = [len(fields) == 32, len(set(fields)) == 32, fields[:22] == old_fields, fields[22:] == expected]
check('D02', 'SD header has 32 unique fields with preserved prefix and exact diagnostic suffix', all(conditions), len(conditions))
applied = subprocess.run(git + ['apply', '--reverse', '--check', '--ignore-space-change',
                         'Tests/pi_investigation_2026_11_14/PROPOSED_PI_DIAGNOSTICS.patch'],
                         cwd=ROOT, capture_output=True, text=True)
check('D03', 'Approved logging patch is installed and can be reverse-checked without writes', applied.returncode == 0)
conditions = [main.count('"TEC_PI  CSV"') == 2, '"TEC_V2  CSV"' not in main,
              'const char firmware_build_id[] = "PHV4-PI-DIAGNOSTICS-20261115-A";' in app]
check('D04', 'Diagnostic file identity and build identity match the approved change', all(conditions), len(conditions))
raise SystemExit(bool(failures))
