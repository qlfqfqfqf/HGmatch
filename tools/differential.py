#!/usr/bin/env python3
"""Compare full ordered-edge tuple sets against an independent vertex-injection oracle."""
import argparse
import itertools
import json
import random
import shutil
import subprocess
import tempfile
from pathlib import Path
from oracle import embeddings


def connected(edges):
    seen = set(edges[0])
    while True:
        nxt = seen.union(*(set(e) for e in edges if seen.intersection(e)))
        if seen == nxt:
            return all(set(e) <= seen for e in edges)
        seen = nxt


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cases', type=int, default=200)
    p.add_argument('--seed', type=int, default=20250330)
    p.add_argument('--max-data-vertices', type=int, default=8)
    p.add_argument('--max-query-vertices', type=int, default=6)
    p.add_argument('--driver', default='build/tests/gpu_driver')
    p.add_argument('--output', default='results/differential.json')
    a = p.parse_args()
    if not 3<=a.max_data_vertices<=9 or not 3<=a.max_query_vertices<=7:
        p.error("oracle bounds: data 3..9 and query 3..7")
    rng = random.Random(a.seed)
    runs = 0
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        for case in range(a.cases):
            n = rng.randint(3, a.max_data_vertices)
            universe = [e for k in range(1, min(4, n)+1) for e in itertools.combinations(range(n), k)]
            de = rng.sample(universe, rng.randint(2, min(12, len(universe))))
            dl = [rng.randrange(3) for _ in range(n)]
            while True:
                source = de if case % 2 else universe
                selected = rng.sample(source, rng.randint(1, min(4, len(source))))
                used = sorted(set().union(*map(set, selected)))
                if len(used) <= a.max_query_vertices and connected(selected):
                    break
            rename = {v:i for i,v in enumerate(used)}
            qe = [tuple(rename[v] for v in e) for e in selected]
            ql = [dl[v] for v in used]
            if case % 3 == 0:
                ql = [rng.randrange(3) for _ in used]
            (root/'de').write_text(''.join(','.join(str(v+1) for v in e)+'\n' for e in de))
            (root/'dn').write_text(''.join(str(x)+'\n' for x in dl))
            (root/'qe').write_text(''.join(str(1000+i)+'\n'+','.join(map(str,e))+'\n' for i,e in enumerate(qe)))
            (root/'qn').write_text(''.join(str(x)+'\n' for x in ql))
            for labeled in (True, False):
                if not labeled:
                    # Metamorphic guard: make data and query labels disjoint.
                    # Unlabeled results must still equal the original-label oracle.
                    (root/'dn').write_text(''.join(str(x+10000)+'\n' for x in dl))
                    (root/'qn').write_text('-999\n'*len(ql))
                expected = embeddings(de, dl, qe, ql, labeled)
                configurations = [(65536, 65536)]
                if case % 10 == 0:
                    configurations += [(1,1), (3,2), (17,65536)]
                for capacity, tile in configurations:
                    cmd = [a.driver, 'labeled' if labeled else 'unlabeled', '--data-edges', str(root/'de'), '--data-nodes', str(root/'dn'), '--query-edges', str(root/'qe'), '--query-nodes', str(root/'qn'), '--collect-file', str(root/'tuples.json'), '--batch-capacity', str(capacity), '--candidate-tile-size', str(tile)]
                    result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
                    report = json.loads(result.stdout)
                    actual_rows = json.loads((root/'tuples.json').read_text()) if result.returncode == 0 else []
                    actual = set(map(tuple, actual_rows))
                    if result.returncode or actual != expected or len(actual_rows) != len(actual) or report['embedding_count'] != str(len(expected)):
                        failure = Path('results/failure_'+str(a.seed)+'_'+str(case))
                        shutil.copytree(root, failure, dirs_exist_ok=True)
                        (failure/'details.json').write_text(json.dumps({'command':cmd, 'expected':sorted(expected), 'report':report}, indent=2))
                        raise AssertionError('differential mismatch: '+str(failure))
                    runs += 1
    out = Path(a.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({'status':'pass','cases':a.cases,'seed':a.seed,'executions':runs,'comparison':'full tuple sets and no duplicate tuples'}, indent=2)+'\n')
    print(out.read_text())


if __name__ == '__main__':
    main()
