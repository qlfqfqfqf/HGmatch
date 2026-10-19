#!/usr/bin/env python3
"""Write a build provenance/config stamp only when the configuration changes."""
import argparse,json,subprocess,pathlib,hashlib
p=argparse.ArgumentParser();p.add_argument('--compiler');p.add_argument('--cuda-compiler');p.add_argument('--arch');p.add_argument('--flags');p.add_argument('--output');a=p.parse_args()
v=subprocess.check_output([a.compiler,'--version'],text=True).splitlines()[0]
cv=subprocess.check_output([a.cuda_compiler,'--version'],text=True).strip() if a.cuda_compiler else ''
s='#pragma once\n#define HGMATCH_COMPILER '+json.dumps(v)+'\n#define HGMATCH_BUILD_FLAGS '+json.dumps(a.flags)+'\n'
def digest(paths):
 h=hashlib.sha256()
 for p in sorted(paths):
  h.update(p.as_posix().encode()+b'\0'+p.read_bytes()+b'\0')
 return h.hexdigest()
root=pathlib.Path('.')
source=digest([p for d in ('include','src') for p in (root/d).rglob('*') if p.is_file()]+[root/'Makefile',root/'tools/build_config.py'])
deps=digest([p for p in (root/'third_party').rglob('*') if p.is_file()])
s+='#define HGMATCH_SOURCE_SHA256 '+json.dumps(source)+'\n#define HGMATCH_DEPENDENCY_SHA256 '+json.dumps(deps)+'\n'
s+='#define HGMATCH_CUDA_COMPILER '+json.dumps(cv)+'\n#define HGMATCH_CUDA_ARCH '+json.dumps(a.arch)+'\n'
f=pathlib.Path(a.output)
if not f.exists() or f.read_text()!=s:f.write_text(s)
