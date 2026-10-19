#pragma once
#include <stdexcept>
#include <string>
namespace hg {
struct Error : std::runtime_error {
  std::string status;
  Error(std::string status, std::string message);
};
int exit_code(const std::string &status);
[[noreturn]] void input_error(const std::string &path, size_t line,
                              const std::string &text);
} // namespace hg
