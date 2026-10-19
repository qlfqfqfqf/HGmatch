# GPU迁移交付与验证记录

日期：2026-10-12。目标服务器：Linux、RTX 5090 32 GB、CUDA Toolkit 13.2。服务器目前无法连接，按用户要求暂不执行服务器验收。

## 已实现

CPU保留输入、精确去重、分区/倒排索引、算法3、静态查询元数据、CUDA控制和报告。CUDA执行SCAN、算法4、算法5、部分嵌入扩展和128位GPU计数。生产程序只有hgmatch_gpu_labeled与hgmatch_gpu_unlabeled，无CPU回退。源码、Linux Makefile、依赖、测试、实验脚本与说明完整提供。

算法4/5使用单计算stream CUDA Events，输出全次挖掘的阶段区间总和及逐层时间，不累加GPU线程时间。报告版本4，含backend与timing_method。显存预算覆盖全部前缀缓冲、候选/标记、GPU计数与CUB临时空间，预算不足时缩小批次；最低空间不足明确失败。

## 本机已经完成的检查

环境是WSL Ubuntu 22.04、GCC 11.4、本机RTX 5070 Laptop GPU（8151 MiB），项目build目录内的NVIDIA官方CUDA 13.2组件（nvcc 13.2.78）。本机较小GPU的验证只证明功能，不代表服务器性能。

- CUDA编译成功，生成两个sm_120 GPU可执行文件和GPU测试驱动；交付源码包已解压到独立目录，从干净源码构建成功。
- 11项主机测试通过，覆盖输入、去重/索引、算法3、布局、动态65边profile及GPU计数带进位/溢出函数。
- 12项GPU测试通过，覆盖匹配语义、profile拒绝、65边查询、不同批次/窗口计数一致、独立计时和受控取消。
- CLI、GPU专属失败路径、CPU参数拒绝、无GPU不回退、显存预算错误、实验runner与汇总导出均通过。
- 两个种子20250330与20260908各40图、104次GPU完整元组集合比较，共208次与独立oracle一致，且没有重复元组。
- GPU v4 JSON Schema成功/输入失败及必需算法时间字段校验通过。

证据保存在原工作目录results/gpu_local中，不将旧CPU results/verified作为GPU验证证据。服务器验证脚本已提供；本机编译SDK仅在build目录，不包含在交付源码包。

## 服务器暂缓验收

尚未在RTX 5090服务器上执行：干净源码编译与运行、HB完整计数954/2043072、服务器显存边界、200案例规模差分、Compute Sanitizer、Nsight Systems计时边界检查和三次重复性能实验。上述检查是未来服务器验收，不声称已经通过；本次代码交付按用户要求不以连接服务器为前提。

源码包：dist/hgmatch_gpu_source.tar.gz。编译、运行和后续验收命令见README。
