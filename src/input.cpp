#include "hgmatch/input.hpp"
#include "hgmatch/error.hpp"
#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>
namespace hg {
namespace {
std::string trim(std::string s) {
  auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return "";
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
bool integer_text(const std::string &s) {
  size_t i = (!s.empty() && (s[0] == '-' || s[0] == '+'));
  if (i == s.size())
    return false;
  for (; i < s.size(); ++i)
    if (s[i] < '0' || s[i] > '9')
      return false;
  return true;
}
int64_t integer(std::string s, const std::string &p, size_t line) {
  if (!integer_text(s))
    input_error(p, line, "expected one integer");
  if (s[0] == '+')
    s.erase(0, 1);
  int64_t v;
  auto [e, err] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (err != std::errc{} || e != s.data() + s.size())
    input_error(p, line, "integer out of range");
  return v;
}
Hypergraph read(const std::string &ep, const std::string &np, int base,
                bool query) {
  if (base != 0 && base != 1)
    throw Error("input_error", "ID base must be 0 or 1");
  Hypergraph g;
  std::ifstream nf(np);
  if (!nf)
    input_error(np, 0, "cannot open labels");
  std::string line;
  size_t n = 0;
  while (std::getline(nf, line)) {
    ++n;
    g.labels.push_back(integer(trim(line), np, n));
  }
  if (nf.bad())
    input_error(np, n, "read error");
  g.vertex_count = g.labels.size();
  std::ifstream ef(ep);
  if (!ef)
    input_error(ep, 0, "cannot open edges");
  n = 0;
  while (std::getline(ef, line)) {
    ++n;
    line = trim(line);
    if (line.empty())
      input_error(ep, n, "empty physical line");
    if (query && n % 2) {
      if (!integer_text(line))
        input_error(ep, n, "ignored line must contain one integer");
      continue;
    }
    Edge edge;
    size_t b = 0;
    while (true) {
      auto comma = line.find(',', b);
      auto part =
          trim(line.substr(b, comma == std::string::npos ? comma : comma - b));
      if (part.empty())
        input_error(ep, n, "empty comma field");
      std::istringstream tokens(part);
      std::string token;
      while (tokens >> token) {
        auto v = integer(token, ep, n);
        if (v < base || static_cast<uint64_t>(v - base) >= g.vertex_count)
          input_error(ep, n, "vertex ID outside label line range: " + token);
        edge.push_back(static_cast<size_t>(v - base));
      }
      if (comma == std::string::npos)
        break;
      b = comma + 1;
    }
    g.edges.push_back(std::move(edge));
    g.source_lines.push_back(n);
  }
  if (ef.bad())
    input_error(ep, n, "read error");
  if (query && n % 2)
    input_error(ep, n, "missing even-line hyperedge");
  return g;
}
} // namespace
Hypergraph read_data_hypergraph(const std::string &e, const std::string &n,
                                int b) {
  return read(e, n, b, false);
}
Hypergraph read_query_hypergraph(const std::string &e, const std::string &n,
                                 int b) {
  return read(e, n, b, true);
}
} // namespace hg
