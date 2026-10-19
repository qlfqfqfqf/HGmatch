# 本轮代码审查修订验证报告

修复内容与论据详见CODE_REVIEW.md，执行计划见REVIEW_AND_FIX_PLAN.md。本报告只使用修订后源码生成的结果。

## 已完成验证

- make -j4 test test-drivers：20项C++契约测试及CLI、驻留线程池、实验统计、oracle自检通过。
- 确定性偷取顺序测试在修复前失败，修复后通过；工作中受控timeout和未知异常均能回收所有worker。
- 两个种子20250330与20260908，各200图、760次完整超边元组集合比较，总计1520次通过，包含无标签模式的标签变形检查和三个调度。
- 独立干净目录构建AddressSanitizer+UndefinedBehaviorSanitizer，20项测试通过；ASAN_OPTIONS=detect_leaks=0。未在本轮声称通过LeakSanitizer/ThreadSanitizer。
- HB主调度两模式1/2/4/8线程全部完成；额外两模式4线程NOSTL和BFS开启workers诊断均完成；完整计数一致，每worker实际线程启动一次。
- JSON Schema v2校验成功和提前输入错误报告通过。

结果文件：contracts.txt、asan_ubsan.txt、build_checks.txt、differential*.json及hb_*.json。报告中的路径与环境为当次执行来源，不要求用户在其它机器使用相同绝对路径。

## HB实测

| 标签模式 | 调度 | 线程 | 诊断 | 完整计数 | matching_time_s |
| --- | --- | --- | --- | --- | --- |
| labeled | bfs | 4 | workers | 954 | 0.002817416 |
| labeled | lifo-nosteal | 4 | workers | 954 | 0.004044915 |
| labeled | lifo-steal | 1 | none | 954 | 0.006142275 |
| labeled | lifo-steal | 2 | none | 954 | 0.003195579 |
| labeled | lifo-steal | 4 | none | 954 | 0.002408547 |
| labeled | lifo-steal | 8 | none | 954 | 0.001800684 |
| unlabeled | bfs | 4 | workers | 2043072 | 3.247799083 |
| unlabeled | lifo-nosteal | 4 | workers | 2043072 | 4.870771137 |
| unlabeled | lifo-steal | 1 | none | 2043072 | 10.506261830 |
| unlabeled | lifo-steal | 2 | none | 2043072 | 5.614085542 |
| unlabeled | lifo-steal | 4 | none | 2043072 | 2.912764157 |
| unlabeled | lifo-steal | 8 | none | 2043072 | 1.576283349 |

源码整体SHA256：`17b477b052b530e389b6afbc76cabecd8f627856198e33fa027cc44fc1b99d8b`。

构建：g++ (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0；CPU：AMD EPYC 9V74 80-Core Processor。

以上是单次功能验收时间，不作为稳定性能排名；正式实验请用runner在目标机器相同资源条件下按论文协议重复3次。诊断开关不同的时间不可混为一组。

HB原始60987条边，规范化54933条边，1494顶点、1216701关联；模式4边29顶点。标签版5116分区、Card=[921,10,2738,1838]、顺序[1,0,3,2]；无标签341分区、Card=[4369,430,7656,7656]、顺序[1,0,2,3]。

## 论文数值复现边界

代码审查核对了核心算法、数据结构、调度、消融与实验入口；其它九个数据集、作者随机查询全集和原硬件未提供，未重跑原论文所有图表和知识库案例。附件HB规范化边数与论文Table II不同。未构造假的其它系统加速比。

图/索引字节为公开的拥有容量估算，RSS是整个独立进程的峰值；设计中的可选匹配期RSS采样仍为null。此次修复不改变计数语义或增加额外匹配剪枝。
