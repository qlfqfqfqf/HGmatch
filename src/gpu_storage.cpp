#include "hgmatch/gpu.hpp"
#include "hgmatch/error.hpp"
#include <limits>
#include <tuple>
namespace hg {
namespace {
template <class T> void size_add(size_t &total, const std::vector<T> &v) {
  if (v.size() > (std::numeric_limits<size_t>::max() - total) / sizeof(T))
    throw Error("out_of_memory", "device layout size overflow");
  total += v.size() * sizeof(T);
}
}
size_t FlatGpuInput::bytes() const {
  size_t n = 0;
  for (const auto *v : {&edge_offsets, &vertices, &partition_offsets,
                       &partition_edges, &posting_partitions, &posting_vertices,
                       &posting_offsets, &posting_edges, &order, &partition_ids,
                       &prefix_vertex_counts, &constraint_offsets,
                       &profile_offsets, &profile_masks, &profile_counts})
    size_add(n, *v);
  size_add(n, labels); size_add(n, constraints);
  size_add(n, profile_labels); size_add(n, forbidden);
  return n;
}
template <class M>
FlatGpuInput flatten_for_gpu(const IndexedGraph<M> &h, const IndexedGraph<M> &q,
                            const QueryPlan &p) {
  FlatGpuInput f;
  f.query_edges = p.order.size();
  f.profile_words = (f.query_edges + 63) / 64;
  f.edge_offsets.push_back(0);
  for (const auto &e : h.edges) {
    f.vertices.insert(f.vertices.end(), e.begin(), e.end());
    f.edge_offsets.push_back(f.vertices.size());
  }
  if constexpr (M::labeled) f.labels = h.labels;
  f.partition_offsets.push_back(0);
  f.posting_offsets.push_back(0);
  for (size_t pid = 0; pid < h.parts.size(); ++pid) {
    const auto &part = h.parts[pid];
    f.partition_edges.insert(f.partition_edges.end(), part.edges.begin(), part.edges.end());
    f.partition_offsets.push_back(f.partition_edges.size());
    std::vector<VertexId> keys;
    for (const auto &[v, list] : part.postings) { (void)list; keys.push_back(v); }
    std::sort(keys.begin(), keys.end());
    for (auto v : keys) {
      f.posting_partitions.push_back(pid); f.posting_vertices.push_back(v);
      for (auto e : part.postings.at(v)) {
        auto it = std::lower_bound(part.edges.begin(), part.edges.end(), e);
        if (it == part.edges.end() || *it != e)
          throw Error("internal_error", "posting outside its partition");
        f.posting_edges.push_back(it - part.edges.begin());
      }
      f.posting_offsets.push_back(f.posting_edges.size());
    }
  }
  f.order.assign(p.order.begin(), p.order.end());
  f.partition_ids.assign(p.partition_ids.begin(), p.partition_ids.end());
  f.prefix_vertex_counts.assign(p.prefix_vertex_counts.begin(), p.prefix_vertex_counts.end());
  const size_t k = f.query_edges;
  if (k && k > std::numeric_limits<size_t>::max() / k)
    throw Error("out_of_memory", "query metadata size overflow");
  f.forbidden.resize(k * k);
  f.constraint_offsets.push_back(0); f.profile_offsets.push_back(0);
  for (size_t d = 0; d < k; ++d) {
    const auto &next = q.edges[p.order[d]];
    for (size_t j = 0; j < d; ++j) {
      const auto &old = q.edges[p.order[j]];
      bool adjacent = false;
      for (auto u : old)
        if (std::binary_search(next.begin(), next.end(), u)) {
          adjacent = true;
          size_t degree = 0;
          for (size_t a = 0; a < d; ++a) {
            const auto &e = q.edges[p.order[a]];
            degree += std::binary_search(e.begin(), e.end(), u);
          }
          Label label = 0;
          if constexpr (M::labeled) label = q.labels[u];
          f.constraints.push_back({j, degree, label});
        }
      f.forbidden[d * k + j] = !adjacent;
    }
    f.constraint_offsets.push_back(f.constraints.size());
    // Query-only reference multiset, encoded by edge POSITIONS, not data IDs.
    using Key = std::pair<Label, std::vector<GpuId>>;
    std::map<Key, GpuId> profiles;
    for (auto u : next) {
      std::vector<GpuId> mask(f.profile_words);
      for (size_t j = 0; j <= d; ++j) {
        const auto &e = q.edges[p.order[j]];
        if (std::binary_search(e.begin(), e.end(), u)) mask[j / 64] |= GpuId(1) << (j % 64);
      }
      Label label = 0;
      if constexpr (M::labeled) label = q.labels[u];
      ++profiles[{label, std::move(mask)}];
    }
    for (const auto &[key, count] : profiles) {
      f.profile_labels.push_back(key.first);
      f.profile_masks.insert(f.profile_masks.end(), key.second.begin(), key.second.end());
      f.profile_counts.push_back(count);
    }
    f.profile_offsets.push_back(f.profile_counts.size());
  }
  return f;
}
template FlatGpuInput flatten_for_gpu(const IndexedGraph<Labeled> &, const IndexedGraph<Labeled> &, const QueryPlan &);
template FlatGpuInput flatten_for_gpu(const IndexedGraph<Unlabeled> &, const IndexedGraph<Unlabeled> &, const QueryPlan &);
} // namespace hg
