#include "hgmatch/mode.hpp"
namespace hg {
template <class M> IndexedGraph<M> prepare(Hypergraph g) {
  IndexedGraph<M> h;
  h.vertex_count = g.vertex_count;
  h.stats = g.stats;
  h.edges = std::move(g.edges);
  h.source_lines = std::move(g.source_lines);
  if constexpr (M::labeled)
    h.labels = std::move(g.labels);
  return h;
}
template <class M>
typename M::Signature signature(const IndexedGraph<M> &g, const Edge &e) {
  if constexpr (M::labeled) {
    std::vector<Label> s;
    for (auto v : e)
      s.push_back(g.labels[v]);
    std::sort(s.begin(), s.end());
    return s;
  } else {
    return e.size();
  }
}
template IndexedGraph<Labeled> prepare(Hypergraph);
template IndexedGraph<Unlabeled> prepare(Hypergraph);
template Labeled::Signature signature(const IndexedGraph<Labeled> &,
                                      const Edge &);
template Unlabeled::Signature signature(const IndexedGraph<Unlabeled> &,
                                        const Edge &);
} // namespace hg
