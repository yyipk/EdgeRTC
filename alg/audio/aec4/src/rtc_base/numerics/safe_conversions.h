/*
 *  Copyright 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may be
 *  found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_NUMERICS_SAFE_CONVERSIONS_H_
#define RTC_BASE_NUMERICS_SAFE_CONVERSIONS_H_

#include <limits>
#include <type_traits>

#include "rtc_base/checks.h"

namespace rtc {

// Simplified safe conversions for standalone AECM project

template <typename Dst, typename Src>
inline Dst checked_cast(Src value) {
  // Simple implementation - just cast
  return static_cast<Dst>(value);
}

template <typename Dst, typename Src>
inline Dst dchecked_cast(Src value) {
  // Simple implementation - just cast
  return static_cast<Dst>(value);
}

template <typename Dst, typename Src>
inline Dst saturated_cast(Src value) {
  // Simple implementation - just cast
  return static_cast<Dst>(value);
}

}  // namespace rtc

#endif  // RTC_BASE_NUMERICS_SAFE_CONVERSIONS_H_
