#include "test_harness.hpp"
#include <iostream>
std::vector<Case> &cases() {
  static std::vector<Case> v;
  return v;
}
Register::Register(const char *g, const char *n, std::function<void()> f) {
  cases().push_back({g, n, f});
}
int main(int argc, char **argv) {
  std::string g = argc > 1 ? argv[1] : "", f = argc > 2 ? argv[2] : "";
  size_t n = 0;
  for (auto &c : cases()) {
    if ((!g.empty() && g != c.group) ||
        (!f.empty() && c.name.find(f) == std::string::npos))
      continue;
    try {
      c.run();
      std::cout << "PASS " << c.group << "/" << c.name << "\n";
      ++n;
    } catch (const std::exception &e) {
      std::cerr << "FAIL " << c.name << ": " << e.what() << "\n";
      return 1;
    }
  }
  if (!n) {
    std::cerr << "No test selected\n";
    return 2;
  }
  std::cout << n << " tests passed\n";
}
