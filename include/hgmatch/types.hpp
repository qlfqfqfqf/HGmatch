#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace hg {
using VertexId = std::size_t;
using EdgeId = std::size_t;
using Label = std::int64_t;
using Count = unsigned __int128;
using Edge = std::vector<VertexId>;
using Clock = std::chrono::steady_clock;
struct NormalizeStats {
  size_t raw_edges = 0, unique_edges = 0, removed_edges = 0,
         removed_vertices = 0, raw_incidences = 0, incidences = 0,
         used_vertices = 0, max_arity = 0;
};
struct Hypergraph {
  size_t vertex_count = 0;
  std::vector<Edge> edges;
  std::vector<Label> labels;
  std::vector<size_t> source_lines;
  NormalizeStats stats;
};
struct Labeled {
  static constexpr bool labeled = true;
  using Signature = std::vector<Label>;
  using Profile = std::pair<Label, std::vector<size_t>>;
};
struct Unlabeled {
  static constexpr bool labeled = false;
  using Signature = size_t;
  using Profile = std::vector<size_t>;
};
template <bool> struct Labels {};
template <> struct Labels<true> {
  std::vector<Label> labels;
};
template <class M> struct Partition {
  typename M::Signature signature;
  std::vector<EdgeId> edges;
  std::unordered_map<VertexId, std::vector<EdgeId>> postings;
};
template <class M> struct SignatureHash {
  size_t operator()(const typename M::Signature &s) const noexcept {
    if constexpr (M::labeled) {
      size_t h = s.size();
      for (auto label : s)
        h ^= std::hash<Label>{}(label) + size_t(0x9e3779b9) + (h << 6) +
             (h >> 2);
      return h;
    } else
      return std::hash<size_t>{}(s);
  }
};
template <class M> struct IndexedGraph : Labels<M::labeled> {
  size_t vertex_count = 0;
  std::vector<Edge> edges;
  NormalizeStats stats;
  std::vector<size_t> source_lines;
  std::vector<Partition<M>> parts;
  std::unordered_map<typename M::Signature, size_t, SignatureHash<M>> lookup;
};
struct QueryPlan {
  std::vector<size_t> order, partition_ids;
  std::vector<size_t> prefix_vertex_counts;
};
inline constexpr size_t missing = static_cast<size_t>(-1);
struct Embedding {
  std::vector<EdgeId> edges;
};
struct DepthStats {
  Count candidates = 0, vertex_pass = 0, valid = 0;
  double candidate_generation_time_s = 0, embedding_validation_time_s = 0;
};
struct RunOptions {
  int gpu_id = 0;
  size_t gpu_memory_mb = 0, batch_capacity = 65536,
         candidate_tile_size = 65536;
};
struct RunControl {
  std::atomic<bool> cancelled{false};
  Clock::time_point deadline;
  bool limited;
  std::atomic<int> failure{0};
  char message[512]{};
  explicit RunControl(double seconds);
  void check();
  void fail(std::string_view status, std::string_view message) noexcept;
  const char *status() const noexcept;
};
struct ExecutionReport {
  std::string status = "ok", message;
  Count count = 0, root_candidates = 0;
  std::vector<DepthStats> depth;
  double matching_time_s = 0;
  double cuda_initialize_time_s = 0, device_layout_time_s = 0,
         host_to_device_time_s = 0;
  int gpu_id = 0, compute_major = 0, compute_minor = 0,
      cuda_runtime_version = 0, cuda_driver_version = 0;
  std::string gpu_name;
  size_t device_total_bytes = 0, device_free_bytes = 0,
         device_budget_bytes = 0, device_peak_allocated_bytes = 0,
         actual_batch_capacity = 0;
  bool timing_complete = false;
  bool counters_available = false;
  uint64_t batches = 0;
};
using Observer = std::function<void(const Embedding &)>;
std::string decimal(Count value);
void add_count(Count &dst, Count value = 1);
double seconds(Clock::time_point a, Clock::time_point b);
} // namespace hg
