"""Inspect the supplied archive without running its code or build scripts."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
ARCHIVE = Path(r'C:\Users\suraj\Downloads\PI+Antiwindup_SWO.zip')
OUT = HERE / '.build' / 'supplied'
GIT = ['git', '-c', 'core.longpaths=true', '-c', 'safe.directory=' + ROOT.as_posix()]

def digest(data):
    return hashlib.sha256(data).hexdigest()

print('ANCESTOR INSTRUCTIONS', flush=True)
for directory in [ROOT, *ROOT.parents]:
    instructions = directory / 'AGENTS.md'
    if instructions.is_file():
        print(instructions, instructions.read_text(encoding='utf-8'), flush=True)
print('ARCHIVE', ARCHIVE, flush=True)
manifest = {'archive_sha256': digest(ARCHIVE.read_bytes()), 'files': []}
with zipfile.ZipFile(ARCHIVE) as archive:
    members = archive.infolist()
    print('MEMBERS', len(members), 'EXPANDED_BYTES', sum(i.file_size for i in members), flush=True)
    for info in members:
        path = PurePosixPath(info.filename.replace('\\', '/'))
        if path.is_absolute() or '..' in path.parts or ':' in str(path):
            raise ValueError('Unsafe archive path: ' + info.filename)
        if info.is_dir():
            continue
        if any(p in ('Debug', 'Release', 'Drivers', '.git', '.settings', '__MACOSX') for p in path.parts):
            continue
        if path.suffix.lower() not in ('.c', '.h', '.md', '.txt', '.ioc', '.launch', '.ld', '.pdf') and path.name not in ('.cproject', '.project'):
            continue
        data = archive.read(info)
        destination = OUT.joinpath(*path.parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        record = {'path': str(path), 'bytes': len(data), 'sha256': digest(data)}
        manifest['files'].append(record)
        print(str(path), len(data), flush=True)

current = ROOT / 'Core/Src/main.c'
head = subprocess.check_output(GIT + ['show', 'HEAD:Core/Src/main.c'], cwd=ROOT)
# Ignore formatting only, not comments, literals or tokens.
print('LOCAL_MAIN_SHA256', digest(current.read_bytes()), flush=True)
print('HEAD_MAIN_SHA256', digest(head), flush=True)
OUT.parent.mkdir(parents=True, exist_ok=True)
(HERE / 'archive_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
