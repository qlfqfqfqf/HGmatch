#!/usr/bin/env python3
"""Sample connected edge queries with the paper's Table III size constraints."""
import argparse
import hashlib
import json
import random
from pathlib import Path
from oracle import read_graph
SETTINGS = {'q2': (2,5,15), 'q3': (3,10,20), 'q4': (4,10,30), 'q6': (6,15,35)}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--data-edges',required=True);p.add_argument('--data-nodes',required=True)
    p.add_argument('--data-id-base',type=int,choices=(0,1),default=1)
    p.add_argument('--settings',default='q2,q3,q4,q6');p.add_argument('--count',type=int,default=20)
    p.add_argument('--seed',type=int,default=20250330);p.add_argument('--max-attempts',type=int,default=100000)
    p.add_argument('--output',required=True);a=p.parse_args()
    if a.count<1 or a.max_attempts<1:p.error('count and max-attempts must be positive')
    settings=a.settings.split(',')
    if any(x not in SETTINGS for x in settings):p.error('unknown setting')
    edges,labels=read_graph(a.data_edges,a.data_nodes,a.data_id_base)
    rng=random.Random(a.seed);postings={}
    for i,e in enumerate(edges):
        for v in e:postings.setdefault(v,[]).append(i)
    out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
    manifest={'dataset':'sampled','data_edges':str(Path(a.data_edges).resolve()),'data_nodes':str(Path(a.data_nodes).resolve()),'data_id_base':a.data_id_base,'queries':[], 'modes':['labeled','unlabeled'],'configurations':[{'gpu_id':0,'batch_capacity':65536,'candidate_tile_size':65536}]}
    provenance={'seed':a.seed,'algorithm':'random adjacent unselected edge from connected frontier; reject final vertex count','data_sha256':{x:hashlib.sha256(Path(x).read_bytes()).hexdigest() for x in (a.data_edges,a.data_nodes)},'settings':{},'status':'ok'}
    for setting in settings:
        k,lo,hi=SETTINGS[setting];made=0;attempts=0;seen=set()
        while made<a.count and attempts<a.max_attempts:
            attempts+=1
            if not edges:break
            chosen=[rng.randrange(len(edges))];vertices=set(edges[chosen[0]])
            while len(chosen)<k:
                frontier=sorted(set(e for v in vertices for e in postings[v])-set(chosen))
                if not frontier:break
                nxt=rng.choice(frontier);chosen.append(nxt);vertices.update(edges[nxt])
            key=tuple(sorted(chosen))
            if len(chosen)!=k or not lo<=len(vertices)<=hi or key in seen:continue
            seen.add(key);ordered=sorted(vertices);remap={v:i for i,v in enumerate(ordered)}
            name=f'{setting}_{made:02d}';d=out/name;d.mkdir(exist_ok=True)
            (d/'patternEdge').write_text(''.join('0\n'+','.join(str(remap[v]) for v in edges[e])+'\n' for e in chosen))
            (d/'patternNode').write_text(''.join(str(labels[v])+'\n' for v in ordered))
            (d/'mapping.json').write_text(json.dumps({'normalized_data_edge_ids':chosen,'query_to_external_data_vertex_ids':[v+a.data_id_base for v in ordered],'query_id_base':0,'seed':a.seed},indent=2)+'\n')
            manifest['queries'].append({'name':name,'group':setting,'edges':f'{name}/patternEdge','nodes':f'{name}/patternNode','query_id_base':0})
            made+=1
        provenance['settings'][setting]={'target':a.count,'generated':made,'attempts':attempts,'edges':k,'vertex_range':[lo,hi]}
        if made<a.count:provenance['status']='sampling_failed'
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (out/'sampling.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print(json.dumps(provenance,indent=2))
    return 0 if provenance['status']=='ok' else 1
if __name__=='__main__':raise SystemExit(main())
