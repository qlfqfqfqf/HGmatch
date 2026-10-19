#include "hgmatch/inverted_index.hpp"
namespace hg {
template <class M> void build_index(IndexedGraph<M> &g) {
  for (auto &p : g.parts)
    for (auto id : p.edges)
      for (auto v : g.edges[id])
        p.postings[v].push_back(id);
}
template <class M>
std::span<const EdgeId> postings(const IndexedGraph<M> &g, size_t p,
                                 VertexId v) {
  if (p == missing)
    return {};
  auto it = g.parts[p].postings.find(v);
  if (it == g.parts[p].postings.end())
    return {};
  return it->second;
}
template void build_index(IndexedGraph<Labeled> &);
template void build_index(IndexedGraph<Unlabeled> &);
template std::span<const EdgeId> postings(const IndexedGraph<Labeled> &, size_t,
                                          VertexId);
template std::span<const EdgeId> postings(const IndexedGraph<Unlabeled> &,
                                          size_t, VertexId);
} // namespace hg
