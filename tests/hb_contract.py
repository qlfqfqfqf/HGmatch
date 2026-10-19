#!/usr/bin/env python3
"""HB attachment contract plus actual matching; no count is hardcoded before measurement."""
import argparse
import json
import subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--io',default='io');p.add_argument('--capacities',default='4096,65536');p.add_argument('--output',default='results/gpu_hb');a=p.parse_args()
root=Path(__file__).resolve().parents[1];io=Path(a.io or 'io').resolve()
counts={}
for mode, parts, card, order in [('labeled',5116,[921,10,2738,1838],[1,0,3,2]),('unlabeled',341,[4369,430,7656,7656],[1,0,2,3])]:
 for capacity in map(int,a.capacities.split(',')):
  dest=root/a.output/f'hb_{mode}_b{capacity}.json'
  cmd=[str(root/'build/bin'/('hgmatch_gpu_'+mode)),'--data-edges',str(io/'HB/hypergraph.txt'),'--data-nodes',str(io/'HB/node.txt'),'--query-edges',str(io/'pattern_HB_4/patternEdge'),'--query-nodes',str(io/'pattern_HB_4/patternNode'),'--batch-capacity',str(capacity),'--metrics-file',str(dest)]
  x=subprocess.run(cmd,capture_output=True,text=True,timeout=3660);r=json.loads(x.stdout)
  assert x.returncode==0,r
  d=r['data'];q=r['query']
  assert (d['vertices'],d['raw_edges'],d['unique_edges'],d['incidences'],d['partitions'])==(1494,60987,54933,1216701,parts)
  assert (q['vertices'],q['unique_edges'],q['incidences'])==(29,4,34)
  assert r['query_cardinalities']==card and r['matching_order']==order
  assert r['embedding_count']==('954' if mode=='labeled' else '2043072')
  expected=['53884bd3a3af6d7d315486afc034796c66721ddc43a0c310268e7b26b3340c12','673196644b7e55d5b6889346e14260fdd57ac313119a4ec48a947ea535991596','78b9ef36937497620df9c652e14cbefafafacbe120540662d739c8bb20f4424b','7087e7cbbf5d1dca0124b6c55337b1f3531557ca660b52cdb601e80376314990']
  assert [r['inputs'][k]['sha256'] for k in ('data-edges','data-nodes','query-edges','query-nodes')]==expected
  assert int(r['last_depth']['valid'])==int(r['embedding_count'])
  assert mode not in counts or counts[mode]==r['embedding_count']
  counts[mode]=r['embedding_count']
  assert r['backend']=='cuda' and r['timing_method']=='cuda_events_single_stream'
  assert r['candidate_generation_time_s']>=0 and r['embedding_validation_time_s']>=0
  print(mode,capacity,r['embedding_count'],r['matching_time_s'],r['candidate_generation_time_s'],r['embedding_validation_time_s'],flush=True)
print('HB attachment contracts and parallel count invariance: PASS',counts)
