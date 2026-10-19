#include "hgmatch/storage.hpp"
namespace hg {
template <class M> void build_partitions(IndexedGraph<M> &g) {
  for (size_t i = 0; i < g.edges.size(); ++i) {
    auto s = signature(g, g.edges[i]);
    auto [it, inserted] = g.lookup.emplace(s, g.parts.size());
    if (inserted) {
      Partition<M> p;
      p.signature = std::move(s);
      g.parts.push_back(std::move(p));
    }
    g.parts[it->second].edges.push_back(i);
  }
}
template void build_partitions(IndexedGraph<Labeled> &);
template void build_partitions(IndexedGraph<Unlabeled> &);
} // namespace hg
