#include "support/collect.hpp"
#include "support/test_harness.hpp"
#include "hgmatch/metrics.hpp"
#include <cmath>
using namespace hg;
template <class M> void bounded_gpu_cases() {
  auto d = graph({{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}}, {1,1,1,1});
  auto q = graph({{0,1},{1,2}}, {1,1,1});
  for (size_t capacity : {1, 3, 17, 256})
    for (size_t tile : {1, 2, 65536}) {
      auto r = solve<M>(d, q, capacity, tile);
      CHECK(r.status == "ok"); CHECK(r.count == 24); CHECK(r.timing_complete);
      CHECK(r.actual_batch_capacity <= capacity);
      CHECK(r.device_peak_allocated_bytes <= r.device_budget_bytes);
      auto j = execution_json(r, RunOptions{});
      for (auto key : {"candidate_generation_time_s", "embedding_validation_time_s"}) {
        double time = j[key]; CHECK(std::isfinite(time) && time >= 0);
        CHECK(time <= r.matching_time_s + 1e-5);
      }
    }
}
TEST(gpu_contract, batches_preserve_counts) {
  bounded_gpu_cases<Labeled>(); bounded_gpu_cases<Unlabeled>();
}
TEST(gpu_contract, validation_rejection_and_single_edge) {
  auto q = graph({{0,1},{1,2}}, {1,1,1});
  auto r = solve<Unlabeled>(graph({{0,1}}, {1,1}), q, 1, 1);
  CHECK(r.status == "ok"); CHECK(r.count == 0);
  CHECK(r.depth[0].candidates > 0 && r.depth[0].vertex_pass == 0);
  CHECK(r.depth[0].embedding_validation_time_s >= 0);
  auto s = solve<Labeled>(graph({{0},{1}}, {1,1}), graph({{0}}, {1}), 1);
  CHECK(s.status == "ok" && s.count == 2 && s.depth.empty());
  CHECK(execution_json(s, RunOptions{})["candidate_generation_time_s"] == 0.0);
}
TEST(gpu_contract, timed_controlled_cancellation) {
  auto h = prepare<Unlabeled>(graph({{0},{1},{2}}, {1,1,1}));
  auto q = prepare<Unlabeled>(graph({{0}}, {1}));
  build_partitions(h); build_index(h); RunControl c(0);
  auto p = compute_order(q, h, c); RunOptions o; o.batch_capacity = 1;
  auto r = run_gpu(h, q, p, o, c, [&](const Embedding &) { c.fail("timeout", "controlled cancellation"); });
  CHECK(r.status == "timeout" && !r.timing_complete);
  CHECK(r.count == 1);
}
