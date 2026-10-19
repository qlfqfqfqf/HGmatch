#include "hgmatch/normalize.hpp"
#include "hgmatch/error.hpp"
#include <set>
namespace hg {
void normalize(Hypergraph &g) {
  NormalizeStats s;
  s.raw_edges = g.edges.size();
  std::set<Edge> seen;
  std::set<VertexId> used;
  std::vector<Edge> out;
  std::vector<size_t> lines;
  for (size_t i = 0; i < g.edges.size(); ++i) {
    auto &e = g.edges[i];
    s.raw_incidences += e.size();
    auto old = e.size();
    std::sort(e.begin(), e.end());
    e.erase(std::unique(e.begin(), e.end()), e.end());
    s.removed_vertices += old - e.size();
    if (e.empty())
      throw Error("input_error", "empty hyperedge");
    if (e.back() >= g.vertex_count)
      throw Error("input_error", "vertex outside graph");
    if (seen.insert(e).second) {
      s.incidences += e.size();
      s.max_arity = std::max(s.max_arity, e.size());
      used.insert(e.begin(), e.end());
      out.push_back(std::move(e));
      lines.push_back(i < g.source_lines.size() ? g.source_lines[i] : i + 1);
    }
  }
  s.unique_edges = out.size();
  s.removed_edges = s.raw_edges - s.unique_edges;
  s.used_vertices = used.size();
  g.edges = std::move(out);
  g.source_lines = std::move(lines);
  g.stats = s;
}
void validate_query(const Hypergraph &q) {
  if (q.edges.empty())
    throw Error("input_error", "query must have at least one edge");
  std::vector<bool> seen(q.vertex_count);
  for (auto &e : q.edges)
    for (auto v : e)
      seen[v] = true;
  if (std::count(seen.begin(), seen.end(), true) !=
      static_cast<ptrdiff_t>(q.vertex_count))
    throw Error("input_error",
                "isolated query vertices are outside paper scope");
  std::fill(seen.begin(), seen.end(), false);
  for (auto v : q.edges[0])
    seen[v] = true;
  std::vector<bool> done(q.edges.size());
  done[0] = true;
  bool change = true;
  while (change) {
    change = false;
    for (size_t i = 0; i < q.edges.size(); ++i)
      if (!done[i]) {
        bool shared = false;
        for (auto v : q.edges[i])
          shared = shared || seen[v];
        if (shared) {
          done[i] = true;
          change = true;
          for (auto v : q.edges[i])
            seen[v] = true;
        }
      }
  }
  if (std::count(done.begin(), done.end(), true) !=
      static_cast<ptrdiff_t>(done.size()))
    throw Error("input_error", "disconnected query is outside paper scope");
}
} // namespace hg
