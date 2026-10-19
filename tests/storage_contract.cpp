#include "support/collect.hpp"
#include "support/test_harness.hpp"
using namespace hg;
TEST(storage_contract, duplicates) {
  auto d = graph({{0, 1}, {1, 0}, {0, 0, 1}, {1, 2}}, {1, 1, 1});
  normalize(d);
  CHECK(d.edges.size() == 2);
  CHECK(d.stats.removed_vertices == 1);
  CHECK(d.stats.removed_edges == 2);
  auto g = prepare<Unlabeled>(std::move(d));
  build_partitions(g);
  build_index(g);
  CHECK(g.parts.size() == 1);
  CHECK(g.parts[0].postings.at(1).size() == 2);
}
TEST(storage_contract, signatures) {
  auto d = graph({{0, 1, 2}, {1, 2, 3}}, {1, 1, 2, 2});
  normalize(d);
  auto l = prepare<Labeled>(d);
  build_partitions(l);
  auto u = prepare<Unlabeled>(d);
  build_partitions(u);
  CHECK(l.parts.size() == 2);
  CHECK(u.parts.size() == 1);
}
