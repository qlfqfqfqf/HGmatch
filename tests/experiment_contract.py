#!/usr/bin/env python3
"""Controlled success/timeout/input-error aggregation and query-first denominator tests."""
import json
import csv
import subprocess
import tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);i=0
 for query,status,times in [('success','ok',[1.,2.,3.]),('slow','timeout',[3600.,3600.,3600.]),('bad','input_error',[None,None,None])]:
  for repeat,time in enumerate(times):
   folder=p/'input'/f'run_{i:06d}';folder.mkdir(parents=True);i+=1
   record={'dataset':'tiny','group':'q2','query':query,'mode':'unlabeled','repeat':repeat,'configuration':{'gpu_id':0,'batch_capacity':65536},'backend':'cuda','timing_method':'cuda_events_single_stream','status':status,'exit_code':0 if status=='ok' else 124 if status=='timeout' else 2,'timeout_seconds':3600}
   report={'status':status,'embedding_count':'2' if status=='ok' else None,'matching_time_s':time,'online_query_time_s':time,'per_depth':[],'workers':[]}
   report.update(candidate_generation_time_s=time/4 if time is not None else None,embedding_validation_time_s=time/2 if time is not None else None)
   (folder/'run.json').write_text(json.dumps(record));(folder/'metrics.json').write_text(json.dumps(report))
 subprocess.run(['python3',str(root/'tools/summarize.py'),'--input',str(p/'input'),'--output',str(p/'output')],check=True,capture_output=True)
 s=json.loads((p/'output/summary.json').read_text())[0]
 assert s['queries']==3 and s['runs']==9 and s['completed_queries']==1 and s['completed_runs']==3
 assert s['query_completion_ratio']==1/3 and s['mean_matching_time_s'] is None and s['mean_paper_capped_time_s'] is None
 assert s['mean_candidate_generation_time_s'] is None and s['mean_embedding_validation_time_s'] is None
 rows=list(csv.DictReader((p/'output/per_query.csv').open()))
 success=next(row for row in rows if row['query']=='success')
 assert float(success['mean_candidate_generation_time_s'])==0.5
 assert float(success['mean_embedding_validation_time_s'])==1.0
 assert next(row for row in rows if row['query']=='slow')['mean_candidate_generation_time_s']==''
 raw=list(csv.DictReader((p/'output/runs.csv').open()))
 assert float(raw[0]['candidate_generation_time_s'])==0.25
 # Remove input errors: paper time is mean(mean(1,2,3),3600)=1801, not averaging only successful runs.
 import shutil
 for n in range(6,9):shutil.rmtree(p/'input'/f'run_{n:06d}')
 subprocess.run(['python3',str(root/'tools/summarize.py'),'--input',str(p/'input'),'--output',str(p/'output')],check=True,capture_output=True)
 s=json.loads((p/'output/summary.json').read_text())[0];assert s['mean_paper_capped_time_s']==1801.
 # Historical reports missing algorithm times must not turn into zero means.
 folder=p/'input/run_000000';report=json.loads((folder/'metrics.json').read_text())
 report.pop('candidate_generation_time_s');report.pop('embedding_validation_time_s')
 (folder/'metrics.json').write_text(json.dumps(report))
 subprocess.run(['python3',str(root/'tools/summarize.py'),'--input',str(p/'input'),'--output',str(p/'output')],check=True,capture_output=True)
 rows=list(csv.DictReader((p/'output/per_query.csv').open()))
 success=next(row for row in rows if row['query']=='success')
 assert success['mean_candidate_generation_time_s']=='' and success['mean_embedding_validation_time_s']==''
print('experiment query-first means, failures and denominators: PASS')
