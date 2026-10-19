#include "hgmatch/input.hpp"
#include "hgmatch/normalize.hpp"
#include "support/test_harness.hpp"
#include <filesystem>
#include <fstream>
using namespace hg;
TEST(input_contract, paired_singleton) {
  auto dir = std::filesystem::temp_directory_path() / "hgmatch_input_contract";
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "n") << "1\n2\n";
  std::ofstream(dir / "e") << "-999999999999999999999999999999\n0\n70\n0,1\n";
  auto g = read_query_hypergraph((dir / "e").string(), (dir / "n").string(), 0);
  normalize(g);
  CHECK(g.edges.size() == 2);
  CHECK(g.edges[0] == Edge{0});
  validate_query(g);
  std::filesystem::remove_all(dir);
}
