

CC = gcc
CXX = g++
AR = ar
RANLIB = ranlib

# 编译选项
CFLAGS = -Wall -Wextra -g -O0 -fPIC
CXXFLAGS = -Wall -Wextra -g -O0 -fPIC -std=c++11
LDFLAGS = -lm

# 检测目标架构
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_M),aarch64)
    TARGET_ARCH = arm64
    CFLAGS += -DRTC_BUILD_WITH_NEON
    CXXFLAGS += -DRTC_BUILD_WITH_NEON
else ifeq ($(UNAME_M),armv7l)
    TARGET_ARCH = arm
    CFLAGS += -DRTC_BUILD_WITH_NEON -mfpu=neon
    CXXFLAGS += -DRTC_BUILD_WITH_NEON -mfpu=neon
else ifeq ($(UNAME_M),mips)
    TARGET_ARCH = mips
else
    TARGET_ARCH = x86
endif

# 包含目录
INCLUDES = -Isrc \
           -Isrc/modules/audio_processing/aecm \
           -Isrc/modules/audio_processing/utility \
           -Isrc/common_audio \
           -Isrc/common_audio/signal_processing \
           -Isrc/common_audio/signal_processing/include \
           -Isrc/rtc_base \
           -Isrc/rtc_base/numerics

# 源文件目录
SRCDIR = src
OBJDIR = obj

# AECM核心源文件
AECM_SOURCES = $(SRCDIR)/modules/audio_processing/aecm/aecm_core.cc \
                $(SRCDIR)/modules/audio_processing/aecm/echo_control_mobile.cc \
                $(SRCDIR)/modules/audio_processing/utility/delay_estimator_wrapper.cc \
                $(SRCDIR)/modules/audio_processing/utility/delay_estimator.cc

# 根据目标架构选择源文件
ifeq ($(TARGET_ARCH),arm64)
    AECM_SOURCES += $(SRCDIR)/modules/audio_processing/aecm/aecm_core_neon.cc
else ifeq ($(TARGET_ARCH),arm)
    AECM_SOURCES += $(SRCDIR)/modules/audio_processing/aecm/aecm_core_neon.cc
else ifeq ($(TARGET_ARCH),mips)
    AECM_SOURCES += $(SRCDIR)/modules/audio_processing/aecm/aecm_core_mips.cc
else
    AECM_SOURCES += $(SRCDIR)/modules/audio_processing/aecm/aecm_core_c.cc
endif

# 通用音频处理源文件
COMMON_AUDIO_SOURCES = $(SRCDIR)/common_audio/ring_buffer.c \
                        $(SRCDIR)/common_audio/signal_processing/real_fft.c \
                        $(SRCDIR)/common_audio/signal_processing/signal_processing_library.cc \
                        $(SRCDIR)/common_audio/signal_processing/auto_correlation.c \
                        $(SRCDIR)/common_audio/signal_processing/auto_corr_to_refl_coef.c \
                        $(SRCDIR)/common_audio/signal_processing/complex_bit_reverse.c \
                        $(SRCDIR)/common_audio/signal_processing/complex_fft.c \
                        $(SRCDIR)/common_audio/signal_processing/copy_set_operations.c \
                        $(SRCDIR)/common_audio/signal_processing/cross_correlation.c \
                        $(SRCDIR)/common_audio/signal_processing/division_operations.c \
                        $(SRCDIR)/common_audio/signal_processing/dot_product_with_scale.cc \
                        $(SRCDIR)/common_audio/signal_processing/downsample_fast.c \
                        $(SRCDIR)/common_audio/signal_processing/energy.c \
                        $(SRCDIR)/common_audio/signal_processing/filter_ar.c \
                        $(SRCDIR)/common_audio/signal_processing/filter_ar_fast_q12.c \
                        $(SRCDIR)/common_audio/signal_processing/filter_ma_fast_q12.c \
                        $(SRCDIR)/common_audio/signal_processing/get_hanning_window.c \
                        $(SRCDIR)/common_audio/signal_processing/get_scaling_square.c \
                        $(SRCDIR)/common_audio/signal_processing/levinson_durbin.c \
                        $(SRCDIR)/common_audio/signal_processing/lpc_to_refl_coef.c \
                        $(SRCDIR)/common_audio/signal_processing/min_max_operations.c \
                        $(SRCDIR)/common_audio/signal_processing/randomization_functions.c \
                        $(SRCDIR)/common_audio/signal_processing/refl_coef_to_lpc.c \
                        $(SRCDIR)/common_audio/signal_processing/resample.c \
                        $(SRCDIR)/common_audio/signal_processing/resample_48khz.c \
                        $(SRCDIR)/common_audio/signal_processing/resample_by_2.c \
                        $(SRCDIR)/common_audio/signal_processing/resample_by_2_internal.c \
                        $(SRCDIR)/common_audio/signal_processing/resample_fractional.c \
                        $(SRCDIR)/common_audio/signal_processing/spl_init.c \
                        $(SRCDIR)/common_audio/signal_processing/spl_inl.c \
                        $(SRCDIR)/common_audio/signal_processing/spl_sqrt.c \
                        $(SRCDIR)/common_audio/signal_processing/splitting_filter.c \
                        $(SRCDIR)/common_audio/signal_processing/sqrt_of_one_minus_x_squared.c \
                        $(SRCDIR)/common_audio/signal_processing/vector_operations.c \
                        $(SRCDIR)/common_audio/signal_processing/vector_scaling_operations.c

# 根据目标架构添加优化源文件
ifeq ($(TARGET_ARCH),arm64)
    COMMON_AUDIO_SOURCES += $(SRCDIR)/common_audio/signal_processing/min_max_operations_neon.c \
                            $(SRCDIR)/common_audio/signal_processing/cross_correlation_neon.c \
                            $(SRCDIR)/common_audio/signal_processing/downsample_fast_neon.c
else ifeq ($(TARGET_ARCH),arm)
    COMMON_AUDIO_SOURCES += $(SRCDIR)/common_audio/signal_processing/min_max_operations_neon.c \
                            $(SRCDIR)/common_audio/signal_processing/cross_correlation_neon.c \
                            $(SRCDIR)/common_audio/signal_processing/downsample_fast_neon.c
else ifeq ($(TARGET_ARCH),mips)
    COMMON_AUDIO_SOURCES += $(SRCDIR)/common_audio/signal_processing/min_max_operations_mips.c \
                            $(SRCDIR)/common_audio/signal_processing/cross_correlation_mips.c \
                            $(SRCDIR)/common_audio/signal_processing/downsample_fast_mips.c \
                            $(SRCDIR)/common_audio/signal_processing/resample_by_2_mips.c \
                            $(SRCDIR)/common_audio/signal_processing/complex_fft_mips.c \
                            $(SRCDIR)/common_audio/signal_processing/filter_ar_fast_q12_mips.c \
                            $(SRCDIR)/common_audio/signal_processing/vector_scaling_operations_mips.c \
                            $(SRCDIR)/common_audio/signal_processing/complex_bit_reverse_mips.c
endif

# RTC基础源文件
RTC_SOURCES = $(SRCDIR)/rtc_base/checks.cc

# 所有源文件
ALL_SOURCES = $(AECM_SOURCES) $(COMMON_AUDIO_SOURCES) $(RTC_SOURCES)

# 目标文件
OBJECTS = $(patsubst $(SRCDIR)/%.cc,$(OBJDIR)/%.o,$(filter %.cc,$(ALL_SOURCES)))
OBJECTS += $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(filter %.c,$(ALL_SOURCES)))

# 目标文件
TARGET_LIB = libaecm.a
TARGET_TEST = aecm_test

# 默认目标
all: $(TARGET_LIB) $(TARGET_TEST)

# 创建目标目录
$(OBJDIR):
	mkdir -p $(OBJDIR)/modules/audio_processing/aecm
	mkdir -p $(OBJDIR)/modules/audio_processing/utility
	mkdir -p $(OBJDIR)/common_audio/signal_processing
	mkdir -p $(OBJDIR)/rtc_base

# 编译C++源文件
$(OBJDIR)/%.o: $(SRCDIR)/%.cc | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 编译C源文件
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 创建静态库
$(TARGET_LIB): $(OBJECTS)
	@echo "OBJECTS = $(OBJECTS)"
	$(AR) rcs $@ $(OBJECTS)
	$(RANLIB) $@

# 清理
clean:
	rm -rf $(OBJDIR) $(TARGET_LIB) $(TARGET_TEST)

# 安装
install: $(TARGET_LIB)
	install -d $(DESTDIR)/usr/lib
	install -d $(DESTDIR)/usr/include/aecm
	install -m 644 $(TARGET_LIB) $(DESTDIR)/usr/lib/
	install -m 644 src/modules/audio_processing/aecm/*.h $(DESTDIR)/usr/include/aecm/
	install -m 644 src/modules/audio_processing/utility/*.h $(DESTDIR)/usr/include/aecm/utility/

# 显示帮助
help:
	@echo "Available targets:"
	@echo "  all       - Build library and test program"
	@echo "  $(TARGET_LIB) - Build static library only"
	@echo "  $(TARGET_TEST) - Build test program only"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install library and headers"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Target architecture: $(TARGET_ARCH)"
	@echo "Compiler: $(CXX)"
	@echo "C++ flags: $(CXXFLAGS)"

.PHONY: all clean install help
