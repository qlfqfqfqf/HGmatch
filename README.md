# HGMatch GPU 超图匹配（CUDA，CPU 预处理）

完整源码项目，目标服务器为 Linux / RTX 5090 32 GB / CUDA Toolkit 13.2。保留 CPU 的文件读取、精确去重、签名分区、倒排索引、算法3、CUDA启动和JSON输出；SCAN、算法4、算法5、嵌入扩展和匹配计数全部由 CUDA 执行。不提供 CPU 挖掘后端，不在 CUDA 不可用时回退到 CPU。

GPU版本保留论文 HGMatch 的匹配语义与过滤条件，GPU调度采用有界分批深度推进，替换论文的CPU线程LIFO/工作窃取实现。因此这是HGMatch核心算法的GPU迁移，不能将其称为作者原CPU调度实现，也不预先声称复现论文性能数值。

## 1. 服务器编译

需要 Linux x86_64、GNU Make、Python 3.9+、支持C++20且受CUDA 13.2支持的GCC、CUDA Toolkit 13.2和可工作的NVIDIA驱动。建议GCC 11–15。JSON头文件随项目提供；CUB/Thrust使用CUDA Toolkit自带版本，正常构建不联网。无需额外图计算库。

```bash
make -j4 NVCC=/usr/local/cuda/bin/nvcc CUDA_ARCH=120
```

生成 `build/bin/hgmatch_gpu_labeled` 和 `build/bin/hgmatch_gpu_unlabeled`。两个程序共享GPU后端，标签模式编译期固定。`CUDA_ARCH=120`针对RTX 5090；可通过`CXX`指定主机编译器，通过`NVCCFLAGS`、`CPPFLAGS`、`CXXFLAGS`、`LDFLAGS`、`LDLIBS`添加参数。`make debug`生成`build/bin/debug/`中的GPU程序。不要并发执行不同配置的make。

`make clean`只删除build目录。`make package`生成`dist/hgmatch_gpu_source.tar.gz`和SHA256，包内包含完整源码、依赖、测试、脚本和HB输入，不包含本机CUDA下载、编译产物或历史CPU记录。源码包可以直接解压到服务器后按上述命令编译。

## 2. 运行与输入

```bash
./build/bin/hgmatch_gpu_labeled \
  --data-edges io/HB/hypergraph.txt --data-nodes io/HB/node.txt \
  --query-edges io/pattern_HB_4/patternEdge --query-nodes io/pattern_HB_4/patternNode \
  --gpu-id 0 --metrics-file results/hb_gpu_labeled.json

./build/bin/hgmatch_gpu_unlabeled \
  --data-edges io/HB/hypergraph.txt --data-nodes io/HB/node.txt \
  --query-edges io/pattern_HB_4/patternEdge --query-nodes io/pattern_HB_4/patternNode \
  --gpu-id 0 --metrics-file results/hb_gpu_unlabeled.json
```

完整JSON始终写stdout；`--metrics-file`另存相同报告并创建父目录。生产程序只输出计数与统计，不逐个打印嵌入。历史CPU真值对应HB有标签954、无标签2043072；GPU服务器尚未验收，这些数值是验收目标，不是服务器GPU实测结果。

输入协议保持原样：

- 数据超边文件每个物理行一条超边，顶点可由逗号或空白分隔；标签文件每行一个有符号64位标签。数据默认从1编号。
- 查询`patternEdge`严格两行一组，奇数行必须是整数文本但其值忽略，偶数行才是超边，允许单顶点。查询默认从0编号。
- `--data-id-base`、`--query-id-base`只允许0或1；不要自动猜编号或奇数行的意义。
- 支持CRLF和末行没有换行；拒绝空白物理行、非法整数、越界编号、缺少查询偶数行等。
- 先做边内顶点去重，再按完整顶点集合去除重复超边；不能按签名合并不同超边。查询必须非空、连通且没有孤立顶点。
- 一次计数是按查询边位置排列的完整数据超边映射元组；不乘顶点排列数，不除查询自同构。完整边集合和交叠结构必须匹配，不采用包含匹配。

| 参数 | 默认 | 含义 |
| --- | --- | --- |
| `--data-edges` / `--data-nodes` / `--query-edges` / `--query-nodes` | 必填 | 输入文件 |
| `--data-id-base` / `--query-id-base` | 1 / 0 | 外部顶点编号起点 |
| `--gpu-id` | 0 | CUDA逻辑设备编号，遵循CUDA_VISIBLE_DEVICES |
| `--gpu-memory-mb` | 0 | 0为启动时可用显存的80%；正值为显式预算，单位MiB |
| `--batch-capacity` | 65536 | 每批候选/子嵌入的容量上限，1..INT_MAX；预算不足时自动缩小 |
| `--candidate-tile-size` | 65536 | 分区内候选编号窗口的上限，实际窗口同时受批次容量限制 |
| `--timeout-seconds` | 3600 | CPU算法3至GPU匹配的在线deadline；0不限时，包含GPU准备与上传 |
| `--metrics-file` | 不另存 | 完整JSON副本路径 |
| `--help` | — | 帮助 |

CPU挖掘参数`--threads`、`--scheduler`、`--diagnostics`、`--seed`已移除，传入会明确报输入错误。查询采样脚本自己的`--seed`仍保留。

退出码：成功0，输入错误2，超时124，其余失败1。CUDA不可用为`gpu_unavailable`，CUDA执行失败为`cuda_error`，显存/内存不足为`out_of_memory`，计数溢出为`count_overflow`。只有`status=ok`的`embedding_count`才是完整计数；其他状态完整计数为null，已完成GPU SINK的计数单独放在`partial_embedding_count`。

## 3. 算法4与算法5的独立时间

运行时默认输出以下字段，单位秒，不需要开启额外诊断：

| 字段 | 含义 |
| --- | --- |
| `candidate_generation_time_s` | 全次匹配中算法4的CUDA Event阶段区间之和，含候选并交与候选压缩 |
| `embedding_validation_time_s` | 全次匹配中算法5的CUDA Event阶段区间之和，含顶点并集数检查及完整profile多重集比较 |
| `matching_time_s` | GPU SCAN开始前至最终GPU计数和统计取回完成的主机墙钟时间 |
| `host_to_device_time_s` | 匹配前上传图、索引及查询静态元数据并等待完成的墙钟时间 |
| `cuda_initialize_time_s` | 设备选择、CUDA上下文、stream与计时事件初始化时间 |
| `device_layout_time_s` | CPU将图/索引打包成连续GPU布局及准备静态查询约束、参考profile的时间 |
| `plan_time_s` | CPU算法3时间 |
| `online_query_time_s` | plan_time_s + matching_time_s；GPU准备/上传另列，不计入此和 |

算法4和算法5始终使用同一个非默认计算stream，阶段之间顺序执行，阶段内部GPU线程并行。因此两个字段是阶段经过时间，不累加各GPU线程时间，也不除以线程数。可以分别除以`matching_time_s`分析匹配流程时间构成；两项之和不等于`matching_time_s`，其余开销包括SCAN、结果压缩、子嵌入物化、计数、CPU批次控制、同步和控制信息传输。

CUDA Events测量的是阶段区间，包含该阶段内kernel间隔，不宣称纯指令执行时间。阶段内不进行动态显存分配或CPU匹配；计时对象预先创建并复用。`per_depth[]`及`last_depth`保留相同字段，顶层是各层之和。单边查询不调用算法4/5，两项为0；输入失败尚未进入算法时为null；计划超时未执行算法时为0。失败报告中的值只代表已经完成测量的区间，`timing_complete=false`，不能当作完整挖掘耗时。

报告`schema_version=4`、`backend=cuda`、`timing_method=cuda_events_single_stream`；旧CPU结果使用另一计时口径，不能混合比较。完整字段见[metrics_fields.md](docs/metrics_fields.md)。

## 4. 显存、分批与正确性

CSR形式的超边和分区倒排索引一次上传。部分嵌入按前缀长度放在GPU缓冲区；CPU只维护深度、批次位置、窗口游标等控制元数据，不执行候选判断、验证或SINK计数。每个候选窗口对应不相交的分区内边编号范围；前缀和游标保留至整个子树处理完成，保证窗口/批次变化不会丢失或重复计数。

分配前核算所有深度的前缀缓冲、候选/有效列表、标记、CUB临时空间和GPU计数器，自动减小批次容量。图/索引或最小工作空间不能装入预算时明确失败，不进行CPU或外存匹配。`gpu.peak_allocated_bytes`是项目分配器管理的峰值，不包含CUDA上下文/驱动内部空间；`peak_rss_bytes`是CPU进程峰值RSS，不能当作GPU显存。

GPU计数由单写者执行两个64位字的带进位128位归并，检测溢出。GPU验证用动态数量的64位关联字比较profile重复次数，支持65条及更多查询超边；不采用固定64位限制，也不以哈希相等代替精确相等。

超时在批次边界检查。不能强行中断正在运行的CUDA kernel，退出会等待已提交工作完成，所以可能超过deadline；外部进程强杀仍由runner记录，不冒充正常程序超时。

## 5. 测试与实验

```bash
# 无CUDA也能运行：CPU输入/计划/布局/计数进位/报告测试
make -j4 test-host

# 有可用CUDA设备：GPU正确性、分批、CLI与失败路径
make -j4 test NVCC=/usr/local/cuda/bin/nvcc CUDA_ARCH=120

# 独立oracle完整超边元组集合比较
python3 tools/differential.py --seed 20250330 --cases 200 --output results/gpu_differential.json

# 以下用于未来服务器验收，本次尚未执行服务器步骤
make test-hb IO_DIR=io
python3 -m pip install -r requirements-test.txt
make test-schema
compute-sanitizer --tool memcheck ./build/tests/gpu_contracts
compute-sanitizer --tool synccheck ./build/tests/gpu_contracts
```

oracle仅作为测试真值来源，不属于生产CPU挖掘后端。GPU测试收集完整元组的入口仅位于`build/tests/gpu_driver`，生产CLI不接受`--collect-file`。

```bash
python3 tools/run_experiments.py --manifest experiments/hb_example.json --repeats 3 --output results/gpu_runs
python3 tools/summarize.py --input results/gpu_runs --output results/gpu_summary
```

runner使用两个GPU程序，记录命令、退出码、stdout/stderr和完整JSON；拒绝旧CPU配置。汇总输出原始两项时间和`mean_candidate_generation_time_s`、`mean_embedding_validation_time_s`，先对每查询重复平均，再对同组查询等权平均；失败/缺失时间不填0。按GPU编号、批量/窗口、显存预算、后端和计时方法分组。一次汇总输入应来自相同GPU硬件和软件环境；完整GPU属性仍在每次原始JSON中保存。CPU线程扩展性/工作窃取消融不适用于此版本。

本机验证与服务器暂缓项目见[GPU_MIGRATION_REPORT.md](docs/GPU_MIGRATION_REPORT.md)。历史CPU设计和结果仅在原工作目录的历史位置保留，不作为GPU验收证据，也不包含在GPU源码交付包中。
