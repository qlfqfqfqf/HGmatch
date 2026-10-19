#include "hgmatch/error.hpp"
#include "hgmatch/resources.hpp"
#include "hgmatch/metrics.hpp"
#include "hgmatch/gpu_count.hpp"
#include "hgmatch/types.hpp"
#include "support/collect.hpp"
#include "support/test_harness.hpp"
#include <cmath>
using namespace hg;
TEST(metrics_contract, sha) {
  CHECK(sha256_text("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256_text("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
TEST(metrics_contract, overflow) {
  Count v = ~Count(0);
  bool failed = false;
  try {
    add_count(v);
  } catch (const Error &e) {
    failed = e.status == "count_overflow";
  }
  CHECK(failed);
  CHECK(decimal(Count(1) << 64) == "18446744073709551616");
}

TEST(metrics_contract, gpu_phase_time_aggregation) {
  ExecutionReport r; r.depth.resize(2); r.timing_complete = true;
  r.depth[0].candidate_generation_time_s = 0.25;
  r.depth[1].candidate_generation_time_s = 0.5;
  r.depth[0].embedding_validation_time_s = 1.0;
  r.depth[1].embedding_validation_time_s = 2.0;
  auto j = execution_json(r, RunOptions{});
  CHECK(j["candidate_generation_time_s"] == 0.75);
  CHECK(j["embedding_validation_time_s"] == 3.0);
  CHECK(j["last_depth"]["embedding_validation_time_s"] == 2.0);
  CHECK(j["gpu"]["compute_streams"] == 1);
  CHECK(!j.contains("workers"));
}
TEST(metrics_contract, device_counter_carry_and_overflow) {
  WideCount a{~0ull, 0}; int overflow = 0;
  wide_add(a, 1, &overflow);
  CHECK(a.lo == 0 && a.hi == 1 && !overflow);
  WideCount b{~0ull, ~0ull};
  wide_add(b, 1, &overflow);
  CHECK(overflow == 1);
  CHECK(b.lo == ~0ull && b.hi == ~0ull);
}
