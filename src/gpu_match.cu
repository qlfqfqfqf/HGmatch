#include "hgmatch/gpu.hpp"
#include "hgmatch/error.hpp"
#include "hgmatch/gpu_count.hpp"
#include <cuda_runtime.h>
#include <cub/cub.cuh>
#include <thrust/iterator/counting_iterator.h>
#include <limits>
#include <climits>
#include <sstream>
namespace hg {
namespace {
constexpr unsigned block_size = 256;
void cuda_check(cudaError_t e, const char *what) {
  if (e == cudaSuccess) return;
  const char *status = e == cudaErrorMemoryAllocation ? "out_of_memory" :
    (e == cudaErrorNoDevice || e == cudaErrorInsufficientDriver) ? "gpu_unavailable" : "cuda_error";
  throw Error(status, std::string(what) + ": " + cudaGetErrorString(e));
}
struct Budget {
  size_t limit, used = 0, peak = 0;
};
template <class T> class Buffer {
  T *data_ = nullptr;
  size_t bytes_ = 0;
  Budget *budget_ = nullptr;
public:
  Buffer() = default;
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;
  Buffer(Buffer &&o) noexcept : data_(o.data_), bytes_(o.bytes_), budget_(o.budget_) {
    o.data_ = nullptr; o.bytes_ = 0;
  }
  ~Buffer() { if (data_) { cudaFree(data_); budget_->used -= bytes_; } }
  void allocate(size_t n, Budget &b) {
    if (!n) return;
    if (n > std::numeric_limits<size_t>::max() / sizeof(T) ||
        n * sizeof(T) > b.limit - b.used)
      throw Error("out_of_memory", "GPU memory budget exhausted");
    cuda_check(cudaMalloc(reinterpret_cast<void **>(&data_), n * sizeof(T)), "cudaMalloc");
    bytes_ = n * sizeof(T); budget_ = &b;
    b.used += bytes_; b.peak = std::max(b.peak, b.used);
  }
  void copy(const std::vector<T> &v, cudaStream_t s) {
    if (!v.empty()) cuda_check(cudaMemcpyAsync(data_, v.data(), bytes_, cudaMemcpyHostToDevice, s), "upload");
  }
  T *get() const { return data_; }
};
struct Stream {
  cudaStream_t value = nullptr;
  Stream() { cuda_check(cudaStreamCreateWithFlags(&value, cudaStreamNonBlocking), "create stream"); }
  ~Stream() { if (value) cudaStreamDestroy(value); }
};
class PhaseTimer {
  cudaEvent_t begin_ = nullptr, end_ = nullptr;
public:
  PhaseTimer() {
    cuda_check(cudaEventCreate(&begin_), "create timing event");
    auto e = cudaEventCreate(&end_);
    if (e != cudaSuccess) { cudaEventDestroy(begin_); begin_ = nullptr; cuda_check(e, "create timing event"); }
  }
  ~PhaseTimer() { if (begin_) cudaEventDestroy(begin_); if (end_) cudaEventDestroy(end_); }
  void begin(cudaStream_t s) { cuda_check(cudaEventRecord(begin_, s), "record algorithm start"); }
  void finish(cudaStream_t s, double &total) {
    cuda_check(cudaEventRecord(end_, s), "record algorithm stop");
    cuda_check(cudaEventSynchronize(end_), "wait algorithm");
    float ms = 0;
    cuda_check(cudaEventElapsedTime(&ms, begin_, end_), "algorithm elapsed time");
    total += double(ms) / 1000.0;
  }
};
struct DeviceInput {
  const GpuId *edge_offsets, *vertices, *partition_offsets, *partition_edges,
      *posting_partitions, *posting_vertices, *posting_offsets, *posting_edges,
      *partition_ids, *prefix_vertex_counts, *constraint_offsets, *profile_offsets,
      *profile_masks, *profile_counts;
  const Label *labels, *profile_labels;
  const CandidateConstraint *constraints;
  const uint8_t *forbidden;
  GpuId posting_keys, query_edges, profile_words;
};
#define GPU_ID_ARRAYS(X) X(edge_offsets) X(vertices) X(partition_offsets) X(partition_edges) \
  X(posting_partitions) X(posting_vertices) X(posting_offsets) X(posting_edges) \
  X(partition_ids) X(prefix_vertex_counts) X(constraint_offsets) X(profile_offsets) \
  X(profile_masks) X(profile_counts)
struct Uploaded {
#define DECLARE(n) Buffer<GpuId> n;
  GPU_ID_ARRAYS(DECLARE)
#undef DECLARE
  Buffer<Label> labels, profile_labels;
  Buffer<CandidateConstraint> constraints;
  Buffer<uint8_t> forbidden;
  Uploaded(const FlatGpuInput &f, Budget &b) {
#define ALLOCATE(n) n.allocate(f.n.size(), b);
    GPU_ID_ARRAYS(ALLOCATE)
#undef ALLOCATE
    labels.allocate(f.labels.size(), b); profile_labels.allocate(f.profile_labels.size(), b);
    constraints.allocate(f.constraints.size(), b); forbidden.allocate(f.forbidden.size(), b);
  }
  void upload(const FlatGpuInput &f, cudaStream_t s) {
#define COPY(n) n.copy(f.n, s);
    GPU_ID_ARRAYS(COPY)
#undef COPY
    labels.copy(f.labels, s); profile_labels.copy(f.profile_labels, s);
    constraints.copy(f.constraints, s); forbidden.copy(f.forbidden, s);
  }
  DeviceInput view(const FlatGpuInput &f) const {
    return {
      edge_offsets.get(), vertices.get(), partition_offsets.get(), partition_edges.get(),
      posting_partitions.get(), posting_vertices.get(), posting_offsets.get(), posting_edges.get(),
      partition_ids.get(), prefix_vertex_counts.get(), constraint_offsets.get(), profile_offsets.get(),
      profile_masks.get(), profile_counts.get(), labels.get(), profile_labels.get(),
      constraints.get(), forbidden.get(), f.posting_vertices.size(), f.query_edges, f.profile_words};
  }
};
struct DeviceDepth { WideCount candidates, vertex_pass, valid; };
struct DeviceTotals { WideCount roots, embeddings; };
__device__ GpuId lower(const GpuId *x, GpuId a, GpuId b, GpuId v) {
  while (a < b) { GpuId m = a + (b - a) / 2; if (x[m] < v) a = m + 1; else b = m; }
  return a;
}
__device__ bool contains(DeviceInput h, GpuId e, GpuId v) {
  auto end = h.edge_offsets[e + 1];
  auto i = lower(h.vertices, h.edge_offsets[e], end, v);
  return i < end && h.vertices[i] == v;
}
__device__ GpuId posting_key(DeviceInput h, GpuId pid, GpuId v) {
  GpuId a = 0, b = h.posting_keys;
  while (a < b) {
    auto m = a + (b - a) / 2;
    if (h.posting_partitions[m] < pid || (h.posting_partitions[m] == pid && h.posting_vertices[m] < v)) a = m + 1;
    else b = m;
  }
  return a < h.posting_keys && h.posting_partitions[a] == pid && h.posting_vertices[a] == v ? a : ~GpuId(0);
}
template <bool Labeled>
__device__ bool allowed(DeviceInput h, const GpuId *parent, GpuId depth,
                        CandidateConstraint c, GpuId v) {
  if constexpr (Labeled) if (h.labels[v] != c.label) return false;
  GpuId degree = 0;
  for (GpuId j = 0; j < depth; ++j) {
    bool incident = contains(h, parent[j], v);
    if (incident && h.forbidden[depth * h.query_edges + j]) return false;
    degree += incident;
  }
  return degree == c.degree;
}
__global__ void roots_kernel(DeviceInput h, GpuId *out, GpuId first, GpuId n) {
  for (GpuId i = blockIdx.x * blockDim.x + threadIdx.x; i < n; i += GpuId(gridDim.x) * blockDim.x)
    out[i] = h.partition_edges[h.partition_offsets[h.partition_ids[0]] + first + i];
}
__global__ void root_count_kernel(DeviceTotals *totals, GpuId n, bool single, int *overflow) {
  wide_add(totals->roots, n, overflow);
  if (single) wide_add(totals->embeddings, n, overflow);
}
// Seed union from the first constraint's postings, within a disjoint edge-ID tile.
template <bool Labeled>
__global__ void seed_union_kernel(DeviceInput h, const GpuId *parents, GpuId depth,
                                 GpuId first, GpuId width, unsigned *flags) {
  auto row = GpuId(blockIdx.x); const auto *parent = parents + row * depth;
  auto c = h.constraints[h.constraint_offsets[depth]];
  auto e = parent[c.old_position], pid = h.partition_ids[depth];
  for (GpuId i = h.edge_offsets[e] + threadIdx.x; i < h.edge_offsets[e + 1]; i += blockDim.x) {
    auto v = h.vertices[i]; if (!allowed<Labeled>(h, parent, depth, c, v)) continue;
    auto key = posting_key(h, pid, v); if (key == ~GpuId(0)) continue;
    auto end = h.posting_offsets[key + 1];
    auto pos = lower(h.posting_edges, h.posting_offsets[key], end, first);
    for (; pos < end && h.posting_edges[pos] < first + width; ++pos)
      atomicExch(flags + row * width + h.posting_edges[pos] - first, 1u);
  }
}
// Intersect the remaining unions; no extra pruning beyond Algorithm 4.
template <bool Labeled>
__global__ void intersection_kernel(DeviceInput h, const GpuId *parents, GpuId depth,
                                    GpuId first, GpuId width, GpuId slots, unsigned *flags) {
  for (GpuId i = blockIdx.x * blockDim.x + threadIdx.x; i < slots; i += GpuId(gridDim.x) * blockDim.x) {
    if (!flags[i]) continue;
    const auto *parent = parents + (i / width) * depth;
    auto candidate = first + i % width, pid = h.partition_ids[depth];
    for (auto g = h.constraint_offsets[depth] + 1; g < h.constraint_offsets[depth + 1]; ++g) {
      auto c = h.constraints[g]; auto e = parent[c.old_position]; bool member = false;
      for (GpuId pos = h.edge_offsets[e]; pos < h.edge_offsets[e + 1] && !member; ++pos) {
        auto v = h.vertices[pos]; if (!allowed<Labeled>(h, parent, depth, c, v)) continue;
        auto key = posting_key(h, pid, v); if (key == ~GpuId(0)) continue;
        auto end = h.posting_offsets[key + 1];
        auto at = lower(h.posting_edges, h.posting_offsets[key], end, candidate);
        member = at < end && h.posting_edges[at] == candidate;
      }
      if (!member) { flags[i] = 0; break; }
    }
  }
}
__device__ GpuId candidate_edge(DeviceInput h, GpuId depth, GpuId first, GpuId width, GpuId id) {
  return h.partition_edges[h.partition_offsets[h.partition_ids[depth]] + first + id % width];
}
__device__ unsigned long long block_sum(unsigned long long v, unsigned long long *scratch) {
  scratch[threadIdx.x] = v; __syncthreads();
  for (unsigned stride = block_size / 2; stride; stride /= 2) {
    if (threadIdx.x < stride) scratch[threadIdx.x] += scratch[threadIdx.x + stride];
    __syncthreads();
  }
  return scratch[0];
}
__global__ void vertex_count_kernel(DeviceInput h, const GpuId *parents, GpuId depth,
                                   GpuId first, GpuId width, const GpuId *ids,
                                   unsigned *pass) {
  auto row = GpuId(blockIdx.x), id = ids[row];
  const auto *parent = parents + (id / width) * depth;
  auto candidate = candidate_edge(h, depth, first, width, id);
  unsigned long long unique = 0;
  for (GpuId j = 0; j <= depth; ++j) {
    auto e = j == depth ? candidate : parent[j];
    for (auto pos = h.edge_offsets[e] + threadIdx.x; pos < h.edge_offsets[e + 1]; pos += blockDim.x) {
      auto v = h.vertices[pos]; bool prior = false;
      for (GpuId a = 0; a < j && !prior; ++a) prior = contains(h, parent[a], v);
      unique += !prior;
    }
  }
  __shared__ unsigned long long scratch[block_size];
  auto total = block_sum(unique, scratch);
  if (threadIdx.x == 0) pass[row] = total == h.prefix_vertex_counts[depth];
}
// Dynamic word count supports arbitrarily long query prefixes (including >64).
template <bool Labeled>
__device__ bool same_profile(DeviceInput h, const GpuId *parent, GpuId depth,
                            GpuId candidate, GpuId v, GpuId group) {
  if constexpr (Labeled) if (h.labels[v] != h.profile_labels[group]) return false;
  for (GpuId word = 0; word < (depth + 64) / 64; ++word) {
    GpuId mask = 0;
    for (GpuId j = word * 64; j <= depth && j < (word + 1) * 64; ++j)
      if (contains(h, j == depth ? candidate : parent[j], v)) mask |= GpuId(1) << (j % 64);
    if (mask != h.profile_masks[group * h.profile_words + word]) return false;
  }
  return true;
}
template <bool Labeled>
__global__ void profile_kernel(DeviceInput h, const GpuId *parents, GpuId depth,
                              GpuId first, GpuId width, const GpuId *ids,
                              const unsigned *vertex_pass, unsigned *valid) {
  auto row = GpuId(blockIdx.x);
  if (!vertex_pass[row]) { if (threadIdx.x == 0) valid[row] = 0; return; }
  auto id = ids[row]; const auto *parent = parents + (id / width) * depth;
  auto candidate = candidate_edge(h, depth, first, width, id);
  __shared__ unsigned long long scratch[block_size];
  bool equal = true;
  for (auto group = h.profile_offsets[depth]; group < h.profile_offsets[depth + 1]; ++group) {
    unsigned long long count = 0;
    for (auto pos = h.edge_offsets[candidate] + threadIdx.x; pos < h.edge_offsets[candidate + 1]; pos += blockDim.x)
      count += same_profile<Labeled>(h, parent, depth, candidate, h.vertices[pos], group);
    auto total = block_sum(count, scratch);
    __syncthreads(); // All threads consume the reduction before scratch is reused.
    if (total != h.profile_counts[group]) { equal = false; break; }
    // All threads saw the same reduction result, so the loop exits uniformly.
  }
  if (threadIdx.x == 0) valid[row] = equal;
}
__global__ void candidate_count_kernel(DeviceDepth *stats, GpuId depth, GpuId n, int *overflow) {
  wide_add(stats[depth - 1].candidates, n, overflow);
}
__global__ void result_count_kernel(DeviceDepth *stats, DeviceTotals *totals, GpuId depth,
                                  const unsigned long long *f, const int *v, bool leaf, int *overflow) {
  wide_add(stats[depth - 1].vertex_pass, *f, overflow);
  wide_add(stats[depth - 1].valid, *v, overflow);
  if (leaf) wide_add(totals->embeddings, *v, overflow);
}
__global__ void expand_kernel(DeviceInput h, const GpuId *parents, GpuId depth,
                             GpuId first, GpuId width, const GpuId *ids, GpuId n,
                             GpuId *out) {
  for (GpuId i = blockIdx.x * blockDim.x + threadIdx.x; i < n * (depth + 1); i += GpuId(gridDim.x) * blockDim.x) {
    auto row = i / (depth + 1), col = i % (depth + 1), id = ids[row];
    out[i] = col == depth ? candidate_edge(h, depth, first, width, id) : parents[(id / width) * depth + col];
  }
}
unsigned grid_for(size_t n) { return unsigned(std::min<size_t>(65535, (n + block_size - 1) / block_size)); }
void launch_check() { cuda_check(cudaGetLastError(), "kernel launch"); }
template <class T> T read_scalar(const T *p, cudaStream_t s) {
  T out{}; cuda_check(cudaMemcpyAsync(&out, p, sizeof(T), cudaMemcpyDeviceToHost, s), "read control scalar");
  cuda_check(cudaStreamSynchronize(s), "wait control scalar"); return out;
}
Count host_count(WideCount v) { return (Count(v.hi) << 64) | Count(v.lo); }
struct Frame { size_t rows, parent = 0, tile = 0; };
} // namespace

template <class M>
ExecutionReport run_gpu(const IndexedGraph<M> &h, const IndexedGraph<M> &q,
                        const QueryPlan &plan, const RunOptions &o, RunControl &control,
                        const Observer &observe) {
  ExecutionReport r; r.gpu_id = o.gpu_id; r.depth.resize(plan.order.size() - 1);
  auto init = Clock::now();
  bool started = false; Clock::time_point matching_start{};
  try {
    int devices = 0; cuda_check(cudaGetDeviceCount(&devices), "get CUDA devices");
    if (!devices) throw Error("gpu_unavailable", "no CUDA device; CPU mining is not available");
    if (o.gpu_id < 0 || o.gpu_id >= devices) throw Error("input_error", "gpu-id is outside the available device range");
    cuda_check(cudaSetDevice(o.gpu_id), "select GPU");
    cuda_check(cudaFree(nullptr), "initialize CUDA context");
    cudaDeviceProp props{}; cuda_check(cudaGetDeviceProperties(&props, o.gpu_id), "GPU properties");
    r.gpu_name = props.name; r.compute_major = props.major; r.compute_minor = props.minor;
    cuda_check(cudaRuntimeGetVersion(&r.cuda_runtime_version), "runtime version");
    cuda_check(cudaDriverGetVersion(&r.cuda_driver_version), "driver version");
    cuda_check(cudaMemGetInfo(&r.device_free_bytes, &r.device_total_bytes), "GPU free memory");
    if (o.gpu_memory_mb && o.gpu_memory_mb > std::numeric_limits<size_t>::max() / (1024 * 1024))
      throw Error("input_error", "gpu-memory-mb is too large");
    r.device_budget_bytes = o.gpu_memory_mb ? o.gpu_memory_mb * size_t(1024 * 1024) : r.device_free_bytes / 5 * 4;
    if (r.device_budget_bytes > r.device_free_bytes) throw Error("out_of_memory", "requested GPU budget exceeds current free memory");
    Budget budget{r.device_budget_bytes}; Stream stream; PhaseTimer timer;
    r.cuda_initialize_time_s = seconds(init, Clock::now());
    auto layout_start = Clock::now();
    auto flat = flatten_for_gpu(h, q, plan);
    r.device_layout_time_s = seconds(layout_start, Clock::now());
    Uploaded uploaded(flat, budget); auto view = uploaded.view(flat);
    auto upload_start = Clock::now(); uploaded.upload(flat, stream.value);
    cuda_check(cudaStreamSynchronize(stream.value), "wait upload");
    r.host_to_device_time_s = seconds(upload_start, Clock::now());
    const auto k = plan.order.size();
    if (!o.batch_capacity || o.batch_capacity > INT_MAX || !o.candidate_tile_size)
      throw Error("input_error", "invalid GPU batch capacity or candidate tile size");
    size_t capacity = o.batch_capacity, temp_bytes = 0;
    thrust::counting_iterator<GpuId> sequence(0);
    for (;;) {
      size_t select_bytes = 0, reduce_bytes = 0;
      cuda_check(cub::DeviceSelect::Flagged(nullptr, select_bytes, sequence, (unsigned *)nullptr,
                 (GpuId *)nullptr, (int *)nullptr, int(capacity), stream.value), "size candidate compaction");
      cuda_check(cub::DeviceReduce::Sum(nullptr, reduce_bytes, (unsigned *)nullptr,
                 (unsigned long long *)nullptr, int(capacity), stream.value), "size vertex reduction");
      temp_bytes = std::max(select_bytes, reduce_bytes);
      Count cells = Count(k) * (k + 1) / 2;
      Count need = (cells * sizeof(GpuId) + 2 * sizeof(GpuId) + 3 * sizeof(unsigned)) * capacity +
                   temp_bytes + (k - 1) * sizeof(DeviceDepth) + sizeof(DeviceTotals) + sizeof(unsigned long long) + 3 * sizeof(int);
      if (need <= budget.limit - budget.used) break;
      if (capacity == 1) throw Error("out_of_memory", "index fits but minimum GPU traversal workspace does not fit");
      capacity = std::max<size_t>(1, capacity / 2);
    }
    r.actual_batch_capacity = capacity;
    std::vector<Buffer<GpuId>> levels(k);
    for (size_t d = 0; d < k; ++d) levels[d].allocate(capacity * (d + 1), budget);
    Buffer<GpuId> candidate_ids, valid_ids;
    candidate_ids.allocate(capacity, budget); valid_ids.allocate(capacity, budget);
    Buffer<unsigned> flags, vertex_pass, valid;
    flags.allocate(capacity, budget); vertex_pass.allocate(capacity, budget); valid.allocate(capacity, budget);
    Buffer<int> candidate_n, valid_n, overflow;
    candidate_n.allocate(1, budget); valid_n.allocate(1, budget); overflow.allocate(1, budget);
    Buffer<unsigned long long> vertex_n; vertex_n.allocate(1, budget);
    Buffer<unsigned char> temp; temp.allocate(temp_bytes, budget);
    const size_t temp_capacity = temp_bytes;
    Buffer<DeviceDepth> stats; stats.allocate(k - 1, budget);
    Buffer<DeviceTotals> totals; totals.allocate(1, budget);
    cuda_check(cudaMemsetAsync(totals.get(), 0, sizeof(DeviceTotals), stream.value), "initialize totals");
    cuda_check(cudaMemsetAsync(overflow.get(), 0, sizeof(int), stream.value), "initialize overflow");
    if (k > 1) cuda_check(cudaMemsetAsync(stats.get(), 0, (k - 1) * sizeof(DeviceDepth), stream.value), "initialize depth stats");
    cuda_check(cudaStreamSynchronize(stream.value), "initialize workspace");
    r.device_peak_allocated_bytes = budget.peak;
    size_t roots = plan.partition_ids[0] == missing ? 0 : h.parts[plan.partition_ids[0]].edges.size();
    matching_start = Clock::now(); started = true;
    auto check_count = [&] {
      if (read_scalar(overflow.get(), stream.value)) throw Error("count_overflow", "GPU 128-bit counter overflow");
    };
    auto collect = [&](const GpuId *rows, size_t n, size_t columns) {
      if (!observe) return;
      std::vector<GpuId> host(n * columns);
      cuda_check(cudaMemcpyAsync(host.data(), rows, host.size() * sizeof(GpuId), cudaMemcpyDeviceToHost, stream.value), "test tuple download");
      cuda_check(cudaStreamSynchronize(stream.value), "wait test tuple download");
      for (size_t row = 0; row < n; ++row) {
        Embedding e; e.edges.assign(host.begin() + row * columns, host.begin() + (row + 1) * columns); observe(e);
      }
    };
    try {
      for (size_t root_first = 0; root_first < roots;) {
        control.check(); size_t root_rows = std::min(capacity, roots - root_first);
        roots_kernel<<<grid_for(root_rows), block_size, 0, stream.value>>>(view, levels[0].get(), root_first, root_rows);
        root_count_kernel<<<1, 1, 0, stream.value>>>(totals.get(), root_rows, k == 1, overflow.get()); launch_check();
        root_first += root_rows;
        if (k == 1) { collect(levels[0].get(), root_rows, 1); check_count(); continue; }
        std::vector<Frame> frames{{root_rows}};
        while (!frames.empty()) {
          control.check(); auto depth = frames.size(); auto &frame = frames.back();
          if (frame.parent == frame.rows) { frames.pop_back(); continue; }
          auto pid = plan.partition_ids[depth];
          if (pid == missing) { frames.pop_back(); continue; }
          auto cardinality = h.parts[pid].edges.size();
          size_t parents = std::min<size_t>(256, frame.rows - frame.parent);
          parents = std::min(parents, capacity);
          size_t width = std::min({o.candidate_tile_size, capacity / parents, cardinality - frame.tile});
          if (!width) { frames.pop_back(); continue; }
          size_t slots = parents * width, first = frame.tile;
          const auto *input = levels[depth - 1].get() + frame.parent * depth;
          auto &timing = r.depth[depth - 1];
          timer.begin(stream.value);
          cuda_check(cudaMemsetAsync(flags.get(), 0, slots * sizeof(unsigned), stream.value), "clear candidate union");
          seed_union_kernel<M::labeled><<<unsigned(parents), block_size, 0, stream.value>>>(view, input, depth, first, width, flags.get());
          intersection_kernel<M::labeled><<<grid_for(slots), block_size, 0, stream.value>>>(view, input, depth, first, width, slots, flags.get()); launch_check();
          temp_bytes = temp_capacity;
          cuda_check(cub::DeviceSelect::Flagged(temp.get(), temp_bytes, sequence, flags.get(), candidate_ids.get(), candidate_n.get(), int(slots), stream.value), "compact candidates");
          timer.finish(stream.value, timing.candidate_generation_time_s);
          auto nc = read_scalar(candidate_n.get(), stream.value);
          candidate_count_kernel<<<1, 1, 0, stream.value>>>(stats.get(), depth, nc, overflow.get()); launch_check();
          check_count(); control.check();
          size_t nv = 0;
          if (nc) {
            timer.begin(stream.value);
            vertex_count_kernel<<<unsigned(nc), block_size, 0, stream.value>>>(view, input, depth, first, width, candidate_ids.get(), vertex_pass.get());
            profile_kernel<M::labeled><<<unsigned(nc), block_size, 0, stream.value>>>(view, input, depth, first, width, candidate_ids.get(), vertex_pass.get(), valid.get()); launch_check();
            timer.finish(stream.value, timing.embedding_validation_time_s);
            temp_bytes = temp_capacity;
            cuda_check(cub::DeviceReduce::Sum(temp.get(), temp_bytes, vertex_pass.get(), vertex_n.get(), nc, stream.value), "sum vertex passes");
            temp_bytes = temp_capacity;
            cuda_check(cub::DeviceSelect::Flagged(temp.get(), temp_bytes, candidate_ids.get(), valid.get(), valid_ids.get(), valid_n.get(), nc, stream.value), "compact valid candidates");
            result_count_kernel<<<1, 1, 0, stream.value>>>(stats.get(), totals.get(), depth, vertex_n.get(), valid_n.get(), depth + 1 == k, overflow.get()); launch_check();
            nv = read_scalar(valid_n.get(), stream.value); check_count();
            if (nv && (depth + 1 < k || observe)) {
              expand_kernel<<<grid_for(nv * (depth + 1)), block_size, 0, stream.value>>>(view, input, depth, first, width, valid_ids.get(), nv, levels[depth].get()); launch_check();
            }
            if (depth + 1 == k && observe) collect(levels[depth].get(), nv, k);
          }
          ++r.batches;
          frame.tile += width;
          if (frame.tile == cardinality) { frame.tile = 0; frame.parent += parents; }
          // Prefix buffers stay device-resident until their entire subtree finishes.
          if (nv && depth + 1 < k) frames.push_back({nv});
        }
      }
      check_count(); control.check();
    } catch (const Error &e) {
      r.status = e.status == "cancelled" ? control.status() : e.status;
      r.message = e.status == "cancelled" ? control.message : e.what();
    }
    catch (const std::exception &e) { r.status = "internal_error"; r.message = e.what(); }
    catch (...) { r.status = "internal_error"; r.message = "unknown GPU traversal exception"; }
    // No host matching/counting: this only decodes the completed GPU counters.
    DeviceTotals host_totals{}; std::vector<DeviceDepth> host_stats(k - 1);
    cuda_check(cudaMemcpyAsync(&host_totals, totals.get(), sizeof(host_totals), cudaMemcpyDeviceToHost, stream.value), "download totals");
    if (k > 1) cuda_check(cudaMemcpyAsync(host_stats.data(), stats.get(), host_stats.size() * sizeof(DeviceDepth), cudaMemcpyDeviceToHost, stream.value), "download stats");
    cuda_check(cudaStreamSynchronize(stream.value), "finish GPU matching");
    r.matching_time_s = seconds(matching_start, Clock::now());
    r.count = host_count(host_totals.embeddings); r.root_candidates = host_count(host_totals.roots);
    r.counters_available = true;
    for (size_t i = 0; i < host_stats.size(); ++i) {
      r.depth[i].candidates = host_count(host_stats[i].candidates);
      r.depth[i].vertex_pass = host_count(host_stats[i].vertex_pass);
      r.depth[i].valid = host_count(host_stats[i].valid);
    }
    r.timing_complete = r.status == "ok";
  } catch (const Error &e) { r.status = e.status; r.message = e.what(); }
  catch (const std::bad_alloc &) { r.status = "out_of_memory"; r.message = "GPU transport allocation failed"; }
  catch (const std::exception &e) { r.status = "internal_error"; r.message = e.what(); }
  if (started && !r.matching_time_s) r.matching_time_s = seconds(matching_start, Clock::now());
  return r;
}
template ExecutionReport run_gpu(const IndexedGraph<Labeled> &, const IndexedGraph<Labeled> &, const QueryPlan &, const RunOptions &, RunControl &, const Observer &);
template ExecutionReport run_gpu(const IndexedGraph<Unlabeled> &, const IndexedGraph<Unlabeled> &, const QueryPlan &, const RunOptions &, RunControl &, const Observer &);
} // namespace hg
