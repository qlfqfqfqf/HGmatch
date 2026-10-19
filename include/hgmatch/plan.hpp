#pragma once
#include "types.hpp"
namespace hg {
template <class M>
QueryPlan compute_order(const IndexedGraph<M> &query,
                        const IndexedGraph<M> &data, RunControl &);
using Incidence = std::unordered_map<VertexId, std::vector<size_t>>;
template <class M>
Incidence incidence(const IndexedGraph<M> &, const std::vector<size_t> &,
                    RunControl * = nullptr);
} // namespace hg
