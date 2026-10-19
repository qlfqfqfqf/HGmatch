#include "hgmatch/cli.hpp"
int main(int argc, char **argv) {
  return hg::run_cli<hg::Unlabeled>(argc, argv);
}
