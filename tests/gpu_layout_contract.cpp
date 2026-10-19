#include "support/collect.hpp"
#include "support/test_harness.hpp"
using namespace hg;
TEST(gpu_layout_contract, sparse_sorted_postings) {
  auto h = prepare<Unlabeled>(graph({{0, 1}, {0, 2}, {1, 2}, {0, 1, 2}}, {1, 2, 3}));
  auto q = prepare<Unlabeled>(graph({{0, 1}, {1, 2}}, {1, 2, 3}));
  build_partitions(h); build_index(h); RunControl c(0);
  auto p = compute_order(q, h, c); auto f = flatten_for_gpu(h, q, p);
  CHECK(f.labels.empty()); CHECK(f.partition_offsets == std::vector<GpuId>({0, 3, 4}));
  CHECK(f.posting_partitions == std::vector<GpuId>({0, 0, 0, 1, 1, 1}));
  CHECK(f.posting_vertices == std::vector<GpuId>({0, 1, 2, 0, 1, 2}));
  CHECK(f.posting_edges == std::vector<GpuId>({0, 1, 0, 2, 1, 2, 0, 0, 0}));
  CHECK(f.constraint_offsets == std::vector<GpuId>({0, 0, 1}));
  CHECK(f.constraints[0].degree == 1); CHECK(f.bytes() > 0);
}
TEST(gpu_layout_contract, dynamic_profiles65) {
  Hypergraph g; g.labels.assign(65, 1); g.vertex_count = 65;
  Edge e; for (size_t i = 0; i < 65; ++i) { e.push_back(i); g.edges.push_back(e); }
  auto h = prepare<Labeled>(g); build_partitions(h); build_index(h);
  RunControl c(0); auto p = compute_order(h, h, c);
  auto f = flatten_for_gpu(h, h, p);
  CHECK(f.profile_words == 2); CHECK(f.labels.size() == 65);
  CHECK(f.profile_masks.size() == 2 * f.profile_counts.size());
  bool second_word = false;
  for (size_t i = 1; i < f.profile_masks.size(); i += 2) second_word |= f.profile_masks[i] != 0;
  CHECK(second_word);
}
