#include "hgmatch/cli.hpp"
#include "build_config.hpp"
#include "hgmatch/error.hpp"
#include "hgmatch/input.hpp"
#include "hgmatch/inverted_index.hpp"
#include "hgmatch/metrics.hpp"
#include "hgmatch/normalize.hpp"
#include "hgmatch/plan.hpp"
#include "hgmatch/resources.hpp"
#include "hgmatch/gpu.hpp"
#include "hgmatch/storage.hpp"
#include <cmath>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
namespace hg {
namespace {
void help() {
  std::cout << "HGMatch GPU (C++20 + CUDA; CPU preprocessing)\nRequired: --data-edges FILE "
               "--data-nodes FILE --query-edges FILE --query-nodes FILE\n"
               "Defaults: --data-id-base 1 --query-id-base 0 --gpu-id 0 "
               "--timeout-seconds 3600\n"
               "--gpu-memory-mb 0 (80% of free GPU memory) "
               "--batch-capacity 65536 --candidate-tile-size 65536\n"
               "Algorithm 4/5 times always use CUDA events; no CPU mining fallback.\n"
               "--metrics-file FILE (optional JSON copy; full JSON always on "
               "stdout)\n"
               "patternEdge: odd integer line ignored; even line is an edge "
               "(one vertex allowed).\n"
               "--timeout-seconds 0 disables deadline. Query must be "
               "connected, nonempty, without isolated vertices.\n";
}
uint64_t natural(const std::string &s) {
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
    throw Error("input_error", "expected nonnegative integer: " + s);
  try {
    return std::stoull(s);
  } catch (...) {
    throw Error("input_error", "integer out of range: " + s);
  }
}
void write_json(const std::string &name, const Json &r) {
  std::filesystem::path p(name);
  if (p.has_parent_path())
    std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p);
  if (!f)
    throw Error("internal_error", "cannot open output: " + name);
  f << r.dump(2) << '\n';
  f.close();
  if (!f)
    throw Error("internal_error", "cannot write output: " + name);
}
} // namespace
template <class M> int run_cli(int argc, char **argv, bool test_entry) {
  auto start = Clock::now();
  Json report = {{"schema_version", 4},
                 {"system", "HGMatch-GPU"},
                 {"backend", "cuda"},
                 {"timing_method", "cuda_events_single_stream"},
                 {"timing_complete", false},
                 {"label_mode", M::labeled ? "labeled" : "unlabeled"},
                 {"count_semantics", "ordered_hyperedge_embeddings"},
                 {"status", "internal_error"},
                 {"message", ""},
                 {"embedding_count", nullptr},
                 {"partial_embedding_count", nullptr}};
  std::string metrics, collect;
  for (auto key :
       {"data_load_time_s", "data_normalize_time_s", "partition_build_time_s",
        "index_build_time_s", "preprocessing_time_s", "query_load_time_s",
        "plan_time_s", "matching_time_s", "online_query_time_s",
        "candidate_generation_time_s", "embedding_validation_time_s",
        "cuda_initialize_time_s", "device_layout_time_s", "host_to_device_time_s"})
    report[key] = nullptr;
  try {
    std::map<std::string, std::string> a = {{"--gpu-id", "0"},
                                            {"--data-id-base", "1"},
                                            {"--query-id-base", "0"},
                                            {"--timeout-seconds", "3600"},
                                            {"--gpu-memory-mb", "0"},
                                            {"--batch-capacity", "65536"},
                                            {"--candidate-tile-size", "65536"}};
    std::set<std::string> accepted = {"--data-edges", "--data-nodes",
                                      "--query-edges", "--query-nodes",
                                      "--metrics-file"};
    for (auto &[k, v] : a)
      accepted.insert(k);
    if (test_entry)
      accepted.insert("--collect-file");
    std::set<std::string> seen;
    for (int i = 1; i < argc; ++i) {
      std::string k = argv[i];
      if (k == "--help") {
        help();
        return 0;
      }
      if (!accepted.count(k) || !seen.insert(k).second || i + 1 == argc)
        throw Error("input_error",
                    "unknown, duplicate or valueless argument: " + k);
      a[k] = argv[++i];
    }
    metrics = a["--metrics-file"];
    collect = a["--collect-file"];
    for (auto k :
         {"--data-edges", "--data-nodes", "--query-edges", "--query-nodes"})
      if (a[k].empty())
        throw Error("input_error", std::string("missing required ") + k);
    RunOptions o;
    auto gpu_id = natural(a["--gpu-id"]);
    if (gpu_id > INT_MAX) throw Error("input_error", "gpu-id is too large");
    o.gpu_id = int(gpu_id);
    o.gpu_memory_mb = natural(a["--gpu-memory-mb"]);
    o.batch_capacity = natural(a["--batch-capacity"]);
    o.candidate_tile_size = natural(a["--candidate-tile-size"]);
    if (!o.batch_capacity || o.batch_capacity > INT_MAX || !o.candidate_tile_size)
      throw Error("input_error", "batch-capacity must be 1..INT_MAX and candidate-tile-size positive");
    auto db = natural(a["--data-id-base"]), qb = natural(a["--query-id-base"]);
    if (db > 1 || qb > 1)
      throw Error("input_error", "ID bases must be 0 or 1");
    double timeout;
    try {
      size_t pos;
      timeout = std::stod(a["--timeout-seconds"], &pos);
      if (pos != a["--timeout-seconds"].size() || !std::isfinite(timeout) ||
          timeout < 0 || timeout > 1e9)
        throw std::invalid_argument("time");
    } catch (...) {
      throw Error("input_error",
                  "timeout must be finite, nonnegative, at most 1e9 seconds");
    }
    report["gpu_id"] = o.gpu_id;
    report["gpu_memory_mb"] = o.gpu_memory_mb;
    report["batch_capacity"] = o.batch_capacity;
    report["candidate_tile_size"] = o.candidate_tile_size;
    report["timeout_seconds"] = timeout;
    report["data_id_base"] = db;
    report["query_id_base"] = qb;
    report["query_input_protocol"] = "odd_integer_ignored_even_hyperedge";
    report["normalization"] = "deduplicate_vertex_ids_then_exact_edges";
    auto t = Clock::now(), offline = t;
    auto raw = read_data_hypergraph(a["--data-edges"], a["--data-nodes"], db);
    report["data_load_time_s"] = seconds(t, Clock::now());
    t = Clock::now();
    normalize(raw);
    report["data_normalize_time_s"] = seconds(t, Clock::now());
    report["data"] = graph_stats(raw.stats, raw.vertex_count);
    {
      std::set<Label> label_types(raw.labels.begin(), raw.labels.end());
      report["data"]["input_label_count"] = label_types.size();
      report["data"]["matching_label_count"] =
          M::labeled ? Json(label_types.size()) : Json(nullptr);
    }
    t = Clock::now();
    auto h = prepare<M>(std::move(raw));
    build_partitions(h);
    report["partition_build_time_s"] = seconds(t, Clock::now());
    t = Clock::now();
    build_index(h);
    report["index_build_time_s"] = seconds(t, Clock::now());
    report["preprocessing_time_s"] = seconds(offline, Clock::now());
    report["data"]["partitions"] = h.parts.size();
    report.update(memory_stats(h));
    report["rss_after_index_bytes"] = current_rss_bytes();
    report["inputs"] = Json::object();
    for (auto key :
         {"--data-edges", "--data-nodes", "--query-edges", "--query-nodes"})
      report["inputs"][std::string(key).substr(2)] = {
          {"path", a[key]}, {"sha256", sha256_file(a[key])}};
    t = Clock::now();
    auto qr = read_query_hypergraph(a["--query-edges"], a["--query-nodes"], qb);
    normalize(qr);
    validate_query(qr);
    report["query"] = graph_stats(qr.stats, qr.vertex_count);
    auto q = prepare<M>(std::move(qr));
    report["query_load_time_s"] = seconds(t, Clock::now());
    RunControl control(timeout);
    t = Clock::now();
    QueryPlan plan;
    try {
      plan = compute_order(q, h, control);
    } catch (...) {
      report["plan_time_s"] = seconds(t, Clock::now());
      report["matching_time_s"] = 0.0;
      report["candidate_generation_time_s"] = 0.0;
      report["embedding_validation_time_s"] = 0.0;
      report["online_query_time_s"] = report["plan_time_s"];
      throw;
    }
    double pt = seconds(t, Clock::now());
    report["plan_time_s"] = pt;
    std::vector<std::vector<size_t>> tuples;
    Observer observer;
    if (test_entry && !collect.empty())
      observer = [&](const Embedding &e) {
        std::vector<size_t> row(e.edges.size());
        for (size_t i = 0; i < row.size(); ++i)
          row[plan.order[i]] = e.edges[i];
        tuples.push_back(std::move(row));
      };
    auto result = run_gpu(h, q, plan, o, control, observer);
    report["matching_order"] = plan.order;
    report["query_cardinalities"] = std::vector<size_t>(q.edges.size(), 0);
    for (size_t i = 0; i < plan.order.size(); ++i)
      report["query_cardinalities"][plan.order[i]] =
          plan.partition_ids[i] == missing
              ? 0
              : h.parts[plan.partition_ids[i]].edges.size();
    report.update(execution_json(result, o));
    report["online_query_time_s"] = pt + result.matching_time_s;
    if (test_entry && !collect.empty()) {
      std::sort(tuples.begin(), tuples.end());
      write_json(collect, tuples);
    }
  } catch (const Error &e) {
    report["status"] = e.status;
    report["message"] = e.what();
    report["embedding_count"] = nullptr;
    if (e.status == "timeout" && report["partial_embedding_count"].is_null())
      report["partial_embedding_count"] = "0";
  } catch (const std::bad_alloc &) {
    report["status"] = "out_of_memory";
    report["message"] = "allocation failed";
    report["embedding_count"] = nullptr;
  } catch (const std::exception &e) {
    report["status"] = "internal_error";
    report["message"] = e.what();
    report["embedding_count"] = nullptr;
  }
  report["peak_rss_bytes"] = peak_rss_bytes();
  report["environment"] = {
      {"os", os_description()},
      {"cpu_model", cpu_model()},
      {"physical_cores", physical_core_count()},
      {"logical_cpus", std::thread::hardware_concurrency()},
      {"affinity_cpus", affinity_cpus()},
      {"compiler", HGMATCH_COMPILER},
      {"cuda_compiler", HGMATCH_CUDA_COMPILER},
      {"cuda_arch", HGMATCH_CUDA_ARCH},
      {"build_flags", HGMATCH_BUILD_FLAGS},
      {"source_sha256", HGMATCH_SOURCE_SHA256},
      {"dependency_sha256", HGMATCH_DEPENDENCY_SHA256}};
  report["end_to_end_time_s"] = seconds(start, Clock::now());
  try {
    if (!metrics.empty())
      write_json(metrics, report);
  } catch (const std::exception &e) {
    report["status"] = "internal_error";
    report["message"] = e.what();
    report["embedding_count"] = nullptr;
  }
  std::cout << report.dump(2) << '\n';
  return exit_code(report["status"].get<std::string>());
}
template int run_cli<Labeled>(int, char **, bool);
template int run_cli<Unlabeled>(int, char **, bool);
} // namespace hg
