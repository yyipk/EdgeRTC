/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may be
 *  found in the AUTHORS file in the root of the source tree.
 */

#include "common_audio/signal_processing/include/signal_processing_library.h"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {

// QMF (Quadrature Mirror Filter) synthesis
void WebRtcSpl_QMF_Synthesis(const int16_t* low_band,
                             const int16_t* high_band,
                             int16_t* out_data,
                             size_t data_length,
                             int32_t* filter_state1,
                             int32_t* filter_state2) {
  // 简化实现，不做实际 QMF 合成
  for (size_t i = 0; i < data_length; ++i) {
    out_data[i] = low_band[i] + high_band[i];
  }
}

// QMF (Quadrature Mirror Filter) analysis
void WebRtcSpl_QMF_Analysis(const int16_t* in_data,
                             int16_t* low_band,
                             int16_t* high_band,
                             int32_t* filter_state1,
                             int32_t* filter_state2) {
  // 简化实现，不做实际 QMF 分析
  for (size_t i = 0; i < 160; ++i) {  // 假设160样本
    low_band[i] = in_data[i];
    high_band[i] = in_data[i];
  }
}

// 音频格式转换函数
void WebRtcSpl_FloatS16ToS16(const float* in_vector,
                              size_t vector_length,
                              int16_t* out_vector) {
  // 简化实现，直接转换
  for (size_t i = 0; i < vector_length; ++i) {
    out_vector[i] = static_cast<int16_t>(in_vector[i]);
  }
}

void WebRtcSpl_S16ToFloatS16(const int16_t* in_vector,
                              size_t vector_length,
                              float* out_vector) {
  // 简化实现，直接转换
  for (size_t i = 0; i < vector_length; ++i) {
    out_vector[i] = static_cast<float>(in_vector[i]);
  }
}

void WebRtcSpl_FloatS16ToFloat(const float* in_vector,
                                size_t vector_length,
                                float* out_vector) {
  // 简化实现，直接复制
  std::copy(in_vector, in_vector + vector_length, out_vector);
}

void WebRtcSpl_FloatToFloatS16(const float* in_vector,
                                size_t vector_length,
                                float* out_vector) {
  // 简化实现，直接复制
  std::copy(in_vector, in_vector + vector_length, out_vector);
}

// 添加 FloatS16ToS16 的别名函数
void FloatS16ToS16(const float* in_vector,
                    size_t vector_length,
                    int16_t* out_vector) {
  WebRtcSpl_FloatS16ToS16(in_vector, vector_length, out_vector);
}

void S16ToFloatS16(const int16_t* in_vector,
                    size_t vector_length,
                    float* out_vector) {
  WebRtcSpl_S16ToFloatS16(in_vector, vector_length, out_vector);
}

void FloatS16ToFloat(const float* in_vector,
                      size_t vector_length,
                      float* out_vector) {
  WebRtcSpl_FloatS16ToFloat(in_vector, vector_length, out_vector);
}

void FloatToFloatS16(const float* in_vector,
                      size_t vector_length,
                      float* out_vector) {
  WebRtcSpl_FloatToFloatS16(in_vector, vector_length, out_vector);
}

}  // extern "C"
