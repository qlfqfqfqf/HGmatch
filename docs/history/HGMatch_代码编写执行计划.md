# HGMatch CPU 复现：代码编写执行计划

版本：1.2。日期：2026-09-08。

> 给后续执行者：必须先完整阅读配套 `HGMatch_设计方案.md`，再按本计划逐项执行。建议使用 `superpowers:executing-plans` 逐任务推进；只有用户明确选择并行代理方式时再进行代理分工。本轮用户只要求设计与计划，因此本文件中的所有开发任务均尚未执行，不创建 HGMatch 源码、不运行匹配实验。

**目标：**生成 `hgmatch_labeled` 和 `hgmatch_unlabeled` 两个 CPU 可执行文件，遵循 HGMatch 论文 Algorithm 2–5 和 §VI，兼容附件输入并输出完整可复算实验指标。

**架构：**一个 C++20 公共实现库，两个编译期标签模式实例；`include/hgmatch/` 放头文件，`src/` 放具体实现，根目录 Makefile 直接构建。输入、精确去重、签名分区和倒排索引组成离线阶段；静态查询顺序、候选生成、profile 验证和任务数据流组成在线阶段。实验脚本只负责采样、独立进程运行及指标汇总，不承担生产匹配逻辑。

**技术栈：**C++20、GNU Make、标准库线程/原子/计时/集合、固定版本 Taskflow 独立 `wsq::UnboundedWSQ` 队列容器、随包 JSON 序列化头文件及 Linux 资源计量接口；Python 3 用于小图 oracle 和实验脚本。第三方组件固定提交/版本并保留许可证，登记到 `third_party/DEPENDENCIES.md`。作者原系统为 Rust；本方案为满足用户目录规范而改用 C++，保留论文算法语义，不能再保留旧 Rust 工程命令或依赖。

**设计依据：**同目录 `HGMatch_设计方案.md` v1.2。本计划按其章节编号引用，不另定义冲突算法。

## 1. 全局约束

- CPU 执行；默认单线程，`--threads` 可选择论文并行模式。
- `--query-id-base` 默认 0，`--data-id-base` 默认 1；命令、帮助、fixture 和 README 必须一致。
- 自有头文件放在 `include/hgmatch/`，具体实现放在 `src/`；根目录必含 `Makefile` 和 `README.md`，另含 `docs/IMPLEMENTATION_STATUS.md` 实现记录。
- 模式文件固定两行一组，奇数行整数直接忽略，偶数行读取超边并允许单顶点；无格式选择参数。数据文件仍每行一条超边。
- 仅两个正式编译匹配目标：`hgmatch_labeled`、`hgmatch_unlabeled`。不生成隐藏第三个生产算法。
- 匹配语义固定为 `ordered_hyperedge_embeddings`；不计区域内部顶点排列，不做自同构除法，不按无序数据边集合去重。
- 规范化身份为完整顶点 ID 集合；必须进行行内顶点去重和重复超边去重。
- 有标签版保留标签多重集签名、候选标签比较、profile 标签比较。
- 无标签版签名只是大小，候选和 profile 热路径完全没有标签比较；不能通过统一标签值模拟。
- 默认支持任意合法大小的连通非空查询，包括单边及超过 64 条边；对不连通或含孤立查询顶点显式报错。
- 不引入 GPU、Tensor Core、显式 SIMD、OIG、IHS、动态顺序、其他成本模型、额外剪枝或候选缓存。
- 主调度为 LIFO 加随机半队列工作窃取。NOSTL 和 BFS 仅是显式选择的论文消融模式，不参与自动选择。
- `matching_time_s` 必须独立输出；超时或异常不能冒充完整计数。
- 生产源码修改发生在后续用户要求实现之后。本计划中的接口、测试数据和命令是后续实现契约，不是声称已存在的代码。

## 2. 源文件规划与职责

项目根目录约定为 `hgmatch_cpu/`。以下都是后续拟创建的相对路径：

| 路径 | 唯一主要职责 |
|---|---|
| `Makefile` | release/debug/test/clean/help 等直接构建入口与头文件依赖 |
| `src/main_labeled.cpp` | 调用 `run_cli<Labeled>()` 的精简入口 |
| `src/main_unlabeled.cpp` | 调用 `run_cli<Unlabeled>()` 的精简入口 |
| `include/hgmatch/types.hpp`、`src/types.cpp` | ID、图、计划、元组、任务与状态类型；前者声明，后者实现 |
| `include/hgmatch/cli.hpp`、`src/cli.cpp` | 参数解析、主阶段编排、状态和退出码；前者声明，后者实现 |
| `include/hgmatch/input.hpp`、`src/input.cpp` | 数据逐行读边、模式固定两行读边、ID 和标签读取；前者声明，后者实现 |
| `include/hgmatch/normalize.hpp`、`src/normalize.cpp` | 行内/边去重、追溯、查询结构校验；前者声明，后者实现 |
| `include/hgmatch/mode.hpp`、`src/mode.cpp` | Labeled/Unlabeled 独立签名/profile 类型及静态分派；前者声明，后者实现 |
| `include/hgmatch/storage.hpp`、`src/storage.cpp` | 签名分区与只读边表；前者声明，后者实现 |
| `include/hgmatch/inverted_index.hpp`、`src/inverted_index.cpp` | 分区 posting 构建和访问；前者声明，后者实现 |
| `include/hgmatch/set_ops.hpp`、`src/set_ops.cpp` | 有序集合差、并、交；前者声明，后者实现 |
| `include/hgmatch/plan.hpp`、`src/plan.cpp` | Algorithm 3 及查询前缀结构；前者声明，后者实现 |
| `include/hgmatch/candidates.hpp`、`src/candidates.cpp` | Algorithm 4 候选生成；前者声明，后者实现 |
| `include/hgmatch/validation.hpp`、`src/validation.cpp` | Algorithm 5 顶点数和 profile 验证；前者声明，后者实现 |
| `include/hgmatch/operators.hpp`、`src/operators.cpp` | SCAN/EXPAND/SINK 算子；前者声明，后者实现 |
| `include/hgmatch/work_queue.hpp`、`src/work_queue.cpp` | 第三方队列适配、任务所有权和底层接口隔离；前者声明，后者实现 |
| `include/hgmatch/scheduler.hpp`、`src/scheduler.cpp` | LIFO、半队列窃取、完成与取消；前者声明，后者实现 |
| `include/hgmatch/scheduler_ablation.hpp`、`src/scheduler_ablation.cpp` | NOSTL/BFS，复用同一匹配算子；前者声明，后者实现 |
| `include/hgmatch/metrics.hpp`、`src/metrics.cpp` | 指标汇总和 JSON 序列化；前者声明，后者实现 |
| `include/hgmatch/resources.hpp`、`src/resources.cpp` | RSS、线程 CPU 时间及可选采样；前者声明，后者实现 |
| `include/hgmatch/error.hpp`、`src/error.cpp` | Result、错误对象、状态和退出码映射；前者声明，后者实现 |
| `third_party/work_stealing_queue/wsq.hpp`、`LICENSE` | 上游独立队列头文件及原许可证（LICENSE 位于同组件目录） |
| `third_party/nlohmann/json.hpp`、`LICENSE` | JSON 头文件及原许可证（LICENSE 位于同组件目录） |
| `third_party/DEPENDENCIES.md` | 每个依赖的固定版本/提交、来源、文件校验和、许可和本地修改 |
| `tests/fixtures/` | 小图、固定两行记录的查询及手算真值 |
| `tests/input_contract.cpp` | 格式、默认 ID 起点和错误定位 |
| `tests/storage_contract.cpp` | 去重、分区、posting 不变量 |
| `tests/algorithm_contract.cpp` | 顺序、候选、验证与高阶交叠反例 |
| `tests/scheduler_contract.cpp` | 任务唯一性、竞争、完成检测和消融一致性 |
| `tests/metrics_contract.cpp` | 时钟边界、字段、状态与 schema |
| `tests/hb_contract.cpp` | 附件离线统计，显式按需运行 |
| `tests/support/collect.hpp`、`tests/support/collect.cpp` | 测试专用完整元组收集与逆置换 |
| `tests/support/test_harness.hpp`、`tests/support/test_harness.cpp` | 测试断言、案例注册、FILTER 选择和非零失败退出 |
| `tools/oracle.py` | 独立小图顶点单射真值 |
| `tools/differential.py` | 小图随机差分与失败反例保存 |
| `tools/sample_queries.py` | 随机游走采样，输出固定两行模式文件 |
| `tools/run_experiments.py` | 独立进程重复、超时和原始报告保存 |
| `tools/summarize.py` | 论文各项指标与完成率汇总 |
| `metrics.schema.json` | 全部输出字段及状态约束 |
| `experiments/hb_example.json` | 附件直接运行清单，查询 ID 默认 0 |
| `experiments/paper_settings.json` | q2/q3/q4/q6、线程和消融设置 |
| `README.md` | 编译运行、完整字段字典、逐文件职责以及测试/限制说明 |
| `docs/IMPLEMENTATION_STATUS.md` | 实际实现范围、论文/函数映射、验证证据与未完成事项 |
| `docs/reproduction_report.md` | 实际环境、实验结果与复现差异 |
| `build/obj/`、`build/dep/` | 按 release/debug 分开的对象及自动头文件依赖 |
| `build/bin/`、`build/tests/` | 正式程序和测试程序，互不混放 |

不为每个超边数量建立专用源文件。两个入口应很薄，标签语义通过编译期类型选择。头文件主要声明，`.cpp` 主要实现；模式模板在对应 `.cpp` 中为 Labeled 和 Unlabeled 显式实例化，必要的小型 inline 定义除外。禁止通过包含 `.cpp` 链接模块；第三方 header-only 代码保持在 `third_party/`，不作为自有头文件堆积实现的理由。各任务只简写 `.cpp` 名时，指 `src/` 下同名实现及本表对应的头文件；修改接口必须同步更新二者。

### 2.1 Makefile 实现契约

根目录 `Makefile` 必须直接编译 `src/`，以 `-Iinclude` 和独立 third_party include 路径解析头文件。默认 `make`、`make all`、`make release` 等价，生成 `build/bin/hgmatch_labeled` 与 `build/bin/hgmatch_unlabeled`。不要生成第三个正式算法程序；测试程序位于 `build/tests/`。

- release 使用 `-O3 -DNDEBUG -std=c++20 -pthread`；debug 使用 `-O0 -g -std=c++20 -pthread`，二进制在 `build/bin/debug/`，对象/依赖按配置分开。
- 支持 `CXX`、`CPPFLAGS`、`CXXFLAGS`、`LDFLAGS`、`LDLIBS`；编译配置变化触发必要重编译，不能使用旧配置对象。默认不使用 `-march=native` 或额外 LTO。
- 实现自动头文件依赖（如 `-MMD -MP`）和目录依赖；同一个公共对象只由一条规则生成，`make -j4` 无竞争。
- 提供 `make debug`、`make test`、`make test TEST=名称 FILTER=案例`、`make test-hb IO_DIR=路径`、`make clean`、`make help`。`TEST` 省略时运行全部默认小测试，`FILTER` 省略时运行该测试集全部案例；未知名称报错，不空跑成功。
- 测试 harness 在 `tests/support/`；Makefile 将 FILTER 传入测试程序的命名筛选接口，任何失败返回非零。HB 测试由 test-hb 显式触发，默认 test 不依赖上传数据。
- clean 只删除本项目 build 构建输出，不删除 io、results、第三方源码或文档；构建不隐式下载依赖，不要求 Cargo 或预生成 CMake 工程。
- 固定第三方提交、校验和和许可证随包交付；最终 README 记录实测编译器/Make 版本，构建系统不会自动替换依赖版本。

## 3. 跨模块接口契约

以下采用 C++ 类型和语言无关的“输入 → 输出”记法描述接口，本轮不写源代码。`Result<T>` 为项目在 `error.hpp` 定义的结果包装，内部用 `std::variant<T, Error>` 表达成功或错误，并对 `void` 特化；它不是 Rust 类型。只读参数使用 const 引用或 `std::span`，所有权通过 RAII 明确，计数运算检查溢出。

### 3.1 基础类型

| 类型 | 定义及所有权 |
|---|---|
| `VertexId`, `EdgeId`, `QueryEdgeId` | 内部非负整数下标；使用 `std::size_t`，64 位构建；外部 ID 先做范围验证 |
| `Label` | 有符号 64 位整数，原值相等即标签相等 |
| `Count` | `unsigned __int128` 的别名，要求 GCC/Clang 扩展支持；所有累计操作检查溢出 |
| `InputOptions` | 数据/查询各自的 ID 起点；无查询格式字段 |
| `RawHypergraph` | 原始真实边顶点列表、标签和源行号；不保存模式奇数行整数值 |
| `NormalizeStats` | 原始边数、唯一边数、移除重复顶点/边数、关联数等 |
| `Hypergraph` | 规范化边、顶点数量、标签和源追溯 |
| `PartitionedGraph<M>` | 模式 M 的签名分区、只读边表和模式上下文 |
| `IndexedGraph<M>` | `PartitionedGraph<M>` 加分区 posting；共享只读 |
| `PreparedQuery<M>` | 规范化查询、模式 M 所需标签上下文 |
| `QueryPlan<M>` | 顺序 `order`、逆置换、前缀信息、各层分区 ID |
| `Embedding` | `std::vector<EdgeId> edges`，顺序对应计划前缀 |
| `RunControl` | deadline、原子取消状态及错误通道 |
| `LocalStats` | 本线程阶段候选/通过数、计数、任务/窃取/诊断计时 |
| `RunReport` | 完整状态、元数据、离线/在线/资源指标 |

`Unlabeled` 的匹配上下文不保留数据或查询标签数组。有标签 profile 键为 `std::pair<Label, std::vector<std::size_t>>`；无标签 profile 键为 `std::vector<std::size_t>`。两者可以共享普通集合和调度代码，但键的类型必须不同。

### 3.2 主要函数接口

| 模块 | 输入 → 输出 |
|---|---|
| input | `read_data_hypergraph(edges_path, nodes_path, id_base) -> Result<RawHypergraph>`；数据每行一条边 |
| input | `read_query_hypergraph(edges_path, nodes_path, id_base) -> Result<RawHypergraph>`；模式固定忽略奇数行、读取偶数行 |
| normalize | `normalize(raw) -> Result<std::pair<Hypergraph, NormalizeStats>>` |
| normalize | `validate_query(graph) -> Result<void>`；检查非空、连通及无孤立模式顶点 |
| mode | `prepare_data<M>(graph) -> M::DataInput`；`prepare_query<M>(graph) -> PreparedQuery<M>` |
| storage | `build_partitions<M>(data) -> Result<PartitionedGraph<M>>` |
| inverted_index | `build_index<M>(partitioned) -> Result<IndexedGraph<M>>` |
| inverted_index | `postings(graph, partition_id, vertex) → std::span<const EdgeId>`；缺失时返回空的只读视图 |
| plan | `compute_order<M>(query, graph) -> Result<QueryPlan<M>>` |
| candidates | `generate_candidates<M>(graph, query, plan, embedding, control) -> Result<std::vector<EdgeId>>` |
| validation | `check_vertex_count(query, plan, graph, extended) -> bool` |
| validation | `check_profiles<M>(query, plan, graph, extended, control) -> Result<bool>` |
| operators | `execute_task<M>(task, context, emitter, local_stats, control) -> Result<void>` |
| scheduler | `run<M>(context, run_options, control) -> Result<ExecutionReport>` |
| metrics | `assemble_report(offline, query, execution, provenance) -> RunReport` |
| cli | `run_cli<M>() -> exit_code` |

`generate_candidates` 不更新 F/V；算子在候选生成完成后记录 C，在数量检查通过后记录 F，在 profile 通过后记录 V，这样论文过滤指标边界唯一。`check_profiles` 只负责算法，不承担输出或全局计数。

`Task` 为枚举：SCAN 携带分区 ID 和首边范围；EXPAND 携带下一层位置与部分元组；SINK 携带完整元组。`Emitter` 接口将一个新任务交给当前调度器，由调度器完成 outstanding 增加、发布和队列所有权管理。主模式的 SCAN 使用完整首分区；NOSTL 才使用等份范围。

## 4. 测试真值清单

以下先建立可手算真值，再写算法；小图 fixture 顶点均使用零基，测试显式配置数据 ID 起点 0，查询使用默认起点 0，标签文件按第 `id+1` 行映射。实际 HB 数据仍使用默认起点 1。所有查询 fixture 写为固定两行记录（每条真实边前写占位整数 0），数据 fixture 每行一条边；下面集合记法仅描述真实超边，不包括忽略行。除特别说明外所有顶点标签相同。期望计数均为“按查询位置区分的数据边元组数”。

| 测试名称 | 查询 / 数据 | 必须满足的断言 |
|---|---|---|
| `single_edge_permutations` | q={{0,1,2}}，H={{0,1,2}} | 有/无标签均为 1，不是 6 |
| `symmetric_two_edges` | q=H={{0,1},{1,2}} | 计数为 2，不是 1 |
| `no_signature` | q 单边大小 3，H 只有大小 2 边 | 完整计数 0，状态 ok |
| `duplicate_edges` | H 原始行 {0,1},{1,0},{0,0,1},{1,2}；q 单边大小 2 | H 规范化后 2 边；计数 2 |
| `same_signature_not_duplicate` | H={{0,1},{2,3}}，相同标签；q 单边大小 2 | 两边同签名但都保留；计数 2 |
| `overlap_label_mismatch` | q={{0,1},{0,2}} 标签 [A,B,B]；H 同边集合，标签 [B,A,A] | 每条边签名都是 {A,B}；有标签计数 0，无标签计数 2 |
| `exact_edge_size` | q={{0,1}}，H={{0,1,2}} | 0，不能按包含关系接受 |
| `extra_data_edges` | q={{0,1},{1,2}}，H 再加 {0,2} | 同标签计数 6；额外数据边不构成诱导约束 |
| `nested_edges` | q=H={{0},{0,1}} | 计数 1，包含边不能当作重复边删除 |
| `reuse_same_data_edge` | q={{0,1},{1,2}}，H={{0,1}} | 0；不能将同一数据边填两个位置 |
| `local_degree_not_global` | q={{0,1},{1,2}}；H={{0,1},{1,2},{1,3}} | 有合法嵌入，计数 6；不能以全局度 3 不等于查询度 2 拒绝 |
| `single_vertex` | q={{0}}，H={{0},{1}} | 计数 2；一整数行是合法边 |

### 四阶结构反例 `four_way_profile`

```text
查询 q：
Q0={1,2}
Q1={1,3}
Q2={2,3}
Q3={0,1,2,3}

数据 H：
D0={0,3}
D1={1,3}
D2={2,3}
D3={0,1,2,3}
```

所有顶点标签相同。两侧边大小均为 2、2、2、4；前三条边两两交集都是 1；每条小边与大边交集都是 2；全体边顶点并集都是 4。但查询前三条边三重交集为空，数据前三条边三重交集为 {3}。正确计数为 0。

计划应首先匹配 Q3，因为大小 4 分区基数为 1。该反例专门防止用整图顶点数、两两交叠或单纯边大小代替 Algorithm 5 的完整 profile。

### 输入协议反例

- 数据文件中 `0\n1\n` 是两条单顶点边；模式文件中同样的内容表示忽略第一行 0、读取一条单顶点边 `{1}`。角色由数据/查询读取入口决定，不通过格式参数选择。
- 模式文件 `70\n7,9,13\n` 表示忽略 70、读取一条边 `{7,9,13}`；`70\n9\n` 表示一条合法单顶点边 `{9}`。奇数行整数不按模式顶点范围校验。
- 把每个奇数行分别替换为 0、负整数或更大的整数文本，偶数行和标签保持不变，解析图与完整计数必须不变；输入文件哈希可以变化。
- 奇数物理行数、空标签行、`1,,2`、负顶点、ID 超界、标签行含两个值均应报错并定位。逗号边界不允许空字段，正常逗号两侧空格允许。
- 标签文件的最后一个换行符正常；不能误当空标签行。中间实际空白行必须报错。
- 不连通查询、孤立模式顶点、无边查询分别报错，不能给出成功的零结果。

## 5. 分阶段执行任务

每项先创建对应的可失败验收测试，确认缺失功能导致失败，然后实现最小符合设计的功能，运行本项验收。若项目已由前一轮部分实现，先检查实际差异，不覆盖用户已有改动。每项结束同步更新 `docs/IMPLEMENTATION_STATUS.md`，记录“头文件与实现文件、关键函数、实现范围、验证命令、结果、与论文差异”；存在未解决计数错误时不得推进到性能实验。

### 任务 1：固定工程边界和运行环境

**文件：**`Makefile`、两个入口、`include/hgmatch/` 类型/CLI/错误头文件及 `src/` 对应实现、README 和实现记录初稿、依赖清单。

**输入：**设计 §1、§4、§12。**输出：**两个可构建目标和精确 CLI 契约。

- [ ] 记录 64 位 Linux、CPU/核数、内存、C++20 编译器及 GNU Make 版本，验证工具链支持 Count 所需的 `unsigned __int128` 和固定依赖，写入 README 与构建报告。
- [ ] 建立头文件/实现/入口目录和 Makefile；默认构建两个正式程序，debug 对象与 release 分离；队列及 JSON 固定源码和许可证放 third_party，CLI 使用自有解析器，系统资源使用 Linux 接口。
- [ ] 核对固定版本 `wsq::UnboundedWSQ` 的所有者 push/pop、外部 steal、size/empty、竞争及回收语义；用封装隔离底层，不接入 Taskflow 执行器。
- [ ] 为 `--help`、`--threads 0`、未知参数、遗漏必填路径建立 CLI 验收；两入口同一参数表。增加“不传 query-id-base 与显式 0 相同”“显式 1 正确读取一基查询”的测试。
- [ ] 按设计实现参数解析和状态/退出码；此阶段没有匹配功能时不能输出伪造的 `status=ok` 与计数 0。
- [ ] 运行 `make -j4 release`，确认 `build/bin/` 下仅两个指定正式程序；测试 harness 放 `build/tests/`。再验证增量编译、头文件修改触发重编译、make debug/clean/help、选项变化配置戳及 -j 构建无竞态。

**通过条件：**两个可执行入口名正确，非法参数可定位，依赖和编译配置可复现，无 CUDA/VTune 依赖。

### 任务 2：固定两行模式读取与精确规范化

**文件：**`src/types.cpp`、`input.cpp`、`normalize.cpp`、`tests/input_contract.cpp`、`tests/hb_contract.cpp`、相应 fixtures。

**输入接口：**CLI 路径和 `InputOptions`。**输出接口：**`read_data_hypergraph`、`read_query_hypergraph`、`normalize`、`validate_query`。

- [ ] 先写本计划输入反例以及四个去重/保留测试：重复边、行内重复、同签名不同边、包含边。
- [ ] 实现标签文件按物理行读取；外部 ID 与标签行的映射必须由独立参数决定。
- [ ] 实现逗号/空白顶点列表，检查空字段。数据入口逐行读边；查询入口严格两行一组，只检查奇数行为单个整数文本后丢弃其值，偶数行读取一条超边并允许单个顶点 ID。校验配对完整性，不跳过实际空白行，不保留格式分支。
- [ ] 实现按顶点 ID 排序、行内去重、精确边集合去重，保存第一次出现的源行；输出移除统计。
- [ ] 查询结构校验使用超边交叠连通关系，允许包含边和一顶点边；明确拒绝不连通及孤立查询顶点。
- [ ] 在附件上只运行读取和离线统计测试，不把尚未存在的挖掘结果预填为真值。
- [ ] 运行 `make test TEST=input_contract`；再用 `make test-hb IO_DIR=/absolute/path/to/io` 检查真实附件。

**HB 硬断言：**原始边 60,987；唯一边 54,933；数据顶点 1,494；原始关联数 1,248,666；规范化关联数 1,216,701；模式真实边数 4、大小 [3,27,2,2]、顶点数 29、关联数 34。默认普通 CI 不依赖用户大文件，缺文件时给出明确运行说明，不假装通过 HB 测试。

### 任务 3：建立独立真值和计数语义闸门

**文件：**`tools/oracle.py`、`tests/fixtures/`、`tests/support/collect.cpp`、计数类型和测试文档。

**输入：**规范化小图和查询。**输出：**按输入查询边位置排列的完整边元组集合及其大小。

- [ ] 为 `single_edge_permutations` 手写预期 1，为 `symmetric_two_edges` 手写预期元组 `(D0,D1)` 和 `(D1,D0)`。
- [ ] oracle 的数据文件入口逐行读边，查询文件入口固定忽略奇数行、读取偶数行，直接忽略整数值且支持偶数行单顶点；所有随机查询文件和 fixture 写出时遵循同一协议。
- [ ] oracle 使用 `itertools.permutations` 枚举不同数据顶点组成的映射；有标签先验证对应标签，再把每条查询边映射成精确顶点集合并查找数据边。
- [ ] 将映射后的完整数据边元组放入 Python 集合，不累计同一边元组的不同顶点排列；不要使用生产 profile 或候选模块。
- [ ] 建立 fixtures 的 oracle 自检命令 `python3 tools/oracle.py --self-test`，逐个核对本计划手算断言。
- [ ] 为测试收集器定义只在测试构建下启用的入口，生产 SINK 默认只有计数。收集器按 `plan.inverse_order` 恢复原查询边位置。
- [ ] 定义小图上限，例如默认数据顶点不超过 8、查询顶点不超过 6；超出时 oracle 报错，不误用来跑完整 HB。

**通过条件：**明确区分“顶点映射数 / 有序超边元组数 / 无序出现次数”，手算 fixtures 全部通过。

### 任务 4：两个静态标签模式、签名分区和倒排索引

**文件：**`mode.cpp`、`storage.cpp`、`inverted_index.cpp`、`set_ops.cpp`、`tests/storage_contract.cpp`。

**输入接口：**规范化图。**输出接口：**`prepare_data<M>`、`prepare_query<M>`、`build_partitions<M>`、`build_index<M>`、`postings`。

- [ ] 测试 `{A,A,B}` 与 `{A,B,B}` 必须分属不同有标签分区，同大小边必须归于同一个无标签分区。
- [ ] 为 Unlabeled 定义无标签字段的上下文、大小签名和关联列表 profile 类型；不可将有标签数组置零后保留。
- [ ] 每个规范化数据边精确归属一个分区，分区基数来自真实边数；相同签名不同顶点的边不能丢失。
- [ ] 为每个分区的实际关联顶点构建升序、无重复边 ID posting；缺失顶点返回空列表。
- [ ] 实现普通有序集合差/并/交，测试空集合、重复 posting 来源和多组候选；结果始终为集合语义。
- [ ] 检查所有 posting 元素总数等于规范化 I；每条边对其每个顶点恰好贡献一次 posting 项。
- [ ] 运行 `make test TEST=storage_contract`，再运行附件分区核验。

**HB 验收：**有标签分区 5,116；无标签分区 341。四条查询边的有标签 Card 为 [921,10,2738,1838]，无标签 Card 为 [4369,430,7656,7656]。这些数基于本附件指纹和论文去重规则，不允许套用论文 52,960 边版本的统计。

### 任务 5：Algorithm 3 和动态查询长度

**文件：**`plan.cpp`、`types.cpp`、`tests/algorithm_contract.cpp`。

**输入接口：**`PreparedQuery<M>`、`IndexedGraph<M>`。**输出接口：**`compute_order<M>` 和 `QueryPlan<M>`。

- [ ] 建立“多个旧边共享同一查询顶点”的顺序测试，确保分母是并集交集大小，不能重复累计该顶点。
- [ ] 实现首边最小 Card，后续最小 `Card / shared_vertex_count`；只在未选且相邻的查询边中选择。
- [ ] 并列按输入查询边编号；使用受检宽整数交叉乘法比较比例，保证顺序稳定。
- [ ] 建立每层已匹配查询前缀顶点数量、旧前缀关联次数及最后新增边 profile 的查询侧描述。
- [ ] 维护顺序和逆置换，测试重编号后 oracle 比较时的元组恢复。
- [ ] 覆盖 1、2、3、4、6、7、65 条边，不存在定长 4 层数组或 64 位 profile 位集上限。
- [ ] 运行 `make test TEST=algorithm_contract FILTER=matching_order`。

**附件验收：**有标签 `Q1,Q0,Q3,Q2`；无标签 `Q1,Q0,Q2,Q3`。预计算仅涉及查询输入信息，不建立额外数据图候选结构。

### 任务 6：严格实现 Algorithm 4

**文件：**`candidates.cpp`、`set_ops.cpp`、`tests/algorithm_contract.cpp`。

**输入接口：**只读图、查询计划、已验证的 `Embedding`、`RunControl`。**输出接口：**升序无重复 `std::vector<EdgeId>` 候选。

- [ ] 手工构造含相邻与非相邻旧边的前缀，逐个记录 `V_non`、每个 `V_inc(e,u)`、组内并集和最终交集，写为精确预期。
- [ ] 先构造所有已匹配非相邻边的顶点并集，不能看尚未匹配的未来数据边。
- [ ] 逐已匹配相邻查询边、逐共享查询顶点形成候选组；在映射旧边中先做顶点差，再做旧前缀度数相等，有标签时再保留标签一致者。
- [ ] 每组只查询当前目标签名分区中的 posting，组内并、组间交；空组立即得到空候选。
- [ ] 无标签实现使用无标签上下文，不读取、不哈希、不比较标签。
- [ ] 保留算法需要的每个 `(e,u)` 组，不加入重新选择锚点、排序剪枝、候选缓存或新的提前拒绝。
- [ ] 长集合循环响应 `RunControl` 取消；取消返回运行错误而不是空候选，防止将未完成搜索变成合法 0。
- [ ] 运行 `make test TEST=algorithm_contract FILTER=candidates`。

**通过条件：**能解释候选中每个边 ID 的来源；真值 oracle 中的可扩展有效边从不被漏掉；允许必要条件仍产生假阳性，交给任务 7。

### 任务 7：Algorithm 5、profile 多重集与标签移除

**文件：**`validation.cpp`、`mode.cpp`、`tests/algorithm_contract.cpp`。

**输入接口：**合法旧前缀加一个候选得到的扩展元组。**输出接口：**顶点数判定与 profile 判定。

- [ ] 先写四阶反例、重叠标签错配、同一数据边复用、嵌套边和单边测试。
- [ ] `check_vertex_count` 比较两侧新前缀的**不同顶点并集数量**，不是超边长度之和。
- [ ] 对最后新增边的每个顶点构造其在整个新前缀中的关联位置列表；有标签键加标签，无标签键无标签字段。
- [ ] 使用精确 profile 键到出现次数的映射比较多重集，保留重复 profile；不能仅比较键集合或哈希摘要。
- [ ] 查询侧与数据侧关联位置坐标一致，不直接比较两边原始边 ID。
- [ ] 证明并测试旧前缀已合法这一调用前提；生产路径只有 SCAN 根和通过验证的 EXPAND 输出能进入下一层。
- [ ] 运行 `make test TEST=algorithm_contract FILTER=validation`。

**通过条件：**`four_way_profile` 为 0，`overlap_label_mismatch` 有标签为 0/无标签为 2，所有手算 fixture 与 oracle 元组集合一致。若只通过两两交叠测试而没有 profile 反例，不得验收。

### 任务 8：算子与单线程 LIFO 完整路径

**文件：**`operators.cpp`、`scheduler.cpp`、`metrics.cpp` 初版、测试收集器。

**输入接口：**图、查询计划和运行选项。**输出接口：**`ExecutionReport`，含完整计数和每层 C/F/V。

- [ ] 写 SCAN→SINK 的单边任务测试和 SCAN→EXPAND→SINK 的两边任务测试。
- [ ] SCAN 对每条首分区数据边产生一份任务；EXPAND 依次调用任务 6/7 的函数，SINK 受检加 1。
- [ ] 在候选、顶点数通过、profile 通过三个固定边界记录 C/F/V；SCAN 根数量单独统计。
- [ ] 单线程使用显式任务 LIFO 队列，不能用层级 `std::vector<Embedding>` 实现主路径；不改成逐顶点递归。
- [ ] 实现 `outstanding_tasks` 生命周期，生成子任务先计入，再发布，最后减少父任务；单线程阶段同样覆盖该规则。
- [ ] 使两个入口接入完整单线程流程，默认只输出计数和指标，测试构建才收集元组。
- [ ] 运行算法契约及 oracle 差分，所有 fixture 全部通过后，才开始多线程阶段。

**通过条件：**有标签和无标签单线程算法都可运行；完整结果与 oracle 一致；`embedding_count=V_last`；单边计数等于根数量。

### 任务 9：论文工作窃取与并发正确性

**文件：**`scheduler.cpp`、`types.cpp`、`tests/scheduler_contract.cpp`。

**输入接口：**同一套算子和 `RunControl`。**输出接口：**p 线程下等价计数、任务和窃取统计。

- [ ] 先用人工任务树测试每个叶任务恰被消费一次，不把真实匹配错误与调度错误混在一起。
- [ ] 为每个线程建立非阻塞 LIFO 队列，使用所有者 push/pop 与外部 stealer；任务的可变元组不能跨线程别名共享。
- [ ] 本地空时从其他非空队列随机选择受害者，按快照目标 `max(1,floor(n/2))` 从尾部窃取；记录请求与实际窃取数量。
- [ ] 处理所固定 C++ 队列的空/竞争返回、受害者变空、只有一个任务、其他窃取者竞争；不得因一次窃取失败提前终止。
- [ ] 加入一致的全局完成检测：排队加执行任务归零才能成功；窃取不加减 outstanding。
- [ ] 每线程局部结果汇总时检查 Count 的 128 位 溢出；在工作线程入口捕获异常并写入共享错误通道；主线程通过 RAII 回收可 join 的线程并检查错误通道，不能让异常逃出 std::thread 入口或静默忽略失败。
- [ ] 用人工阻塞/交错控制验证“父任务发布子任务期间”“最后一个队列任务被窃取期间”不会误终止。
- [ ] 运行 `make test TEST=scheduler_contract`，并对小图重复执行 1/2/4/8 线程、多个 seed 的完整元组比较。

**通过条件：**每个任务恰好执行一次，无丢计、重计、死锁、空转不退出；多个线程的结果集合与单线程完全相同。不得仅凭运行更快判定并发正确。

### 任务 10：原论文调度消融

**文件：**`scheduler_ablation.cpp`、`scheduler.cpp`、`tests/scheduler_contract.cpp`。

**输入接口：**相同计划、算子和指标。**输出接口：**`lifo-nosteal` / `bfs` 可选运行。

- [ ] NOSTL 把首分区按数量分成尽可能等长范围，每工作线程一个范围，本地 LIFO，关闭工作窃取。
- [ ] BFS 以当前层中间元组为输入并行扩展并保存完整下一层；该实现只用于论文内存对照。
- [ ] 三种调度都调用同一个 `execute_task` 或同源候选/验证算子，不复制独立匹配条件。
- [ ] 为 BFS 记录正常运行、资源失败和超时；显式说明逐层物化可能占用大量内存。
- [ ] 在所有小图上比较三种调度的完整计数、末层候选/过滤数；调度方式不应改变搜索结果。
- [ ] 将所有报告明确写出 scheduler，实验汇总不混合主路径与消融。

**通过条件：**能复现论文消融所需的运行模式，而不是只输出一个没有比较对象的“内存优化百分比”。

### 任务 11：计时、内存、过滤和逐线程指标

**文件：**`metrics.cpp`、`resources.cpp`、`cli.cpp`、`metrics.schema.json`、`tests/metrics_contract.cpp`。

**输入接口：**所有阶段的时间戳、规范化统计和工作线程报告。**输出接口：**标准输出摘要与完整 JSON。

- [ ] 先写 schema 测试：`ok` 必须有完整计数；`timeout` 完整计数为 null 且有部分计数；未采集的诊断值不能为伪造的 0。
- [ ] 按设计 §9.1 放置所有时钟，确保 `matching_time_s` 从线程启动之前到线程完成及计数汇总之后，排除输入/去重/索引/最终输出。
- [ ] `online_query_time_s=plan_time_s+matching_time_s`；测量主性能的计时调用不逐候选调用时钟。
- [ ] 输出每层 C/F/V 和三种汇总，单边查询层数组为空且根数量正确；断言 V≤F≤C 和末层 V 等于最终计数。
- [ ] 按唯一所有者统计图、签名、posting 与索引结构容量，避免 std::shared_ptr 等共享引用重复计量；同时输出 posting 纯 payload 字节。
- [ ] 通过系统接口采集 RSS 高水位、索引后 RSS 和线程 CPU 时间；Linux `ru_maxrss` 转 bytes，避免单位错用。
- [ ] `--diagnostics workers` 记录每线程累计 task/steal/idle 墙钟以及任务计数；默认关闭任务级计时，仍保留轻量 C/F/V。
- [ ] 若实现匹配期 RSS 采样，只在内存诊断运行开启并标明周期与开销；不能用进程全程峰值减起始值冒充查询峰值。
- [ ] 保存输入哈希、标签模式、计数语义、顺序、工具链/构建参数和硬件信息；Count 的 128 位 在 JSON 写成十进制字符串。
- [ ] 运行 `make test TEST=metrics_contract`；在测试时钟下验证阶段边界，不设与硬件相关的“必须快于某秒”断言。

**通过条件：**设计 §9 的所有必需指标都可从单次报告或多次报告汇总得到，字段不重名混义，无无依据的加速比。

### 任务 12：超时、失败与长模式边界

**文件：**`error.cpp`、`scheduler.cpp`、`set_ops.cpp`、`candidates.cpp`、`validation.cpp`、`cli.cpp`、相关测试。

**输入接口：**deadline、取消状态、错误通道。**输出接口：**一致状态、退出码与不完整结果。

- [ ] 建立受控截止时钟测试，在 plan/任务边界及长集合循环触发取消；结果不得表现为成功的少计数。
- [ ] 异常线程通知其他线程，停止创建后继任务，退出时完成资源回收并保留错误原因。
- [ ] 用受控小上限计数器测试溢出分支；正式版本仍使用 Count 的 128 位，不故意构造无法运行的超大枚举来测溢出。
- [ ] 数据/查询文件错误输出路径及物理行号，返回 2；运行超时返回 124；内部错误返回 1。
- [ ] 构建 65 条嵌套查询边及相同数据图，边大小依次 1–65，所有标签相同；每种大小只有一条边，期望计数 1，验证动态 profile 无位宽上限。
- [ ] 测试空数据边集对非空合法查询返回完整 0；空查询或不连通查询明确拒绝。

**通过条件：**没有无限重试、溢出回绕、固定 4/64 边限制或错误状态伪装成完整计数。

### 任务 13：小图随机差分与标签语义审计

**文件：**`tools/differential.py`、oracle、测试 fixtures、`docs/reproduction_report.md`。

**输入：**生产算法测试入口和独立 oracle。**输出：**可重现的差分记录与失败反例。

- [ ] 用固定种子生成数据顶点数 1–8、边数 0–10、标签数 1–3 的简单超图；查询顶点不超过 6，查询边数 1–6，保证非空连通或作为预期错误用例。
- [ ] 同时生成“从数据采样的有解查询”和独立生成的可能无解查询，避免只测试正例。
- [ ] 初始固定 200 个可穷举小案例，两种标签模式逐个比较完整元组集合；失败立即保存数据/查询/seed/模式/顺序/差异元组。
- [ ] 对一部分案例改变边顺序和顶点编号，并通过逆置换比较结果；多线程 seed 改变不能影响计数。
- [ ] 对同一拓扑修改标签：无标签分区、匹配顺序、候选集合、最终元组必须不变。
- [ ] 审计无标签静态实例：上下文无标签数组，签名只大小，候选函数和 profile 键无标签值；共享库中存在有标签代码不是错误，关键是无标签生产实例不执行它。
- [ ] 完整运行时断言 `unlabeled_count >= labeled_count`；同时检验三种调度结果一致。
- [ ] 修复发现的问题后只补必要反例再复测，不以额外优化“修复”性能或计数。

**建议命令：**

```bash
python3 tools/oracle.py --self-test
python3 tools/differential.py --seed 20260908 --cases 200 --max-data-vertices 8 --max-query-vertices 6
make test
```

**通过条件：**全部确定性和随机小图通过；无未解释的计数差异。oracle 仅用于测试，不出现在正式计时路径。

### 任务 14：附件 HB 的集成验证

**文件：**`tests/hb_contract.cpp`、`experiments/hb_example.json`、README 示例、复现报告。

**输入：**用户原始附件。**输出：**两种标签模式可直接运行的命令、真实指标文件及核验结论。

- [ ] 先核对设计附录 A 中四个文件哈希，确认输入未被其他预处理修改。
- [ ] 核对任务 2/4/5 的统计、签名基数和顺序；有差异先解释输入/去重/编号问题，不开始性能比较。
- [ ] 使用以下真实路径语义分别运行两种模式，`io/` 为解压后的目录。数据一基、查询零基均为默认值，下列命令无需编号参数；另验证显式指定 0 的结果与省略一致。

```bash
make -j4 release
mkdir -p results

./build/bin/hgmatch_labeled \
  --data-edges io/HB/hypergraph.txt \
  --data-nodes io/HB/node.txt \
  --query-edges io/pattern_HB_4/patternEdge \
  --query-nodes io/pattern_HB_4/patternNode \
  --threads 1 --scheduler lifo-steal \
  --timeout-seconds 3600 \
  --metrics-file results/HB_labeled_t1.json

./build/bin/hgmatch_unlabeled \
  --data-edges io/HB/hypergraph.txt \
  --data-nodes io/HB/node.txt \
  --query-edges io/pattern_HB_4/patternEdge \
  --query-nodes io/pattern_HB_4/patternNode \
  --threads 1 --scheduler lifo-steal \
  --timeout-seconds 3600 \
  --metrics-file results/HB_unlabeled_t1.json
```

- [ ] 制作只修改奇数行整数的模式测试副本，保留全部偶数行和标签，验证两种标签模式各自的解析图、顺序和完整计数均不变；另用两行记录的单顶点模式验证偶数行单个 ID 不会被忽略。原附件保持原样。
- [ ] 分别用 2/4/8 线程对同一模式运行，完整时与各自单线程计数一致；超时只报告不完整状态，不以部分计数作相等比较。
- [ ] 对 HB 的可控小子图跑 oracle 差分，给完整 HB 大图结果增加独立语义证据。
- [ ] 把真实 `embedding_count`、`matching_time_s`、完成状态和硬件写入报告；当前文档不预设这些数值。

**通过条件：**附件原格式可直接运行；若某模式超时，必须明确记录，不能称完整 HB 性能验收已通过。不得将附件结果和论文 HB 表中统计不一致归咎于算法而随意删边。

### 任务 15：论文实验脚本及统计导出

**文件：**`sample_queries.py`、`run_experiments.py`、`summarize.py`、两份实验配置、schema。

**输入：**规范化数据、查询文件、两个二进制。**输出：**逐运行 JSON、分实验 CSV/Markdown 汇总；编译后的匹配程序仍只有两个。

- [ ] 采样器按 q2=(2,[5,15])、q3=(3,[10,20])、q4=(4,[10,30])、q6=(6,[15,35]) 设置，每设置目标 20 个查询，保存种子和顶点/边映射。
- [ ] 随机游走从当前连通区域选择相邻数据边，拒绝重复边，精确达到目标边数后检查顶点范围；有限次失败给出 `sampling_failed`，不得无限循环或改变用户设定。
- [ ] 所有采样模式统一输出固定两行记录：每条超边前写整数占位行 0，下一行写真实顶点 ID 列表；单顶点边也按两行写出。模式 ID 零基、标签文件独立逐行输出，运行清单仅记录 ID 起点，不提供格式开关。
- [ ] 每查询每模式每配置独立进程重复 3 次，保存 stdout/stderr、退出码、原始报告；不同配置均使用相同查询文件。
- [ ] runner 的外部看门狗作为进程无法响应时的保护，时限比程序 deadline 留出退出清理宽限；杀死进程单独标记，不把剩余 stdout 当完整 JSON。
- [ ] 汇总每查询三次均值，再对同组查询平均；失败状态单独列出。论文 3600 秒超时规则单列 `paper_capped_time_s`，不覆盖原始实际时间。
- [ ] 导出数据统计、索引时间与大小、计数时间、完成率、每层候选过滤、线程配置时间、调度内存、每线程工作量等表。
- [ ] 可扩展性线程列表默认提供 [1,2,4,8,16,20,40,60] 配置；运行器记录实际物理/逻辑核数，缺少硬件条件时标为未运行而不是伪造结果。
- [ ] Exp-5/6 在同查询和同线程数下分别执行主调度、BFS/NOSTL；逐线程诊断另跑，不与无诊断主性能重复混合。
- [ ] 用 2 个小查询、3 次重复的微型清单验证汇总公式，包含 1 个受控超时和 1 个输入错误，确认平均/完成率分母准确。

**建议命令：**

```bash
python3 tools/run_experiments.py --manifest experiments/hb_example.json --repeats 3 --output results/hb
python3 tools/summarize.py --input results/hb --output results/hb_summary
```

**通过条件：**设计 §9 的每个论文指标都有原始数据到汇总列的明确来源。附件只有 HB 和一个 q4 模式时，报告如实说明其他论文数据/案例尚未运行。

### 任务 16：README、实现记录、论文一致性审查与最终交付

**文件：**README、`docs/IMPLEMENTATION_STATUS.md`、`docs/reproduction_report.md`、最终项目压缩包。

- [ ] 按设计 §12.3 编写根目录 README.md 的“如何编译并运行”：列明实测环境、依赖位置、make/release/debug/test/clean 命令、两个二进制路径、从项目根目录运行的有/无标签完整命令。附带不传编号参数的 HB 示例、显式 query-id-base 1 的一基模式示例，以及线程/超时/调度/诊断的默认值和用法。
- [ ] 编写 README 的“输出的各项指标或字段的含义”：从最终 metrics.schema.json 提取字段清单，逐一填写类型、单位、统计边界、适用状态、null/未采集含义。包括嵌套 per_depth 和 workers 成员、完整/部分计数、全部阶段耗时、图/索引/RSS 内存、C/F/V、窃取/负载、输入统计、复现元数据、状态/退出码。每个 schema 字段均可在字典定位，不只解释摘要。
- [ ] 编写 README 的“项目文件的组织如何，每个文件的作用”：根据最终包中的实际路径核对逐文件职责表，每个 include/hgmatch/*.hpp、src/*.cpp、tests 文件、tools 脚本、实验配置、schema 和文档分别说明；配对头文件/实现文件须分别标明声明与实现职责。第三方按组件说明原文件/许可证/依赖清单，build 产物按生成规则说明。
- [ ] 补充 README 的输入两行协议与单顶点示例、标签编号、去重与计数语义、论文复现边界、测试/采样/重复实验步骤、常见错误、已知限制、许可证和参考资料，并链接实现记录及实测报告。
- [ ] 完整更新 docs/IMPLEMENTATION_STATUS.md：记录代码版本或源码摘要、日期、环境；逐模块填“功能、论文算法步骤、头文件、实现文件、关键函数、有/无标签覆盖、实现状态、验证状态、证据和限制”。区分未实现/部分实现/已实现未验证/已实现且验证通过；不能用计划勾选代替实际证据。
- [ ] 实现记录至少覆盖读取/去重、签名/索引、Algorithm 3/4/5、全部算子、LIFO/半队列窃取、NOSTL/BFS、计数/超时/错误、指标、Makefile、README、oracle/差分/采样/实验脚本。将用户无标签扩展和 C++ 语言调整列为明确工程差异，未完成项写具体原因。
- [ ] 在干净解压目录按 README 实际运行编译和两个程序示例，测试默认 query-id-base 0 与显式 0 完全一致；验证 make clean 保留预先放入 io/results 的文件，头文件更改使相关对象重建，debug/release 不串用。
- [ ] 将 Algorithm 3/4/5 每一步映射到实际函数、对应测试和论文页码；明确修正的排版符号、无标签扩展及工程计时约定。
- [ ] 阅读两个静态模式的完整调用链，确认没有额外过滤器、顶点回溯生产路径或 HB 特判。
- [ ] 确认默认匹配采用 LIFO/工作窃取，BFS 仅为显式消融，没有误用 Algorithm 2 的 BFS 伪代码作为主执行引擎。
- [ ] 核对所有计数状态、单位、固定读取协议、奇数行整数忽略规则以及 HB 与论文数据差异说明。
- [ ] 在干净环境运行最终构建、确定性测试、小图差分和附件集成命令。记录无法执行的硬件测试，不能标为通过。
- [ ] 执行 `make clean`、`make -j4 release`、`make test`、`make help`；代码审查产生修改后仅重跑受影响的必要验证。
- [ ] 清理 `build/`、大日志、临时 oracle 输出和未使用依赖；保留头文件/实现源码、Makefile、依赖清单及许可证、测试、指标 schema、README、实现记录和实验脚本。
- [ ] 打包项目并检查压缩包相对路径和构建说明，注明实际验证过的 OS/架构；用户未要求预编译跨平台包时不宣称任意平台二进制可运行。

**最终交付条件：**目录按 include/src 分离、根目录 Makefile 直接构建两个二进制，README 的三项强制说明完整，项目包含如实更新的实现记录；所有正确性闸门通过；输入与计数口径明确；每项指标可输出或通过规范重复实验汇总；论文偏差已记录，无未解释的计数错误。

## 6. 开发顺序与停止条件

依赖顺序为：工程 → 输入规范化 → 真值语义 → 分区/索引 → 查询计划 → 候选 → 验证 → 单线程算子 → 多线程调度 → 消融 → 指标与失败控制 → 差分 → HB → 实验脚本 → 交付审查。

任务 3 的 oracle 可以在任务 4 之前独立完成；指标 schema 草案可以早建，但最终时钟必须在真实执行路径上验收。不能在单线程计数尚未正确时用多线程加速掩盖错误，也不能为了赶进度省掉无标签热路径审计。

遇到下列情况暂停当前后续步骤，先在对应模块解决：

| 现象 | 首先检查 |
|---|---|
| 模式被读成 8 条边 | 查询读取入口是否错误地逐行读边，未固定忽略奇数行 |
| 70/436 顶点越界 | 是否错误地将元数据行当作超边 |
| 标签模式计数离谱 | 数据一基、模式零基是否独立配置；是否用错标签物理行 |
| HB 唯一边数不是 54,933 | 文件哈希、逗号解析、行内/边去重身份 |
| 无标签分区仍有 5,116 个 | 仍按标签签名分区，没有真正改为大小签名 |
| 无标签改标签后候选变化 | 候选/顺序/索引仍残留标签约束 |
| 高阶反例被接受 | 是否只验两两交叠，或丢失 profile 的重复次数 |
| 同标签单边计数为 6 | 错误累计顶点排列 |
| 两条对称边只计 1 | 额外做了自同构去重或元组排序去重 |
| 多线程计数偶尔不同 | 任务发布顺序、重复消费、窃取、outstanding 和局部汇总 |
| 完成率高但结果明显不足 | 是否超时被当成成功、取消被转换为空候选 |
| “计数时间”包含数秒读取 | 时钟边界混入离线阶段 |

## 7. 需求到任务的验收映射

| 用户要求 | 设计对应 | 执行任务 | 最终证据 |
|---|---|---|---|
| 指定数据/模式文本格式 | §3–4 | 2、14 | 原附件直接运行、忽略行不变性及单顶点边测试 |
| 与论文一致的 CPU 挖掘逻辑 | §5–8 | 4–10、16 | Algorithm 2–5 和 §VI 到源码的映射 |
| 支持不同模式 | §4、§7、§11 | 5、7、12、13、15 | 1–65 边、标签/拓扑/交叠变化及随机差分 |
| 有标签/无标签两个可执行文件 | §1、§6 | 1、4、6、7、13 | 两个 bin 构建记录和无标签路径审计 |
| 论文实验指标，计数时间必须输出 | §9–10 | 11、14、15 | 真实 JSON、schema、全部实验汇总字段 |
| 查询 ID 默认 0 | §4.1 | 1、2、14、16 | 默认与显式 0 等价，显式 1 单独测试 |
| include 头文件 / src 实现规范与 Makefile | §12.1–12.2 | 1、4–12、16 | 干净构建、目录审计、头文件依赖与增量构建 |
| README 三项详细说明 | §12.3 | 1、11、14、16 | 编译运行实测、schema 字段覆盖、逐文件职责覆盖 |
| 项目包含具体实现记录 Markdown | §12.4 | 每任务更新、16 终审 | docs/IMPLEMENTATION_STATUS.md 的实际状态和验证证据 |
| 本轮只修改设计和计划 | 本文开头 | 当前轮次 | 仅两份 Markdown，不提交 HGMatch 实现 |

## 8. 下一轮执行者必须保留的结论

1. 所有模式固定两行一组：直接忽略奇数行整数，偶数行读取真实超边，允许单顶点；无格式参数或兼容分支。本附件模式零基、HB 数据一基。
2. 本附件规范化数据有 54,933 条边，不能为了接近论文 52,960 而任意删除更多边。
3. HGMatch 需要完整 profile 多重集验证；无标签只移除标签分量，不移除局部度数和高阶关联约束。
4. 计数按查询位置上的数据超边元组，不能改成顶点单射数或无序子图出现次数。
5. CPU 并行 LIFO 与工作窃取是论文原系统的一部分，不能将其当作不允许复现的新增优化。
6. 本文尚未给出任何真实 HB 挖掘计数/时间；这些值必须在后续实现、验证和实际运行后填写。

7. 工程采用 C++20 与 Makefile，自有头文件放 include/hgmatch，实现放 src。查询 ID 默认 0；README 必须解释编译运行、全部输出字段及逐文件职责，docs/IMPLEMENTATION_STATUS.md 必须如实记录完成范围和验证证据。
