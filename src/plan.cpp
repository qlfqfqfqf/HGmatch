#include "hgmatch/plan.hpp"
#include "hgmatch/error.hpp"
#include "hgmatch/mode.hpp"
namespace hg {
template <class M>
QueryPlan compute_order(const IndexedGraph<M> &q, const IndexedGraph<M> &h,
                        RunControl &c) {
  QueryPlan p;
  size_t k = q.edges.size();
  std::vector<size_t> card(k), pid(k, missing);
  for (size_t i = 0; i < k; ++i) {
    c.check();
    auto it = h.lookup.find(signature(q, q.edges[i]));
    if (it != h.lookup.end()) {
      pid[i] = it->second;
      card[i] = h.parts[it->second].edges.size();
    }
  }
  // Query-only preparation: a vertex contributes to overlap only on its
  // first insertion into the prefix union. Each incidence is updated once.
  std::vector<std::vector<size_t>> query_incidence(q.vertex_count);
  size_t ticks = 0;
  for (size_t i = 0; i < k; ++i)
    for (auto v : q.edges[i]) {
      if ((++ticks & 1023) == 0)
        c.check();
      query_incidence[v].push_back(i);
    }
  std::vector<size_t> overlap(k);
  std::vector<bool> used(k), joined(q.vertex_count);
  size_t joined_count = 0;
  while (p.order.size() < k) {
    c.check();
    size_t best = missing, denbest = 1;
    for (size_t i = 0; i < k; ++i) {
      if ((i & 1023) == 0)
        c.check();
      if (used[i])
        continue;
      size_t den = p.order.empty() ? 1 : overlap[i];
      if (!den)
        continue;
      if (best == missing ||
          Count(card[i]) * denbest < Count(card[best]) * den) {
        best = i;
        denbest = den;
      }
    }
    if (best == missing)
      throw Error("input_error",
                  "query order cannot advance: disconnected query");
    used[best] = true;
    p.order.push_back(best);
    p.partition_ids.push_back(pid[best]);
    for (auto v : q.edges[best])
      if (!joined[v]) {
        joined[v] = true;
        ++joined_count;
        for (auto edge : query_incidence[v]) {
          if ((++ticks & 1023) == 0)
            c.check();
          ++overlap[edge];
        }
      }
    p.prefix_vertex_counts.push_back(joined_count);
  }
  return p;
}
template <class M>
Incidence incidence(const IndexedGraph<M> &g, const std::vector<size_t> &edges,
                    RunControl *c) {
  Incidence result;
  size_t n = 0;
  for (size_t j = 0; j < edges.size(); ++j)
    for (auto v : g.edges[edges[j]]) {
      if (c && !(++n % 1024))
        c->check();
      result[v].push_back(j);
    }
  return result;
}
template QueryPlan compute_order(const IndexedGraph<Labeled> &,
                                 const IndexedGraph<Labeled> &, RunControl &);
template QueryPlan compute_order(const IndexedGraph<Unlabeled> &,
                                 const IndexedGraph<Unlabeled> &, RunControl &);
template Incidence incidence(const IndexedGraph<Labeled> &,
                             const std::vector<size_t> &, RunControl *);
template Incidence incidence(const IndexedGraph<Unlabeled> &,
                             const std::vector<size_t> &, RunControl *);
} // namespace hg
