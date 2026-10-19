#include "hgmatch/resources.hpp"
#include "hgmatch/error.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <sched.h>
#include <set>
#include <sstream>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
namespace hg {
size_t peak_rss_bytes() {
  rusage r{};
  if (getrusage(RUSAGE_SELF, &r))
    return 0;
  return size_t(r.ru_maxrss) * 1024;
}
size_t current_rss_bytes() {
  std::ifstream f("/proc/self/statm");
  size_t pages = 0, resident = 0;
  f >> pages >> resident;
  return resident * size_t(sysconf(_SC_PAGESIZE));
}
double thread_cpu_time() {
  timespec t{};
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}
std::string cpu_model() {
  std::ifstream f("/proc/cpuinfo");
  std::string s;
  while (std::getline(f, s))
    if (s.rfind("model name", 0) == 0)
      return s.substr(s.find(':') + 2);
  return "unknown";
}
size_t physical_core_count() {
  std::ifstream f("/proc/cpuinfo");
  std::set<std::pair<std::string, std::string>> cores;
  std::string l, socket, core;
  while (std::getline(f, l)) {
    if (l.rfind("physical id", 0) == 0)
      socket = l.substr(l.find(':') + 1);
    if (l.rfind("core id", 0) == 0)
      core = l.substr(l.find(':') + 1);
    if (l.empty() && !core.empty()) {
      cores.emplace(socket, core);
      core.clear();
    }
  }
  if (!core.empty())
    cores.emplace(socket, core);
  return cores.size();
}
std::string affinity_cpus() {
  cpu_set_t s;
  CPU_ZERO(&s);
  if (sched_getaffinity(0, sizeof(s), &s))
    return "unknown";
  std::string out;
  for (int i = 0; i < CPU_SETSIZE; ++i)
    if (CPU_ISSET(i, &s)) {
      if (!out.empty())
        out += ',';
      out += std::to_string(i);
    }
  return out;
}
std::string os_description() {
  utsname u{};
  uname(&u);
  return std::string(u.sysname) + " " + u.release + " " + u.machine;
}
namespace {
// FIPS 180-4 SHA-256. Streaming input; independent known-vector and attachment
// tests.
struct SHA256 {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::array<unsigned char, 64> block{};
  size_t used = 0;
  uint64_t length = 0;
  static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
  void compress() {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t(block[4 * i]) << 24) |
             (uint32_t(block[4 * i + 1]) << 16) |
             (uint32_t(block[4 * i + 2]) << 8) | block[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
      auto a = w[i - 15], b = w[i - 2];
      w[i] = w[i - 16] + (rotr(a, 7) ^ rotr(a, 18) ^ (a >> 3)) + w[i - 7] +
             (rotr(b, 17) ^ rotr(b, 19) ^ (b >> 10));
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5],
             g = h[6], z = h[7];
    for (int i = 0; i < 64; ++i) {
      auto t1 = z + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) +
                ((e & f) ^ (~e & g)) + k[i] + w[i];
      auto t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) +
                ((a & b) ^ (a & c) ^ (b & c));
      z = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += z;
  }
  void feed(unsigned char c) {
    block[used++] = c;
    ++length;
    if (used == 64) {
      compress();
      used = 0;
    }
  }
  std::string finish() {
    auto bits = length * 8;
    feed(0x80);
    while (used != 56)
      feed(0);
    for (int i = 7; i >= 0; --i)
      feed(static_cast<unsigned char>(bits >> (i * 8)));
    std::ostringstream s;
    s << std::hex << std::setfill('0');
    for (auto x : h)
      s << std::setw(8) << x;
    return s.str();
  }
};
} // namespace
std::string sha256_text(const std::string &s) {
  SHA256 h;
  for (unsigned char c : s)
    h.feed(c);
  return h.finish();
}
std::string sha256_file(const std::string &p) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    input_error(p, 0, "cannot hash input");
  SHA256 h;
  std::array<char, 65536> b;
  while (f) {
    f.read(b.data(), b.size());
    for (std::streamsize i = 0; i < f.gcount(); ++i)
      h.feed(static_cast<unsigned char>(b[i]));
  }
  if (f.bad())
    input_error(p, 0, "hash read failed");
  return h.finish();
}
} // namespace hg
