#include "support/collect.hpp"
#include "support/test_harness.hpp"
using namespace hg;
TEST(algorithm_contract, single_edge) {
  auto g = graph({{0, 1, 2}}, {1, 1, 1});
  CHECK(solve<Labeled>(g, g).count == 1);
  CHECK(solve<Unlabeled>(g, g).count == 1);
}
TEST(algorithm_contract, symmetric) {
  auto g = graph({{0, 1}, {1, 2}}, {1, 1, 1});
  CHECK(solve<Labeled>(g, g).count == 2);
}
TEST(algorithm_contract, validation_labels) {
  auto q = graph({{0, 1}, {0, 2}}, {1, 2, 2}),
       d = graph({{0, 1}, {0, 2}}, {2, 1, 1});
  CHECK(solve<Labeled>(d, q).count == 0);
  CHECK(solve<Unlabeled>(d, q).count == 2);
}
TEST(algorithm_contract, validation_four_way) {
  auto q = graph({{1, 2}, {1, 3}, {2, 3}, {0, 1, 2, 3}}, {1, 1, 1, 1}),
       d = graph({{0, 3}, {1, 3}, {2, 3}, {0, 1, 2, 3}}, {1, 1, 1, 1});
  CHECK(solve<Labeled>(d, q).count == 0);
  CHECK(solve<Unlabeled>(d, q).count == 0);
}
TEST(algorithm_contract, exact_not_containment) {
  CHECK(solve<Unlabeled>(graph({{0, 1, 2}}, {1, 1, 1}), graph({{0, 1}}, {1, 1}))
            .count == 0);
}
TEST(algorithm_contract, extra_edges) {
  CHECK(solve<Unlabeled>(graph({{0, 1}, {1, 2}, {0, 2}}, {1, 1, 1}),
                         graph({{0, 1}, {1, 2}}, {1, 1, 1}))
            .count == 6);
}
TEST(algorithm_contract, local_degree) {
  CHECK(solve<Unlabeled>(graph({{0, 1}, {1, 2}, {1, 3}}, {1, 1, 1, 1}),
                         graph({{0, 1}, {1, 2}}, {1, 1, 1}))
            .count == 6);
}
TEST(algorithm_contract, reuse) {
  CHECK(solve<Unlabeled>(graph({{0, 1}}, {1, 1}),
                         graph({{0, 1}, {1, 2}}, {1, 1, 1}))
            .count == 0);
}
TEST(algorithm_contract, nested65) {
  Hypergraph g;
  g.labels.assign(65, 1);
  g.vertex_count = 65;
  Edge e;
  for (size_t i = 0; i < 65; ++i) {
    e.push_back(i);
    g.edges.push_back(e);
  }
  CHECK(solve<Unlabeled>(g, g).count == 1);
}
