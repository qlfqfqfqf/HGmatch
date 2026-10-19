#pragma once
#include "types.hpp"
#include <cstdint>
namespace hg {
// CPU builds this transport layout; it never enumerates data embeddings.
using GpuId = uint64_t;
struct CandidateConstraint {
  GpuId old_position, degree;
  Label label;
};
struct FlatGpuInput {
  std::vector<GpuId> edge_offsets, vertices, partition_offsets, partition_edges;
  std::vector<Label> labels;
  std::vector<GpuId> posting_partitions, posting_vertices, posting_offsets,
      posting_edges;
  std::vector<GpuId> order, partition_ids, prefix_vertex_counts,
      constraint_offsets, profile_offsets, profile_masks, profile_counts;
  std::vector<CandidateConstraint> constraints;
  std::vector<Label> profile_labels;
  std::vector<uint8_t> forbidden;
  size_t query_edges = 0, profile_words = 0;
  size_t bytes() const;
};
template <class M>
FlatGpuInput flatten_for_gpu(const IndexedGraph<M> &, const IndexedGraph<M> &,
                            const QueryPlan &);
template <class M>
ExecutionReport run_gpu(const IndexedGraph<M> &, const IndexedGraph<M> &,
                        const QueryPlan &, const RunOptions &, RunControl &,
                        const Observer & = {});
} // namespace hg
