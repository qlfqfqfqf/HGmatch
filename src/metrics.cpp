#include "hgmatch/metrics.hpp"
namespace hg {
Json graph_stats(const NormalizeStats &s, size_t v) {
  return {{"vertices", v},
          {"used_vertices", s.used_vertices},
          {"raw_edges", s.raw_edges},
          {"unique_edges", s.unique_edges},
          {"removed_duplicate_edges", s.removed_edges},
          {"removed_duplicate_vertices", s.removed_vertices},
          {"raw_incidences", s.raw_incidences},
          {"incidences", s.incidences},
          {"max_arity", s.max_arity},
          {"average_arity",
           s.unique_edges ? double(s.incidences) / s.unique_edges : 0.0}};
}
Json execution_json(const ExecutionReport &r, const RunOptions &o) {
  Json j;
  j["status"] = r.status;
  j["message"] = r.message;
  j["embedding_count"] =
      r.status == "ok" ? Json(decimal(r.count)) : Json(nullptr);
  j["partial_embedding_count"] =
      r.status == "ok" || !r.counters_available ? Json(nullptr) : Json(decimal(r.count));
  j["matching_time_s"] = r.matching_time_s;
  j["cuda_initialize_time_s"] = r.cuda_initialize_time_s;
  j["device_layout_time_s"] = r.device_layout_time_s;
  j["host_to_device_time_s"] = r.host_to_device_time_s;
  j["timing_complete"] = r.timing_complete;
  j["gpu"] = {{"id", r.gpu_id}, {"name", r.gpu_name},
    {"compute_major", r.compute_major}, {"compute_minor", r.compute_minor},
    {"runtime_version", r.cuda_runtime_version}, {"driver_version", r.cuda_driver_version},
    {"total_memory_bytes", r.device_total_bytes}, {"free_memory_bytes_at_start", r.device_free_bytes},
    {"memory_budget_bytes", r.device_budget_bytes}, {"peak_allocated_bytes", r.device_peak_allocated_bytes},
    {"actual_batch_capacity", r.actual_batch_capacity}, {"block_size", 256}, {"compute_streams", 1}};
  j["batches"] = r.batches;
  j["root_candidates"] = decimal(r.root_candidates);
  j["per_depth"] = Json::array();
  Count cs = 0, fs = 0, vs = 0;
  double candidate_time = 0, validation_time = 0;
  for (size_t i = 0; i < r.depth.size(); ++i) {
    auto &d = r.depth[i];
    candidate_time += d.candidate_generation_time_s;
    validation_time += d.embedding_validation_time_s;
    add_count(cs, d.candidates);
    add_count(fs, d.vertex_pass);
    add_count(vs, d.valid);
    j["per_depth"].push_back({{"matched_edges", i + 2},
                              {"candidates", decimal(d.candidates)},
                              {"vertex_count_pass", decimal(d.vertex_pass)},
                              {"valid", decimal(d.valid)},
                              {"candidate_generation_time_s",
                               d.candidate_generation_time_s},
                              {"embedding_validation_time_s",
                               d.embedding_validation_time_s}});
  }
  j["candidate_generation_time_s"] = candidate_time;
  j["embedding_validation_time_s"] = validation_time;
  j["candidates_total"] = decimal(cs);
  j["vertex_count_pass_total"] = decimal(fs);
  j["valid_partial_total"] = decimal(vs);
  j["last_depth"] = r.depth.empty() ? Json(nullptr) : j["per_depth"].back();
  if (!r.counters_available && r.status != "ok") {
    // A failed CUDA context may prevent counter download; do not fabricate zero.
    for (auto key : {"root_candidates", "per_depth", "candidates_total",
                     "vertex_count_pass_total", "valid_partial_total", "last_depth"})
      j.erase(key);
  }
  (void)o;
  return j;
}
template <class M> Json memory_stats(const IndexedGraph<M> &h) {
  size_t graph = sizeof(h) + h.edges.capacity() * sizeof(Edge) +
                 h.source_lines.capacity() * sizeof(size_t);
  for (auto &e : h.edges)
    graph += e.capacity() * sizeof(VertexId);
  if constexpr (M::labeled)
    graph += h.labels.capacity() * sizeof(Label);
  size_t signatures = h.parts.capacity() * sizeof(Partition<M>), index = 0,
         payload = 0;
  for (auto &p : h.parts) {
    graph += p.edges.capacity() * sizeof(EdgeId);
    if constexpr (M::labeled)
      signatures += p.signature.capacity() * sizeof(Label);
    index += p.postings.bucket_count() * sizeof(void *);
    for (auto &[v, ids] : p.postings) {
      (void)v;
      payload += ids.size() * sizeof(EdgeId);
      index += ids.capacity() * sizeof(EdgeId) +
               sizeof(std::pair<const VertexId, std::vector<EdgeId>>) +
               sizeof(void *);
    }
  }
  signatures +=
      h.lookup.bucket_count() * sizeof(void *) +
      h.lookup.size() *
          (sizeof(typename decltype(h.lookup)::value_type) + sizeof(void *));
  if constexpr (M::labeled)
    for (auto &[key, id] : h.lookup) {
      (void)id;
      signatures += key.capacity() * sizeof(Label);
    }
  return {{"graph_storage_bytes", graph},
          {"signature_storage_bytes", signatures},
          {"inverted_index_bytes", index},
          {"inverted_posting_payload_bytes", payload},
          {"storage_bytes_method",
           "owned_capacity_estimate: containers/node overhead estimated; "
           "allocator metadata excluded; not RSS"}};
}
template Json memory_stats(const IndexedGraph<Labeled> &);
template Json memory_stats(const IndexedGraph<Unlabeled> &);
} // namespace hg
