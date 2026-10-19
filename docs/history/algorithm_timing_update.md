# Algorithm 4 / Algorithm 5 独立计时修改与验证

验证日期：2026-10-12。

运行原有命令即可在 stdout JSON 和 --metrics-file 的 JSON 中得到 candidate_generation_time_s（算法4）与 embedding_validation_time_s（算法5），单位秒，默认启用。算法5包含顶点并集大小检查与 profile 多重集比较。两者各自计时并累加，不含嵌入复制、计数和任务入队；中断调用也保留已用时间。

每层和每线程在 per_depth / last_depth / workers 中有同名字段。多线程值是各线程调用区间的累计和，可能超过 matching_time_s，不能当作程序实际总耗时。时钟读取与统计增加少量测量开销。单边查询为0，匹配前输入失败为null，计划失败未执行算法为0。详见 README 与 metrics_fields.md。

当前报告版本为3；metrics.schema.json 兼容历史版本2，版本3要求新增字段。实验汇总导出原始时间和查询优先的平均时间，失败或旧结果缺失字段时不填0。results/verified 中历史验收结果未改写。

## 验证结果

- WSL Ubuntu 中 make -j4 test test-drivers 成功，22项C++契约测试通过，覆盖两种标签模式、三个调度、单/多线程、时间归并和只通过/失败检查的计时路径。
- CLI、驻留线程池、实验汇总和独立 oracle 自检全部通过。
- JSON Schema 校验通过：历史v2、当前v3、提前输入失败以及缺失新增字段的拒绝检查。WSL系统jsonschema版本过旧，因此使用本机已有的新版本jsonschema通过WSL执行测试程序，无需安装依赖。Linux环境运行 make test-schema 需要 jsonschema>=4。
- HB 两种模式各1/4线程完整运行，计数保持有标签954、无标签2043072；stdout与文件报告一致，总算法时间与逐层/逐线程之和一致。
- 下表为单次功能验收测量，不作为稳定性能比较。

| 模式 | 线程 | 完整计数 | matching_time_s | candidate_generation_time_s | embedding_validation_time_s |
| --- | --- | --- | --- | --- | --- |
| labeled | 1 | 954 | 0.0067037 | 0.0008325829999999998 | 0.005469113 |
| labeled | 4 | 954 | 0.002693261 | 0.001090832 | 0.006971655000000003 |
| unlabeled | 1 | 2043072 | 10.102485916 | 0.26145104600000174 | 9.485842034999726 |
| unlabeled | 4 | 2043072 | 2.565726427 | 0.2627457000000004 | 9.559794214000288 |

本次构建源码 SHA256：`ac7ee5ef2f60c9fadc66980590b901034381afd2d4afba325721089bee86cfa5`。

证据：[测试日志](../results/algorithm_timing/contracts.txt)、[HB验证日志](../results/algorithm_timing/verification.txt)、[Schema日志](../results/algorithm_timing/schema_validation.txt)，以及同目录的 hb_*.json。
