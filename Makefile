# ---------------------------------------------------------------------------
# CH572/CH592 项目 - 快捷构建入口
# ---------------------------------------------------------------------------
# 本 Makefile 只是 cmake 的薄封装，提供短命令。
#
# 用法：
#   make            编译（等价于 cmake --build build）
#   make flash      烧录（USB，wchisp）
#   make probe      探测 USB ISP 设备
#   make verify     校验 Flash
#   make erase      擦除 Flash
#   make size       显示固件大小
#   make config     重新配置 CMake
#   make clean      清理构建目录
#   make rebuild    清理后重新编译
#
# 目标芯片：默认取 default_target.txt 的 chip，
#           也可用 make config CHIP=CH592 指定。
# 目标固件：默认取 default_target.txt 的 target，
#           也可用 make flash T=<固件名> 指定。
#
# Windows 提示：若没有 make，可用 mingw32-make，或直接用 build.ps1 / build.bat。
# ---------------------------------------------------------------------------

# 构建目录
BUILD_DIR ?= build

# 工具链文件（相对路径，CMake 会基于当前目录解析）
TOOLCHAIN := sdk/common/cmake/riscv.cmake

# Ninja 可执行文件（不在 PATH 时指定）
NINJA ?= ninja

# 目标固件（空 = 用 SDK 的默认目标）
T ?=

# 目标芯片（空 = 用 default_target.txt 的 chip）
CHIP ?=

# 生成 --target 参数
ifneq ($(T),)
  TARGET_ARG := --target $(T)
  FLASH_TARGET := $(T)-flash
else
  TARGET_ARG :=
  FLASH_TARGET := flash
endif

# 生成 -DCHIP_SDK 参数
ifneq ($(CHIP),)
  CHIP_ARG := -DCHIP_SDK=$(CHIP)
else
  CHIP_ARG :=
endif

.PHONY: all config build flash probe verify erase size clean rebuild help

# 默认目标
all: build

# ---------------------------------------------------------------------------
# 帮助
# ---------------------------------------------------------------------------
help:
	@echo "CH572/CH592 项目快捷命令："
	@echo "  make            编译"
	@echo "  make flash      烧录（USB，wchisp）"
	@echo "  make probe      探测 USB ISP 设备"
	@echo "  make verify     校验 Flash"
	@echo "  make erase      擦除 Flash"
	@echo "  make size       显示固件大小"
	@echo "  make config     重新配置 CMake"
	@echo "  make clean      清理构建目录"
	@echo "  make rebuild    清理后重新编译"
	@echo ""
	@echo "指定固件：make flash T=usb_dual"
	@echo "指定芯片：make config CHIP=CH592"
	@echo "芯片与默认固件也可在 default_target.txt 中配置。"

# ---------------------------------------------------------------------------
# 配置
# ---------------------------------------------------------------------------
config:
	cmake -B $(BUILD_DIR) -G Ninja \
		-DCMAKE_MAKE_PROGRAM=$(NINJA) \
		-DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) \
		$(CHIP_ARG)

# 自动配置（build 目录不存在，或 default_target.txt 更新时）
$(BUILD_DIR)/build.ninja: default_target.txt
	@$(MAKE) config

# ---------------------------------------------------------------------------
# 构建
# ---------------------------------------------------------------------------
build: $(BUILD_DIR)/build.ninja
	cmake --build $(BUILD_DIR)

# ---------------------------------------------------------------------------
# 烧录与调试
# ---------------------------------------------------------------------------
flash: $(BUILD_DIR)/build.ninja
	cmake --build $(BUILD_DIR) --target $(FLASH_TARGET)

probe: $(BUILD_DIR)/build.ninja
	cmake --build $(BUILD_DIR) --target probe

verify: $(BUILD_DIR)/build.ninja
	cmake --build $(BUILD_DIR) --target $(if $(T),$(T)-verify,verify)

erase: $(BUILD_DIR)/build.ninja
	cmake --build $(BUILD_DIR) --target erase

size: $(BUILD_DIR)/build.ninja
	cmake --build $(BUILD_DIR) --target $(if $(T),$(T)-size,size)

# ---------------------------------------------------------------------------
# 清理
# ---------------------------------------------------------------------------
clean:
	-rm -rf $(BUILD_DIR)

rebuild: clean build
