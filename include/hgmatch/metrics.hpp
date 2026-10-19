#pragma once
#include "types.hpp"
#include <nlohmann/json.hpp>
namespace hg {
using Json = nlohmann::ordered_json;
Json graph_stats(const NormalizeStats &, size_t vertices);
Json execution_json(const ExecutionReport &, const RunOptions &);
template <class M> Json memory_stats(const IndexedGraph<M> &);
} // namespace hg
