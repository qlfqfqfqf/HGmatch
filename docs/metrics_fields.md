# GPU运行报告字段说明（v4）

CPU预处理和CUDA挖掘分开报告。时间单位秒，内存字节，128位数量为十进制字符串；null表示未知/尚未取得，0表示已知为零。只有status=ok给出完整embedding_count。

算法4/5用单stream CUDA Events计时，累加全次挖掘的各批次阶段区间，不累加GPU线程时间。顶层时间等于per_depth的时间之和。算法5包含顶点数检查和profile比较；验证后的结果压缩、嵌入复制和GPU计数不计入算法5。matching_time_s仍包含这些其他匹配开销。详细范围见README。

| 字段路径 | 类型 | 含义 |
| --- | --- | --- |
| `schema_version` | integer | GPU report v4; algorithm times are single-stream CUDA event intervals. |
| `system` | string | 系统标识 HGMatch-reproduction。 |
| `label_mode` | string | labeled 或 unlabeled，由可执行文件固定。 |
| `count_semantics` | string | ordered_hyperedge_embeddings：按模式边位置排列的数据边映射数，不乘顶点排列，不除模式自同构。 |
| `status` | string | ok/input_error/timeout/out_of_memory/count_overflow/internal_error；只有 ok 表示完整计数。 |
| `message` | string | 失败原因；成功为空字符串。 |
| `embedding_count` | string/null | 完整超边映射数量，十进制字符串（128 位无符号）；非 ok 为 null。 |
| `partial_embedding_count` | string/null | 失败前已到达 SINK 的计数；成功为 null；匹配前失败通常为 null，计划超时为字符串 0。 |
| `data_load_time_s` | number/null | 读取数据边和标签文本的秒数。 |
| `data_normalize_time_s` | number/null | 数据边内去重、精确边去重、统计的秒数。 |
| `partition_build_time_s` | number/null | 移动规范化数据、构造签名分区的秒数。 |
| `index_build_time_s` | number/null | 构建分区内顶点到超边 ID 倒排表的秒数（Fig.6）。 |
| `preprocessing_time_s` | number/null | 数据读取起至倒排索引完成的墙钟秒数，含统计等开销，不要求等于分项和。 |
| `query_load_time_s` | number/null | 模式读取、规范化、合法性验证、构造查询图的秒数。 |
| `plan_time_s` | number/null | Algorithm 3 生成匹配顺序的秒数。 |
| `matching_time_s` | number/null | Host wall seconds from GPU SCAN start through final GPU counters download; excludes preprocessing, planning, device layout, upload and JSON output; includes control, compaction, expansion and count work. |
| `online_query_time_s` | number/null | plan_time_s + matching_time_s，在线计划与计数总秒数；不含报告组装。 |
| `timeout_seconds` | number | 在线阶段计划与匹配共用 deadline 秒数；0 关闭；不限制读文件或建索引。 |
| `data_id_base` | integer | 数据外部顶点编号起点，默认 1；ID i 对应 node.txt 第 i-base+1 物理行。 |
| `query_id_base` | integer | 模式外部编号起点，默认 0；ID i 对应 patternNode 第 i-base+1 物理行。 |
| `query_input_protocol` | string | 固定 odd_integer_ignored_even_hyperedge，不存在格式切换。 |
| `normalization` | string | 先去除边内重复顶点，再按完整顶点集合去除重复边，保留首次出现次序。 |
| `data` | object | 数据图统计，规范化后的简单超图。 |
| `data.vertices` | integer | 标签文件行数；包含数据图孤立点，查询图禁止孤立点。 |
| `data.used_vertices` | integer | 规范化边集合覆盖的不同顶点数。 |
| `data.raw_edges` | integer | 读取到的原始实际超边记录数；模式忽略行不计。 |
| `data.unique_edges` | integer | 精确去重后超边数。 |
| `data.removed_duplicate_edges` | integer | 原始超边数减规范化超边数。 |
| `data.removed_duplicate_vertices` | integer | 所有原始边内重复顶点出现次数的总和，在边去重前统计。 |
| `data.raw_incidences` | integer | 原始实际边中顶点项数总和。 |
| `data.incidences` | integer | 规范化后各边大小之和。 |
| `data.max_arity` | integer | 规范化最大超边大小；无边为0。 |
| `data.average_arity` | number | 规范化 incidences/unique_edges；无边为0。 |
| `data.input_label_count` | integer | 输入标签不同取值数，仅输入统计，无标签匹配不使用该值。 |
| `data.matching_label_count` | integer/null | 有标签匹配的不同标签数；无标签为 null。 |
| `data.partitions` | integer | 不同超边签名分区数；无标签即不同超边大小数。 |
| `graph_storage_bytes` | integer | 数据图、边数组、边ID数组、源行号、标签等拥有容量的字节估算；不等于文件大小或 RSS。 |
| `signature_storage_bytes` | integer | 分区对象、签名及签名查找哈希表容量/节点开销字节估算。 |
| `inverted_index_bytes` | integer | 倒排表桶、节点及 posting 容量字节估算，不含分区对象；分配器元数据未计。 |
| `inverted_posting_payload_bytes` | integer | 倒排中实际 EdgeId 数量乘 sizeof(EdgeId)，字节；不含容量余量与节点开销。 |
| `storage_bytes_method` | string | 内存计量方法文本；容量/节点估算，不是精确分配器计数。 |
| `rss_after_index_bytes` | integer | 索引完成后的当前进程 RSS 字节，来自 Linux /proc/self/statm。 |
| `inputs` | object | 四个输入文件的路径与 SHA256；哈希在匹配计时外生成。 |
| `inputs.data-edges` | object | 输入文件 data-edges 的路径和摘要。 |
| `inputs.data-edges.path` | string | 本次运行实际使用的文件路径。 |
| `inputs.data-edges.sha256` | string | 文件原始字节的 SHA256 十六进制摘要。 |
| `inputs.data-nodes` | object | 输入文件 data-nodes 的路径和摘要。 |
| `inputs.data-nodes.path` | string | 本次运行实际使用的文件路径。 |
| `inputs.data-nodes.sha256` | string | 文件原始字节的 SHA256 十六进制摘要。 |
| `inputs.query-edges` | object | 输入文件 query-edges 的路径和摘要。 |
| `inputs.query-edges.path` | string | 本次运行实际使用的文件路径。 |
| `inputs.query-edges.sha256` | string | 文件原始字节的 SHA256 十六进制摘要。 |
| `inputs.query-nodes` | object | 输入文件 query-nodes 的路径和摘要。 |
| `inputs.query-nodes.path` | string | 本次运行实际使用的文件路径。 |
| `inputs.query-nodes.sha256` | string | 文件原始字节的 SHA256 十六进制摘要。 |
| `query` | object | 查询图统计；边数、度等不计忽略行。 |
| `query.vertices` | integer | 标签文件行数；包含数据图孤立点，查询图禁止孤立点。 |
| `query.used_vertices` | integer | 规范化边集合覆盖的不同顶点数。 |
| `query.raw_edges` | integer | 读取到的原始实际超边记录数；模式忽略行不计。 |
| `query.unique_edges` | integer | 精确去重后超边数。 |
| `query.removed_duplicate_edges` | integer | 原始超边数减规范化超边数。 |
| `query.removed_duplicate_vertices` | integer | 所有原始边内重复顶点出现次数的总和，在边去重前统计。 |
| `query.raw_incidences` | integer | 原始实际边中顶点项数总和。 |
| `query.incidences` | integer | 规范化后各边大小之和。 |
| `query.max_arity` | integer | 规范化最大超边大小；无边为0。 |
| `query.average_arity` | number | 规范化 incidences/unique_edges；无边为0。 |
| `matching_order` | array | Algorithm 3 输出的规范化模式边零基索引顺序，不受 query-id-base 影响。 |
| `query_cardinalities` | array | 按规范化模式原边序排列的签名分区基数 Card，无分区为0。 |
| `root_candidates` | string | SCAN 产生的首边候选数；不混入 EXPAND 的 C/F/V。 |
| `per_depth` | array | 逐个已匹配边数 2..k 的候选过滤指标；单边模式为空数组。 |
| `per_depth[].matched_edges` | integer | 当前新前缀的超边数。 |
| `per_depth[].candidates` | string | 该深度 Algorithm 4 生成的候选总数 C（不同前缀分别计）。 |
| `per_depth[].vertex_count_pass` | string | C 中通过 Observation V.5 顶点并集大小检查的数量 F。 |
| `per_depth[].valid` | string | F 中通过 Algorithm 5 profile 多重集检查的数量 V。完整运行最后一层 V 等于 embedding_count。 |
| `per_depth[].candidate_generation_time_s` | number | This depth: All Algorithm 4 CUDA event intervals, including candidate union/intersection and compaction. |
| `per_depth[].embedding_validation_time_s` | number | This depth: All Algorithm 5 CUDA event intervals, including vertex union count and exact profile multiset comparison; excludes child compaction and count reduction. |
| `candidates_total` | string | 所有 EXPAND 深度 C 之和；不含根候选。 |
| `vertex_count_pass_total` | string | 所有 EXPAND 深度 F 之和。 |
| `valid_partial_total` | string | 所有 EXPAND 深度 V 之和，包含中间嵌入，不等于最终嵌入数。 |
| `last_depth` | object | per_depth 最后一项的副本；单边模式为 null；用于最终层 C/F/V 对照。 |
| `peak_rss_bytes` | integer | 整个进程至报告生成时的峰值 RSS 字节，getrusage；包括读取与预处理峰值，不能声称仅匹配峰值。 |
| `environment` | object | 构建和运行环境，便于复现实验。 |
| `environment.os` | string | Linux 内核/架构描述。 |
| `environment.cpu_model` | string | /proc/cpuinfo 中 CPU 型号。 |
| `environment.physical_cores` | integer | 当前可见 cpuinfo 的不同物理封装/核心组合数，不代表宿主机器全量核数。 |
| `environment.logical_cpus` | integer | std::thread::hardware_concurrency 可见逻辑处理器数。 |
| `environment.affinity_cpus` | string | 进程可用 CPU affinity 集合的逗号文本。 |
| `environment.compiler` | string | 实际编译器版本首行。 |
| `environment.build_flags` | string | 实际 CPPFLAGS、编译选项、链接选项记录。 |
| `environment.source_sha256` | string | 构建时 include/src 所有文件及 Makefile、build_config.py 的路径排序整体 SHA256；按路径+NUL+内容+NUL。 |
| `environment.dependency_sha256` | string | 构建时 third_party 所有文件同法生成的整体 SHA256。 |
| `environment.cuda_compiler` | string |  |
| `environment.cuda_arch` | string |  |
| `end_to_end_time_s` | number/null | CLI入口至报告末尾元数据填充完成的墙钟秒数；不含最终JSON写盘/标准输出。 |
| `candidate_generation_time_s` | number/null | All Algorithm 4 CUDA event intervals, including candidate union/intersection and compaction. Single compute stream; GPU thread durations are not summed. Failures contain only completed measured intervals. |
| `embedding_validation_time_s` | number/null | All Algorithm 5 CUDA event intervals, including vertex union count and exact profile multiset comparison; excludes child compaction and count reduction. Single compute stream; GPU thread durations are not summed. Failures contain only completed measured intervals. |
| `backend` | string | 固定为 cuda |
| `timing_method` | string | 固定为 cuda_events_single_stream |
| `timing_complete` | boolean | Whether this entire matching run completed with valid stage timing. |
| `gpu_id` | integer |  |
| `gpu_memory_mb` | integer |  |
| `batch_capacity` | integer |  |
| `candidate_tile_size` | integer |  |
| `batches` | integer |  |
| `cuda_initialize_time_s` | number/null |  |
| `device_layout_time_s` | number/null |  |
| `host_to_device_time_s` | number/null |  |
| `gpu` | object |  |
| `gpu.id` | integer |  |
| `gpu.compute_major` | integer |  |
| `gpu.compute_minor` | integer |  |
| `gpu.runtime_version` | integer |  |
| `gpu.driver_version` | integer |  |
| `gpu.total_memory_bytes` | integer |  |
| `gpu.free_memory_bytes_at_start` | integer |  |
| `gpu.memory_budget_bytes` | integer |  |
| `gpu.peak_allocated_bytes` | integer |  |
| `gpu.actual_batch_capacity` | integer |  |
| `gpu.block_size` | integer |  |
| `gpu.compute_streams` | integer |  |
| `gpu.name` | string |  |

last_depth是per_depth最后一项的副本；单边查询为null。GPU显存峰值为项目显存分配器的自有分配峰值，不能与CPU进程peak_rss_bytes混用。CUDA初始化失败时部分GPU属性可能尚未取得。timing_complete=false的时间仅表示已取得的阶段时间，不用于完整运行平均。
