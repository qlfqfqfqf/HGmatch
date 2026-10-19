#pragma once
#include "types.hpp"
namespace hg {
size_t peak_rss_bytes();
size_t current_rss_bytes();
double thread_cpu_time();
std::string sha256_file(const std::string &);
std::string sha256_text(const std::string &);
std::string cpu_model();
size_t physical_core_count();
std::string affinity_cpus();
std::string os_description();
} // namespace hg
