"""Download a temporary, SHA-256 verified Zig host compiler; no global install."""
import hashlib
import json
from pathlib import Path
import tempfile
import urllib.request
import zipfile

base = Path(tempfile.gettempdir()) / 'phloton_host_tools'
base.mkdir(exist_ok=True)
exe = base / 'zig-windows-x86_64-0.13.0/zig.exe'
if not exe.exists():
    print('Reading official Zig release index', flush=True)
    with urllib.request.urlopen('https://ziglang.org/download/index.json', timeout=20) as response:
        meta = json.load(response)['0.13.0']['x86_64-windows']
    print('Downloading host compiler', flush=True)
    with urllib.request.urlopen(meta['tarball'], timeout=30) as response:
        data = response.read()
    assert hashlib.sha256(data).hexdigest() == meta['shasum'], 'Compiler checksum mismatch'
    with zipfile.ZipFile(__import__('io').BytesIO(data)) as archive:
        assert all((base / n).resolve().is_relative_to(base.resolve()) for n in archive.namelist())
        archive.extractall(base)
print(exe, flush=True)
