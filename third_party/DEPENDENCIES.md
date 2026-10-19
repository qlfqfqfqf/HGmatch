# 依赖

- nlohmann/json 3.11.3：随源码提供，MIT许可证位于nlohmann/LICENSE；仅用于JSON报告。
- NVIDIA CUDA Toolkit 13.2：服务器需预先安装，提供nvcc、CUDA Runtime、CUB和Thrust；使用其随附许可证。正常项目编译不下载依赖。
- Python标准库：实验、采样、oracle、源码打包。
- jsonschema>=4,<5：可选测试依赖，见requirements-test.txt；不参与挖掘或构建。

旧CPU工作窃取队列已不属于GPU项目。build_config.py记录include/src/Makefile/构建脚本的整体源码摘要，以及third_party的整体依赖摘要；CUDA编译器版本与架构另行记录。
