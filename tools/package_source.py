#!/usr/bin/env python3
"""Package the complete GPU-only Linux source project, excluding build/runtime artifacts."""
from pathlib import Path
import tarfile, hashlib
root=Path(__file__).resolve().parents[1]
out=root/'dist';out.mkdir(exist_ok=True)
archive=out/'hgmatch_gpu_source.tar.gz'
files=[]
for name in ('src','include','tests','tools','io','experiments'):
 files.extend(p for p in (root/name).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
files.extend(p for p in (root/'third_party/nlohmann').rglob('*') if p.is_file())
files.extend(p for p in (root/'docs').glob('*.md'))
files.extend(root/name for name in ('Makefile','README.md','metrics.schema.json','requirements-test.txt','third_party/DEPENDENCIES.md','third_party/SHA256SUMS'))
with tarfile.open(archive,'w:gz') as tar:
 for p in sorted(files):
  if not p.exists():raise FileNotFoundError(p)
  tar.add(p,arcname='hgmatch_gpu/'+p.relative_to(root).as_posix())
(out/'hgmatch_gpu_source.tar.gz.sha256').write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n')
print(archive)
