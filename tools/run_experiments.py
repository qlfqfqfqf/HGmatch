#!/usr/bin/env python3
"""Run each query/configuration/repetition in an independent process."""
import argparse
import json
import subprocess
import time
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',required=True);p.add_argument('--repeats',type=int,default=3)
    p.add_argument('--output',required=True);p.add_argument('--bin-dir',default='build/bin')
    p.add_argument('--timeout-seconds',type=float,default=3600);p.add_argument('--watchdog-grace',type=float,default=300)
    a=p.parse_args()
    if a.repeats<1 or a.timeout_seconds<0 or a.watchdog_grace<=0:p.error('invalid repetition or time limit')
    path=Path(a.manifest).resolve();m=json.loads(path.read_text());base=path.parent
    resolve=lambda x:str((base/x).resolve())
    out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
    (out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
    for q in m['queries']:
      for mode in m.get('modes',['labeled','unlabeled']):
       if mode not in ('labeled','unlabeled'):raise ValueError('invalid mode')
       for config_no,c in enumerate(m.get('configurations',[{'gpu_id':0,'batch_capacity':65536}])):
        if any(k in c for k in ('threads','scheduler','diagnostics','seed')):raise ValueError('CPU mining configuration is no longer supported')
        for repeat in range(a.repeats):
         # Numeric directory keys cannot escape the requested output directory.
         number=len(list(out.glob('run_*/run.json')))
         folder=out/f'run_{number:06d}';folder.mkdir(exist_ok=False)
         cmd=[str(Path(a.bin_dir).resolve()/('hgmatch_gpu_'+mode)),'--data-edges',resolve(m['data_edges']),'--data-nodes',resolve(m['data_nodes']),'--query-edges',resolve(q['edges']),'--query-nodes',resolve(q['nodes']),'--data-id-base',str(m.get('data_id_base',1)),'--query-id-base',str(q.get('query_id_base',0)),'--gpu-id',str(c.get('gpu_id',0)),'--gpu-memory-mb',str(c.get('gpu_memory_mb',0)),'--batch-capacity',str(c.get('batch_capacity',65536)),'--candidate-tile-size',str(c.get('candidate_tile_size',65536)),'--timeout-seconds',str(a.timeout_seconds)]
         before=time.monotonic();watchdog=a.timeout_seconds+a.watchdog_grace if a.timeout_seconds else None
         report=None
         try:
          x=subprocess.run(cmd,capture_output=True,text=True,timeout=watchdog)
          stdout,stderr,code=x.stdout,x.stderr,x.returncode
          try:report=json.loads(stdout);status=report['status']
          except (ValueError,KeyError):status='invalid_report'
          if code<0:status='process_signal'
         except subprocess.TimeoutExpired as e:
          stdout=e.stdout or b'';stderr=e.stderr or b'';code=None;status='watchdog_killed'
          if isinstance(stdout,bytes):stdout=stdout.decode(errors='replace')
          if isinstance(stderr,bytes):stderr=stderr.decode(errors='replace')
         except OSError as e:stdout='';stderr=str(e);code=None;status='launch_error'
         (folder/'stdout.txt').write_text(stdout);(folder/'stderr.txt').write_text(stderr)
         if report is not None:(folder/'metrics.json').write_text(json.dumps(report,indent=2)+'\n')
         record={'dataset':m.get('dataset','unknown'),'query':q['name'],'group':q.get('group',q['name']),'mode':mode,'configuration':c,'backend':'cuda','timing_method':'cuda_events_single_stream','configuration_index':config_no,'repeat':repeat,'command':cmd,'exit_code':code,'status':status,'process_wall_time_s':time.monotonic()-before,'timeout_seconds':a.timeout_seconds,'watchdog_grace_s':a.watchdog_grace}
         (folder/'run.json').write_text(json.dumps(record,indent=2)+'\n')
         print(q['name'],mode,c.get('gpu_id',0),repeat,status,flush=True)
if __name__=='__main__':main()
