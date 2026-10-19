#!/usr/bin/env python3
"""Optional GPU v4 schema checks; requires jsonschema>=4."""
import json, subprocess, tempfile, copy
from pathlib import Path
import jsonschema
root=Path(__file__).resolve().parents[1]
s=json.loads((root/'metrics.schema.json').read_text(encoding='utf-8'))
validator=jsonschema.Draft202012Validator(s);validator.check_schema(s)
x=subprocess.run([str(root/'build/bin/hgmatch_gpu_unlabeled')],capture_output=True,text=True)
assert x.returncode==2;validator.validate(json.loads(x.stdout))
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 for name,text in {'dn':'1\n1\n1\n','de':'1,2\n2,3\n1,3\n','qn':'1\n1\n1\n','qe':'9\n0,1\n9\n1,2\n'}.items(): (p/name).write_text(text)
 for mode in ('labeled','unlabeled'):
  for capacity in (1,17):
   cmd=[str(root/'build/bin'/('hgmatch_gpu_'+mode)),'--data-nodes',str(p/'dn'),'--data-edges',str(p/'de'),'--query-nodes',str(p/'qn'),'--query-edges',str(p/'qe'),'--batch-capacity',str(capacity)]
   x=subprocess.run(cmd,capture_output=True,text=True);assert x.returncode==0
   report=json.loads(x.stdout);validator.validate(report)
   for section in (None,'per_depth','last_depth'):
    broken=copy.deepcopy(report)
    target=broken if section is None else broken[section] if section=='last_depth' else broken[section][0]
    target.pop('candidate_generation_time_s');assert not validator.is_valid(broken)
print('GPU v4 JSON Schema, success and input failure, required phase times: PASS')
