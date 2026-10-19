#!/usr/bin/env python3
import pathlib,subprocess,tempfile,json
root=pathlib.Path(__file__).resolve().parents[1]
bin=root/'build/bin/hgmatch_gpu_unlabeled'
assert bin.exists(),'two CLI executables have not been implemented'
with tempfile.TemporaryDirectory() as tmp:
 p=pathlib.Path(tmp);(p/'dn').write_text('1\n1\n');(p/'qn').write_text('1\n');(p/'de').write_text('1\n2\n');(p/'qe').write_text('900\n0\n')
 cmd=[str(bin),'--data-edges',str(p/'de'),'--data-nodes',str(p/'dn'),'--query-edges',str(p/'qe'),'--query-nodes',str(p/'qn')]
 def run(extra=()):
  x=subprocess.run(cmd+list(extra),capture_output=True,text=True);return x,json.loads(x.stdout)
 x,r=run();assert x.returncode==0 and r['embedding_count']=='2' and r['query_id_base']==0
 x,r2=run(['--query-id-base','0']);assert r2['embedding_count']==r['embedding_count']
 assert r['per_depth']==[] and r['root_candidates']=='2'
 assert r['online_query_time_s']==r['plan_time_s']+r['matching_time_s']
 assert r['schema_version']==4
 assert r['candidate_generation_time_s']==0 and r['embedding_validation_time_s']==0
 x,r=run(['--timeout-seconds','0.000000001']);assert x.returncode==124 and r['embedding_count'] is None
 assert r['candidate_generation_time_s']==0 and r['embedding_validation_time_s']==0
 for extra in [['--threads','1'],['--scheduler','bfs'],['--diagnostics','workers'],['--seed','0'],['--batch-capacity','0'],['--candidate-tile-size','0'],['--query-format','x'],['--query-id-base','2']]:
  x,r=run(extra);assert x.returncode==2 and r['status']=='input_error'
  assert r['candidate_generation_time_s'] is None and r['embedding_validation_time_s'] is None
 (p/'qe').write_text('4\n1\n');x,r=run(['--query-id-base','1']);assert r['embedding_count']=='2'
 if pathlib.Path('/dev/full').exists():
  x,r=run(['--metrics-file','/dev/full']);assert x.returncode==1 and r['status']=='internal_error'
 (p/'qe').write_text('4\n');x,r=run();assert x.returncode==2 and 'missing' in r['message']
print('CLI defaults, input errors, singletons and timeout: PASS')
