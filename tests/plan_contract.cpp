#include "support/collect.hpp"
#include "support/test_harness.hpp"
using namespace hg;
TEST(plan_contract, union_overlap_not_pairwise_sum) {
  auto d = graph({{0, 1, 2}, {1, 2, 3}, {1, 2, 4}, {0, 3, 4}, {0, 1}, {2, 3}},
                 {1, 1, 1, 1, 1});
  auto q = graph({{0, 1, 2}, {1, 2, 3}, {1, 2, 4}, {0, 3, 4}, {0, 1}},
                 {1, 1, 1, 1, 1});
  normalize(d);
  normalize(q);
  auto h = prepare<Unlabeled>(std::move(d));
  auto query = prepare<Unlabeled>(std::move(q));
  build_partitions(h);
  RunControl c(0);
  auto p = compute_order(query, h, c);
  std::set<VertexId> joined;
  std::set<size_t> used;
  for (size_t depth = 0; depth < query.edges.size(); ++depth) {
    size_t best = missing, best_den = 1, best_card = 0;
    for (size_t i = 0; i < query.edges.size(); ++i)
      if (!used.count(i)) {
        size_t den = 0;
        for (auto v : query.edges[i])
          den += joined.count(v);
        if (depth == 0)
          den = 1;
        if (!den)
          continue;
        size_t card =
            h.parts[h.lookup.at(signature(query, query.edges[i]))].edges.size();
        if (best == missing ||
            Count(card) * best_den < Count(best_card) * den) {
          best = i;
          best_den = den;
          best_card = card;
        }
      }
    CHECK(p.order[depth] == best);
    used.insert(best);
    joined.insert(query.edges[best].begin(), query.edges[best].end());
    CHECK(p.prefix_vertex_counts[depth] == joined.size());
  }
}
TEST(plan_contract, quadratic_star_order) {
  Hypergraph q;
  q.vertex_count = 2001;
  q.labels.assign(q.vertex_count, 1);
  for (size_t i = 1; i < q.vertex_count; ++i)
    q.edges.push_back({0, i});
  normalize(q);
  auto query = prepare<Unlabeled>(q), h = prepare<Unlabeled>(std::move(q));
  build_partitions(h);
  RunControl c(0);
  auto p = compute_order(query, h, c);
  CHECK(p.order.size() == 2000);
  for (size_t i = 0; i < p.order.size(); ++i) {
    CHECK(p.order[i] == i);
    CHECK(p.prefix_vertex_counts[i] == i + 2);
  }
}
