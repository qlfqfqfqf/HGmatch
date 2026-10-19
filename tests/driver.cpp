// Test-only tuple collector. Never installed in build/bin or used for timings.
#include "hgmatch/cli.hpp"
#include <string>
int main(int argc, char **argv) {
  if (argc < 2)
    return 2;
  bool l = std::string(argv[1]) == "labeled";
  return l ? hg::run_cli<hg::Labeled>(argc - 1, argv + 1, true)
           : hg::run_cli<hg::Unlabeled>(argc - 1, argv + 1, true);
}
