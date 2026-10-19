#pragma once
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
struct Case {
  std::string group, name;
  std::function<void()> run;
};
std::vector<Case> &cases();
struct Register {
  Register(const char *g, const char *n, std::function<void()> f);
};
#define TEST(G, N)                                                             \
  void G##_##N();                                                              \
  static Register reg_##G##_##N(#G, #N, G##_##N);                              \
  void G##_##N()
#define CHECK(X)                                                               \
  do {                                                                         \
    if (!(X))                                                                  \
      throw std::runtime_error(std::string(__FILE__) + ":" +                   \
                               std::to_string(__LINE__) + ": " #X);            \
  } while (false)
