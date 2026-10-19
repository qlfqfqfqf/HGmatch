#include "hgmatch/types.hpp"
#include "hgmatch/error.hpp"
#include <cstring>
namespace hg {
std::string decimal(Count v) {
  if (!v)
    return "0";
  std::string s;
  while (v) {
    s.push_back(char('0' + v % 10));
    v /= 10;
  }
  std::reverse(s.begin(), s.end());
  return s;
}
void add_count(Count &dst, Count x) {
  if (~Count(0) - dst < x)
    throw Error("count_overflow", "128-bit counter overflow");
  dst += x;
}
double seconds(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double>(b - a).count();
}
RunControl::RunControl(double s)
    : deadline(s > 0
                   ? Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                        std::chrono::duration<double>(s))
                   : Clock::time_point::max()),
      limited(s > 0) {}
void RunControl::fail(std::string_view s, std::string_view m) noexcept {
  cancelled.store(true, std::memory_order_release);
  int code = s == "timeout"          ? 1
             : s == "out_of_memory"  ? 2
             : s == "count_overflow" ? 3
                                     : 4;
  int expected = 0;
  if (failure.compare_exchange_strong(expected, code)) {
    const auto n = std::min(m.size(), sizeof(message) - 1);
    std::memcpy(message, m.data(), n);
    message[n] = '\0';
  }
}
const char *RunControl::status() const noexcept {
  switch (failure.load()) {
  case 0:
    return "ok";
  case 1:
    return "timeout";
  case 2:
    return "out_of_memory";
  case 3:
    return "count_overflow";
  default:
    return "internal_error";
  }
}
void RunControl::check() {
  if (cancelled.load(std::memory_order_relaxed))
    throw Error("cancelled", "execution cancelled");
  if (limited && Clock::now() >= deadline) {
    fail("timeout", "online query deadline exceeded");
    throw Error("timeout", "online query deadline exceeded");
  }
}
} // namespace hg
