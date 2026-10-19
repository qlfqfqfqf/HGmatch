#include "hgmatch/error.hpp"
namespace hg {
Error::Error(std::string s, std::string m)
    : std::runtime_error(m), status(std::move(s)) {}
int exit_code(const std::string &s) {
  return s == "ok" ? 0 : s == "input_error" ? 2 : s == "timeout" ? 124 : 1;
}
void input_error(const std::string &p, size_t n, const std::string &t) {
  throw Error("input_error", p + ":" + std::to_string(n) + ": " + t);
}
} // namespace hg
