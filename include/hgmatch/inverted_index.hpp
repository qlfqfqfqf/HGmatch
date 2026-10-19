#pragma once
#include "types.hpp"
namespace hg {
template <class M> void build_index(IndexedGraph<M> &);
template <class M>
std::span<const EdgeId> postings(const IndexedGraph<M> &, size_t, VertexId);
} // namespace hg
