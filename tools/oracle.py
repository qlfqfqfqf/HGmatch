#!/usr/bin/env python3
"""Independent tiny-graph oracle: enumerate vertex injections, deduplicate edge tuples."""
import argparse
import itertools
import json
from pathlib import Path


def read_graph(edges_path, nodes_path, base, paired=False):
    labels = [int(x.strip()) for x in Path(nodes_path).read_text().splitlines()]
    lines = Path(edges_path).read_text().splitlines()
    if paired:
        if len(lines) % 2:
            raise ValueError('missing even edge line')
        for metadata in lines[::2]:
            int(metadata.strip())
        lines = lines[1::2]
    edges = []
    seen = set()
    for line in lines:
        edge = tuple(sorted(set(int(x)-base for x in line.replace(',', ' ').split())))
        if not edge or min(edge) < 0 or max(edge) >= len(labels):
            raise ValueError('invalid edge')
        if edge not in seen:
            seen.add(edge)
            edges.append(edge)
    return edges, labels


def embeddings(data_edges, data_labels, query_edges, query_labels, labeled=True):
    if len(data_labels) > 9 or len(query_labels) > 7:
        raise ValueError('oracle restricted to <=9 data and <=7 query vertices')
    lookup = {tuple(sorted(e)): i for i, e in enumerate(data_edges)}
    result = set()
    for image in itertools.permutations(range(len(data_labels)), len(query_labels)):
        if labeled and any(query_labels[u] != data_labels[v] for u, v in enumerate(image)):
            continue
        row = []
        for edge in query_edges:
            mapped = tuple(sorted(image[u] for u in edge))
            if mapped not in lookup:
                break
            row.append(lookup[mapped])
        else:
            result.add(tuple(row))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--self-test', action='store_true')
    for arg in ('data-edges', 'data-nodes', 'query-edges', 'query-nodes'):
        p.add_argument('--'+arg)
    p.add_argument('--data-id-base', type=int, default=1, choices=(0, 1))
    p.add_argument('--query-id-base', type=int, default=0, choices=(0, 1))
    p.add_argument('--unlabeled', action='store_true')
    a = p.parse_args()
    if a.self_test:
        assert len(embeddings([(0,1,2)], [1,1,1], [(0,1,2)], [1,1,1])) == 1
        assert len(embeddings([(0,1),(1,2)], [1,1,1], [(0,1),(1,2)], [1,1,1])) == 2
        assert not embeddings([(0,1,2)], [1,1,1], [(0,1)], [1,1])
        assert len(embeddings([(0,1),(0,2),(1,2)], [1,1,1], [(0,1),(1,2)], [1,1,1])) == 6
        print('oracle manual semantic cases: PASS')
        return
    if not all((a.data_edges,a.data_nodes,a.query_edges,a.query_nodes)):
        p.error('four input paths are required unless --self-test')
    d, dl = read_graph(a.data_edges, a.data_nodes, a.data_id_base)
    q, ql = read_graph(a.query_edges, a.query_nodes, a.query_id_base, True)
    matches = sorted(embeddings(d, dl, q, ql, not a.unlabeled))
    print(json.dumps({'embedding_count': str(len(matches)), 'tuples': matches}))


if __name__ == '__main__':
    main()
