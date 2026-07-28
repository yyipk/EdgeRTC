/*
 *  Copyright 2006 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may be
 *  found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_CHECKS_H_
#define RTC_BASE_CHECKS_H_

#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
#include <cstdint>
#include <cstdlib>
//#include <iostream>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Fatal error function
void rtc_FatalMessage(const char* file, int line, const char* msg);

#ifdef __cplusplus
}  // extern "C"

// Simplified checks for standalone AECM project
#define RTC_CHECK(x) \
  do { \
    if (!(x)) { \
      printf("RTC_CHECK failed: %s at %s:%d\n", #x, __FILE__, __LINE__); \
      abort(); \
    } \
  } while (0)

#define RTC_CHECK_EQ(x, y) \
  do { \
    if ((x) != (y)) { \
      printf("RTC_CHECK_EQ failed: %s (%d) != %s (%d) at %s:%d\n", \
             #x, (int)(x), #y, (int)(y), __FILE__, __LINE__); \
      abort(); \
    } \
  } while (0)

#define RTC_CHECK_NE(x, y) \
  do { \
    if ((x) == (y)) { \
      printf("RTC_CHECK_NE failed: %s (%d) == %s (%d) at %s:%d\n", \
             #x, (int)(x), #y, (int)(y), __FILE__, __LINE__); \
      abort(); \
    } \
  } while (0)

#define RTC_CHECK_GT(x, y) \
  do { \
    if (!((x) > (y))) { \
      printf("RTC_CHECK_GT failed: %s (%d) <= %s (%d) at %s:%d\n", \
             #x, (int)(x), #y, (int)(y), __FILE__, __LINE__); \
      abort(); \
    } \
  } while (0)

#define RTC_CHECK_GE(x, y) \
  do { \
    if (!((x) >= (y))) { \
      printf("RTC_CHECK_GE failed: %s (%d) < %s (%d) at %s:%d\n", \
             #x, (int)(x), #y, (int)(y), __FILE__, __LINE__); \
      abort(); \
    } \
  } while (0)

#define RTC_CHECK_LT(x, y) \
  do { \
    if (!((x) < (y))) { \
      printf("RTC_CHECK_LT failed: %s (%d) >= %s (%d) at %s:%d\n", \
             #x, (int)(x), #y, (int)(y), __FILE__, __LINE__); \
      abort(); \
    } \
  } while (0)

#define RTC_CHECK_LE(x, y) \
  do { \
    if (!((x) <= (y))) { \
      printf("RTC_CHECK_LE failed: %s (%d) > %s (%d) at %s:%d\n", \
             #x, (int)(x), #y, (int)(y), __FILE__, __LINE__); \
      abort(); \
    } \
  } while (0)

#define RTC_CHECK_NOTNULL(x) \
  do { \
    if ((x) == NULL) { \
      printf("RTC_CHECK_NOTNULL failed: %s is NULL at %s:%d\n", \
             #x, __FILE__, __LINE__); \
      abort(); \
    } \
  } while (0)

#define RTC_CHECK_NOTREACHED() \
  do { \
    printf("RTC_CHECK_NOTREACHED reached at %s:%d\n", __FILE__, __LINE__); \
    abort(); \
  } while (0)

// Debug versions (only active in debug builds)
#ifdef NDEBUG
#define RTC_DCHECK(x) ((void)0)
#define RTC_DCHECK_EQ(x, y) ((void)0)
#define RTC_DCHECK_NE(x, y) ((void)0)
#define RTC_DCHECK_GT(x, y) ((void)0)
#define RTC_DCHECK_GE(x, y) ((void)0)
#define RTC_DCHECK_LT(x, y) ((void)0)
#define RTC_DCHECK_LE(x, y) ((void)0)
#define RTC_DCHECK_NOTNULL(x) ((void)0)
#else
#define RTC_DCHECK(x) RTC_CHECK(x)
#define RTC_DCHECK_EQ(x, y) RTC_CHECK_EQ(x, y)
#define RTC_DCHECK_NE(x, y) RTC_CHECK_NE(x, y)
#define RTC_DCHECK_GT(x, y) RTC_CHECK_GT(x, y)
#define RTC_DCHECK_GE(x, y) RTC_CHECK_GE(x, y)
#define RTC_DCHECK_LT(x, y) RTC_CHECK_LT(x, y)
#define RTC_DCHECK_LE(x, y) RTC_CHECK_LE(x, y)
#define RTC_DCHECK_NOTNULL(x) RTC_CHECK_NOTNULL(x)
#endif

#endif  // __cplusplus

// C version
#ifndef __cplusplus
#define RTC_CHECK(condition) \
  do { \
    if (!(condition)) { \
      rtc_FatalMessage(__FILE__, __LINE__, "CHECK failed: " #condition); \
    } \
  } while (0)

#define RTC_CHECK_EQ(a, b) RTC_CHECK((a) == (b))
#define RTC_CHECK_NE(a, b) RTC_CHECK((a) != (b))
#define RTC_CHECK_LE(a, b) RTC_CHECK((a) <= (b))
#define RTC_CHECK_LT(a, b) RTC_CHECK((a) < (b))
#define RTC_CHECK_GE(a, b) RTC_CHECK((a) >= (b))
#define RTC_CHECK_GT(a, b) RTC_CHECK((a) > (b))

#define RTC_DCHECK(condition) ((void)0)
#define RTC_DCHECK_EQ(a, b) ((void)0)
#define RTC_DCHECK_NE(a, b) ((void)0)
#define RTC_DCHECK_LE(a, b) ((void)0)
#define RTC_DCHECK_LT(a, b) ((void)0)
#define RTC_DCHECK_GE(a, b) ((void)0)
#define RTC_DCHECK_GT(a, b) ((void)0)
#endif

#endif  // RTC_BASE_CHECKS_H_
