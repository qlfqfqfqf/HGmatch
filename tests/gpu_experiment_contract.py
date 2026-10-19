#!/usr/bin/env python3
"""End-to-end GPU experiment runner/export; not a performance benchmark."""
import json
import subprocess
import tempfile
import csv
from pathlib import Path
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    for name, text in {'dn':'1\n1\n1\n','de':'1,2\n2,3\n1,3\n','qn':'1\n1\n1\n','qe':'9\n0,1\n9\n1,2\n'}.items():
        (p/name).write_text(text)
    manifest = {'dataset':'tiny','data_edges':'de','data_nodes':'dn',
                'queries':[{'name':'path','edges':'qe','nodes':'qn'}],
                'modes':['labeled','unlabeled'],
                'configurations':[{'gpu_id':0,'batch_capacity':3,'candidate_tile_size':2}]}
    path = p/'manifest.json'; path.write_text(json.dumps(manifest))
    run = ['python3', str(root/'tools/run_experiments.py'), '--manifest', str(path),
           '--repeats','2','--bin-dir',str(root/'build/bin'),'--output',str(p/'runs')]
    subprocess.run(run, check=True, capture_output=True)
    records = list((p/'runs').glob('run_*/run.json')); assert len(records)==4
    for file in records:
        assert json.loads(file.read_text())['status']=='ok'
        report = json.loads((file.parent/'metrics.json').read_text())
        assert report['backend']=='cuda' and report['embedding_count']=='6'
    subprocess.run(['python3',str(root/'tools/summarize.py'),'--input',str(p/'runs'),
                    '--output',str(p/'summary')], check=True, capture_output=True)
    summary=json.loads((p/'summary/summary.json').read_text()); assert len(summary)==2
    assert all(s['query_completion_ratio']==1 and s['mean_candidate_generation_time_s']>=0
               and s['mean_embedding_validation_time_s']>=0 for s in summary)
    raw=list(csv.DictReader((p/'summary/runs.csv').open())); assert len(raw)==4
    manifest['configurations']=[{'threads':1}]; path.write_text(json.dumps(manifest))
    bad=subprocess.run(run, capture_output=True, text=True)
    assert bad.returncode!=0 and 'CPU mining configuration' in bad.stderr
print('GPU experiment runner, independent repetitions and time exports: PASS')
