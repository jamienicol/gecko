/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_AndroidChoreographer_h
#define mozilla_widget_AndroidChoreographer_h

#include <android/choreographer.h>

#include "mozilla/StaticPtr.h"

namespace mozilla::widget {

class AndroidChoreographerApi {
 public:
  static void Init();
  static void Shutdown();

  static AndroidChoreographerApi* Get();

  AChoreographer* AChoreographer_getInstance() const;
  void AChoreographer_postFrameCallback(AChoreographer* choreographer,
                                        AChoreographer_frameCallback callback,
                                        void* data) const;
  void AChoreographer_postFrameCallbackDelayed(
      AChoreographer* choreographer, AChoreographer_frameCallback callback,
      void* data, long delayMillis) const;
  void AChoreographer_postFrameCallback64(
      AChoreographer* choreographer, AChoreographer_frameCallback64 callback,
      void* data) const;
  void AChoreographer_postFrameCallbackDelayed64(
      AChoreographer* choreographer, AChoreographer_frameCallback64 callback,
      void* data, uint32_t delayMillis) const;
  void AChoreographer_postVsyncCallback(AChoreographer* choreographer,
                                        AChoreographer_vsyncCallback callback,
                                        void* data) const;
  void AChoreographer_registerRefreshRateCallback(
      AChoreographer* choreographer, AChoreographer_refreshRateCallback callback,
      void* data) const;
  void AChoreographer_unregisterRefreshRateCallback(
      AChoreographer* choreographer, AChoreographer_refreshRateCallback callback,
      void* data) const;
  int64_t AChoreographerFrameCallbackData_getFrameTimeNanos(
      const AChoreographerFrameCallbackData* data) const;
  size_t AChoreographerFrameCallbackData_getFrameTimelinesLength(
      const AChoreographerFrameCallbackData* data) const;
  size_t AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex(
      const AChoreographerFrameCallbackData* data) const;
  AVsyncId AChoreographerFrameCallbackData_getFrameTimelineVsyncId(
      const AChoreographerFrameCallbackData* data, size_t index) const;
  int64_t
  AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos(
      const AChoreographerFrameCallbackData* data, size_t index) const;
  int64_t AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos(
      const AChoreographerFrameCallbackData* data, size_t index) const;

 private:
  AndroidChoreographerApi() = default;
  bool Load();

  using _AChoreographer_getInstance = AChoreographer* (*)();
  using _AChoreographer_postFrameCallback =
      void (*)(AChoreographer* choreographer,
               AChoreographer_frameCallback callback, void* data);
  using _AChoreographer_postFrameCallbackDelayed = void (*)(
      AChoreographer* choreographer, AChoreographer_frameCallback callback,
      void* data, long delayMillis);
  using _AChoreographer_postFrameCallback64 =
      void (*)(AChoreographer* choreographer,
               AChoreographer_frameCallback64 callback, void* data);
  using _AChoreographer_postFrameCallbackDelayed64 = void (*)(
      AChoreographer* choreographer, AChoreographer_frameCallback64 callback,
      void* data, uint32_t delayMillis);
  using _AChoreographer_postVsyncCallback =
      void (*)(AChoreographer* choreographer,
               AChoreographer_vsyncCallback callback, void* data);
  using _AChoreographer_registerRefreshRateCallback =
      void (*)(AChoreographer* choreographer,
               AChoreographer_refreshRateCallback, void* data);
  using _AChoreographer_unregisterRefreshRateCallback =
      void (*)(AChoreographer* choreographer,
               AChoreographer_refreshRateCallback, void* data);
  using _AChoreographerFrameCallbackData_getFrameTimeNanos =
      int64_t (*)(const AChoreographerFrameCallbackData* data);
  using _AChoreographerFrameCallbackData_getFrameTimelinesLength =
      size_t (*)(const AChoreographerFrameCallbackData* data);
  using _AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex =
      size_t (*)(const AChoreographerFrameCallbackData* data);
  using _AChoreographerFrameCallbackData_getFrameTimelineVsyncId =
      AVsyncId (*)(const AChoreographerFrameCallbackData* data, size_t index);
  using _AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos =
      int64_t (*)(const AChoreographerFrameCallbackData* data, size_t index);
  using _AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos =
      int64_t (*)(const AChoreographerFrameCallbackData* data, size_t index);

  _AChoreographer_getInstance mAChoreographer_getInstance = nullptr;
  _AChoreographer_postFrameCallback mAChoreographer_postFrameCallback = nullptr;
  _AChoreographer_postFrameCallbackDelayed
      mAChoreographer_postFrameCallbackDelayed = nullptr;
  _AChoreographer_postFrameCallback64 mAChoreographer_postFrameCallback64 =
      nullptr;
  _AChoreographer_postFrameCallbackDelayed64
      mAChoreographer_postFrameCallbackDelayed64 = nullptr;
  _AChoreographer_postVsyncCallback mAChoreographer_postVsyncCallback = nullptr;
  _AChoreographer_registerRefreshRateCallback
      mAChoreographer_registerRefreshRateCallback = nullptr;
  _AChoreographer_unregisterRefreshRateCallback
      mAChoreographer_unregisterRefreshRateCallback = nullptr;
  _AChoreographerFrameCallbackData_getFrameTimeNanos
      mAChoreographerFrameCallbackData_getFrameTimeNanos = nullptr;
  _AChoreographerFrameCallbackData_getFrameTimelinesLength
      mAChoreographerFrameCallbackData_getFrameTimelinesLength = nullptr;
  _AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex
      mAChoreographerFrameCallbackData_getPreferredFrameTimelineIndex = nullptr;
  _AChoreographerFrameCallbackData_getFrameTimelineVsyncId
      mAChoreographerFrameCallbackData_getFrameTimelineVsyncId = nullptr;
  _AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos
      mAChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos =
          nullptr;
  _AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos
      mAChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos = nullptr;

  static StaticAutoPtr<AndroidChoreographerApi> sInstance;
};

}  // namespace mozilla::widget

#endif  // mozilla_widget_AndroidChoreographer_h
