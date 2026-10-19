# GPU迁移实现状态

生产挖掘仅使用CUDA，CPU保留输入、精确去重、签名分区、倒排索引、算法3、连续布局、设备启动和输出。

- gpu_storage.cpp：CPU构建GPU连续传输布局；posting保存分区内边编号，查询参考profile按匹配位置编码。
- gpu_match.cu：SCAN、posting并集与交集、顶点并集数、profile多重集精确比较、嵌入物化、GPU计数以及有界分批深度遍历。
- gpu_count.hpp：GPU计数使用双64位字带进位归并，每个计数器单写者，不使用不安全的双字原子拼接。
- cli.cpp / metrics.cpp：版本4、GPU参数、错误状态、CUDA Event独立计时、GPU预算与分配器峰值。
- Makefile：只生成两个GPU匹配程序；test-host保留CPU预处理检查，test包含GPU小图检查。
- tools：GPU实验runner、独立小图oracle差分、算法时间汇总、完整源码打包。

匹配语义保留精确超边元组计数；无标签模式静态移除标签比较；查询非空连通无孤立点；CPU线程调度、工作窃取及三个旧调度后端已移除。GPU调度改变工程执行方式，未加入新候选剪枝。query profile参考表在CPU准备一次，实际数据profile匹配与验证全部在GPU。

显存不足时缩小批次，索引或单条工作所需空间仍不能装入时明确失败；无CPU回退、无多GPU、无图索引外存执行。查询长度由动态profile字数支持，显存消耗会随长度增长。

本机功能验证与服务器尚未执行的验收分别记录，见GPU_MIGRATION_REPORT.md；不能用本机结果代替RTX 5090服务器性能。
