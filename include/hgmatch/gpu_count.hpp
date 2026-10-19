#pragma once
#include <cstdint>
#ifdef __CUDACC__
#define HG_COUNT_HD __host__ __device__
#else
#define HG_COUNT_HD
#endif
namespace hg {
struct WideCount { unsigned long long lo = 0, hi = 0; };
// Each GPU counter has one reducer/writer; no torn multiword atomics.
HG_COUNT_HD inline void wide_add(WideCount &a, unsigned long long n, int *overflow) {
  auto next = a.lo + n;
  bool carry = next < a.lo;
  if (carry && a.hi == ~0ull) { *overflow = 1; return; }
  a.lo = next;
  if (carry) ++a.hi;
}
} // namespace hg
#undef HG_COUNT_HD
