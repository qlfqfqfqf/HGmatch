#pragma once
#include "hgmatch/inverted_index.hpp"
#include "hgmatch/normalize.hpp"
#include "hgmatch/plan.hpp"
#include "hgmatch/gpu.hpp"
#include "hgmatch/storage.hpp"
#include <set>
namespace hg {
template <class M>
ExecutionReport solve(Hypergraph d, Hypergraph q, size_t capacity = 65536,
                      size_t tile = 65536) {
  normalize(d);
  normalize(q);
  validate_query(q);
  auto data = prepare<M>(std::move(d));
  auto query = prepare<M>(std::move(q));
  build_partitions(data);
  build_index(data);
  RunControl c(0);
  auto p = compute_order(query, data, c);
  RunOptions o;
  o.batch_capacity = capacity;
  o.candidate_tile_size = tile;
  return run_gpu(data, query, p, o, c);
}
inline Hypergraph graph(std::vector<Edge> edges, std::vector<Label> labels) {
  Hypergraph g;
  g.edges = std::move(edges);
  g.labels = std::move(labels);
  g.vertex_count = g.labels.size();
  return g;
}
} // namespace hg
