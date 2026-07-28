# AEC4

AEC4 是 EdgeRTC 的可选 AECM (Acoustic Echo Control for Mobile) 模块，用于
资源受限设备的实时回声消除。

## 项目结构

```
aec4/
├── src/
│   ├── modules/audio_processing/
│   │   ├── aecm/                    # AECM核心模块
│   │   │   ├── aecm_core.cc        # AECM核心实现
│   │   │   ├── aecm_core.h         # AECM核心头文件
│   │   │   ├── aecm_core_c.cc      # C语言实现
│   │   │   ├── aecm_core_mips.cc   # MIPS优化实现
│   │   │   ├── aecm_core_neon.cc   # ARM NEON优化实现
│   │   │   ├── aecm_defines.h      # 常量定义
│   │   │   ├── echo_control_mobile.cc  # 回声控制接口实现
│   │   │   └── echo_control_mobile.h   # 回声控制接口头文件
│   │   └── utility/
│   │       ├── delay_estimator_wrapper.cc  # 延迟估计器包装
│   │       └── delay_estimator_wrapper.h   # 延迟估计器包装头文件
│   ├── common_audio/
│   │   ├── ring_buffer.c           # 环形缓冲区实现
│   │   ├── ring_buffer.h           # 环形缓冲区头文件
│   │   └── signal_processing/      # 信号处理库
│   │       ├── include/            # 信号处理头文件
│   │       ├── real_fft.c          # 实数FFT实现
│   │       ├── signal_processing_library.cc  # 信号处理库
│   │       ├── complex_fft.c       # 复数FFT实现
│   │       ├── min_max_operations.c # 最小最大操作
│   │       ├── cross_correlation.c # 互相关计算
│   │       ├── resample.c          # 重采样实现
│   │       └── ...                 # 其他信号处理函数
│   └── rtc_base/
│       ├── checks.cc               # 检查函数实现
│       ├── checks.h                # 检查函数头文件
│       ├── sanitizer.h             # 内存清理头文件
│       └── numerics/               # 数值计算
│           ├── safe_conversions.h  # 安全类型转换
│           └── safe_conversions_impl.h  # 安全类型转换实现
└── README.md                      # 本文件
```

## 功能特性

- **回声消除**: 基于频域的自适应回声消除算法
- **多平台支持**: 支持ARM NEON、MIPS和通用C实现
- **低延迟**: 专为移动设备优化的低延迟处理
- **内存高效**: 优化的内存使用和缓存友好的数据结构

## 构建要求

- C++编译器 (支持C++11或更高版本)
- 支持的目标平台: x86/x64, ARM/ARM64, MIPS


## 使用方法

### C++接口

```cpp
#include "modules/audio_processing/aecm/echo_control_mobile.h"

// 创建AECM实例
void* aecmInst = WebRtcAecm_Create();

// 初始化 (采样率: 8000Hz 或 16000Hz)
int32_t result = WebRtcAecm_Init(aecmInst, 16000);

// 处理远端信号 (扬声器输出)
result = WebRtcAecm_BufferFarend(aecmInst, farEndData, frameSize);

// 处理近端信号 (麦克风输入) 并输出回声消除后的信号
result = WebRtcAecm_Process(aecmInst, nearEndData, nullptr, outputData, frameSize, 0);

// 清理
WebRtcAecm_Free(aecmInst);
```

### 参数说明

- **采样率**: 支持8000Hz和16000Hz
- **帧大小**: 8000Hz时为80样本(10ms)，16000Hz时为80样本(5ms)
- **数据类型**: 16位有符号整数 (int16_t)


## 性能特性

- **延迟**: 通常小于10ms
- **CPU使用**: 在ARM Cortex-A系列处理器上约2-5% CPU使用率
- **内存使用**: 约50-100KB RAM

## 注意事项

1. 确保远端和近端信号的采样率一致
2. 建议在音频处理线程中调用，避免阻塞主线程
3. 对于实时应用，建议使用固定的帧大小
4. 在初始化后，需要先处理几帧远端信号才能开始回声消除

