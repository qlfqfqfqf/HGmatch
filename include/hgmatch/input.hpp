#pragma once
#include "types.hpp"
namespace hg {
Hypergraph read_data_hypergraph(const std::string &, const std::string &,
                                int base = 1);
Hypergraph read_query_hypergraph(const std::string &, const std::string &,
                                 int base = 0);
} // namespace hg
