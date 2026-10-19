.DEFAULT_GOAL := all
CXX ?= g++
NVCC ?= nvcc
CUDA_ARCH ?= 120
MODE ?= release
OPT_release := -O3 -DNDEBUG
OPT_debug := -O0 -g
CPPFLAGS += -Iinclude -Ithird_party -Ibuild/generated
HOST_FLAGS = -std=c++20 -Wall -Wextra $(OPT_$(MODE)) $(CXXFLAGS)
CUDA_FLAGS = -std=c++20 -arch=sm_$(CUDA_ARCH) $(OPT_$(MODE)) -lineinfo $(NVCCFLAGS)
OBJDIR = build/obj/gpu-$(MODE)
BINDIR = $(if $(filter debug,$(MODE)),build/bin/debug,build/bin)
HOST_NAMES = error input normalize mode storage inverted_index plan types metrics resources gpu_storage
HOST_OBJS = $(addprefix $(OBJDIR)/,$(addsuffix .o,$(HOST_NAMES)))
GPU_OBJS = $(HOST_OBJS) $(OBJDIR)/cli.o $(OBJDIR)/gpu_match.o
HOST_TEST_NAMES = input_contract storage_contract plan_contract metrics_contract gpu_layout_contract
HOST_TEST_OBJS = $(addprefix $(OBJDIR)/tests/,$(addsuffix .o,$(HOST_TEST_NAMES))) $(OBJDIR)/tests/support/test_harness.o
GPU_TEST_OBJS = $(OBJDIR)/tests/algorithm_contract.o $(OBJDIR)/tests/gpu_contract.o
.PHONY: all release debug FORCE test test-host test-drivers test-tools test-hb test-schema clean help package
all: $(BINDIR)/hgmatch_gpu_labeled $(BINDIR)/hgmatch_gpu_unlabeled
release: all
debug:
	$(MAKE) MODE=debug all
FORCE:
build/generated/host_config.hpp: FORCE
	@mkdir -p build/generated
	@python3 tools/build_config.py --compiler '$(CXX)' --flags '$(CPPFLAGS) $(HOST_FLAGS)' --output $@
build/generated/build_config.hpp: FORCE
	@mkdir -p build/generated
	@python3 tools/build_config.py --compiler '$(CXX)' --cuda-compiler '$(NVCC)' --arch '$(CUDA_ARCH)' --flags '$(CPPFLAGS) $(HOST_FLAGS) $(CUDA_FLAGS) $(LDFLAGS) $(LDLIBS)' --output $@
$(OBJDIR)/%.o: src/%.cpp build/generated/host_config.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(HOST_FLAGS) -MMD -MP -c $< -o $@
$(OBJDIR)/cli.o: build/generated/build_config.hpp
$(OBJDIR)/gpu_match.o: src/gpu_match.cu build/generated/build_config.hpp
	@mkdir -p $(dir $@)
	$(NVCC) $(CPPFLAGS) $(CUDA_FLAGS) -MMD -MP -c $< -o $@
$(OBJDIR)/tests/%.o: tests/%.cpp build/generated/host_config.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Itests $(HOST_FLAGS) -MMD -MP -c $< -o $@
$(BINDIR)/hgmatch_gpu_labeled: $(GPU_OBJS) $(OBJDIR)/main_labeled.o
	@mkdir -p $(BINDIR)
	$(NVCC) $(CUDA_FLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@
$(BINDIR)/hgmatch_gpu_unlabeled: $(GPU_OBJS) $(OBJDIR)/main_unlabeled.o
	@mkdir -p $(BINDIR)
	$(NVCC) $(CUDA_FLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@
build/tests/host_contracts: $(HOST_OBJS) $(HOST_TEST_OBJS)
	@mkdir -p build/tests
	$(CXX) $^ $(LDFLAGS) $(LDLIBS) -o $@
build/tests/gpu_contracts: $(GPU_OBJS) $(GPU_TEST_OBJS) $(OBJDIR)/tests/support/test_harness.o
	@mkdir -p build/tests
	$(NVCC) $(CUDA_FLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@
build/tests/gpu_driver: $(GPU_OBJS) $(OBJDIR)/tests/driver.o
	@mkdir -p build/tests
	$(NVCC) $(CUDA_FLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@
test-host: build/tests/host_contracts
	./build/tests/host_contracts
	python3 tests/experiment_contract.py
	python3 tools/oracle.py --self-test
test: test-host all build/tests/gpu_contracts test-drivers
	./build/tests/gpu_contracts
	python3 tests/cli_contract.py
	python3 tests/gpu_cli_contract.py
	python3 tests/gpu_experiment_contract.py
test-drivers: build/tests/gpu_driver
test-tools:
	python3 tests/experiment_contract.py
	python3 tools/oracle.py --self-test
test-hb: all
	python3 tests/hb_contract.py --io '$(IO_DIR)'
test-schema: all
	python3 tests/schema_contract.py
package:
	python3 tools/package_source.py
clean:
	rm -rf build
help:
	@echo 'CUDA GPU mining only; CPU preprocessing retained.'
	@echo 'make -j4 NVCC=/usr/local/cuda/bin/nvcc CUDA_ARCH=120'
	@echo 'Targets: all debug test-host test test-drivers test-hb test-schema package clean'
	@echo 'Overrides: CXX NVCC CUDA_ARCH CPPFLAGS CXXFLAGS NVCCFLAGS LDFLAGS LDLIBS'
-include $(wildcard $(OBJDIR)/*.d $(OBJDIR)/tests/*.d $(OBJDIR)/tests/support/*.d)
