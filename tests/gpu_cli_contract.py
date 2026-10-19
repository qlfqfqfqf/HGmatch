#!/usr/bin/env python3
"""CUDA-only execution, batching invariance, timing and failure contracts."""
import json, subprocess, tempfile, os, math
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 for name,text in {'dn':'1\n1\n1\n','de':'1,2\n2,3\n1,3\n','qn':'1\n1\n1\n','qe':'8\n0,1\n9\n1,2\n'}.items(): (p/name).write_text(text)
 for mode in ('labeled','unlabeled'):
  cmd=[str(root/'build/bin'/('hgmatch_gpu_'+mode)),'--data-nodes',str(p/'dn'),'--data-edges',str(p/'de'),'--query-nodes',str(p/'qn'),'--query-edges',str(p/'qe')]
  signatures=[]
  for capacity,tile in ((1,1),(3,2),(65536,65536)):
   dest=p/'report.json'
   x=subprocess.run(cmd+['--batch-capacity',str(capacity),'--candidate-tile-size',str(tile),'--metrics-file',str(dest)],capture_output=True,text=True,check=True)
   r=json.loads(x.stdout);assert r==json.loads(dest.read_text())
   assert r['schema_version']==4 and r['backend']=='cuda' and r['embedding_count']=='6'
   assert r['timing_method']=='cuda_events_single_stream' and r['timing_complete']
   assert r['gpu']['compute_streams']==1 and r['gpu']['actual_batch_capacity']<=capacity
   assert r['gpu']['peak_allocated_bytes']<=r['gpu']['memory_budget_bytes']
   assert all(key not in r for key in ('workers','threads','scheduler','diagnostics'))
   assert r['online_query_time_s']==r['plan_time_s']+r['matching_time_s']
   assert math.isclose(sum(d['candidate_generation_time_s'] for d in r['per_depth']),r['candidate_generation_time_s'],abs_tol=1e-12)
   assert math.isclose(sum(d['embedding_validation_time_s'] for d in r['per_depth']),r['embedding_validation_time_s'],abs_tol=1e-12)
   signatures.append([(d['candidates'],d['vertex_count_pass'],d['valid']) for d in r['per_depth']])
  assert signatures[0]==signatures[1]==signatures[2]
  env=dict(os.environ,CUDA_VISIBLE_DEVICES='')
  x=subprocess.run(cmd,capture_output=True,text=True,env=env);r=json.loads(x.stdout)
  assert x.returncode==1 and r['status']=='gpu_unavailable' and r['embedding_count'] is None
  x=subprocess.run(cmd+['--gpu-memory-mb','999999999'],capture_output=True,text=True);r=json.loads(x.stdout)
  assert x.returncode==1 and r['status']=='out_of_memory' and r['embedding_count'] is None
print('GPU CLI batching, phase timings, no CPU fallback and memory errors: PASS')
