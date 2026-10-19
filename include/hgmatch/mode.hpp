#pragma once
#include "types.hpp"
namespace hg {
template <class M> IndexedGraph<M> prepare(Hypergraph graph);
template <class M>
typename M::Signature signature(const IndexedGraph<M> &, const Edge &);
} // namespace hg
