/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidChoreographer.h"

#include "mozilla/jni/Utils.h"

namespace mozilla::widget {

StaticAutoPtr<AndroidChoreographerApi> AndroidChoreographerApi::sInstance;

/* static */ void AndroidChoreographerApi::Init() {
  sInstance = new AndroidChoreographerApi();
  if (!sInstance->Load()) {
    sInstance = nullptr;
  }
}

/* static */ AndroidChoreographerApi* AndroidChoreographerApi::Get() {
  return sInstance;
}

/* static */
void AndroidChoreographerApi::Shutdown() { sInstance = nullptr; }

AChoreographer* AndroidChoreographerApi::AChoreographer_getInstance() const {
  return mAChoreographer_getInstance();
}

void AndroidChoreographerApi::AChoreographer_postFrameCallback(
    AChoreographer* choreographer, AChoreographer_frameCallback callback,
    void* data) const {
  mAChoreographer_postFrameCallback(choreographer, callback, data);
}

void AndroidChoreographerApi::AChoreographer_postFrameCallbackDelayed(
    AChoreographer* choreographer, AChoreographer_frameCallback callback,
    void* data, long delayMillis) const {
  mAChoreographer_postFrameCallbackDelayed(choreographer, callback, data,
                                           delayMillis);
}

void AndroidChoreographerApi::AChoreographer_postFrameCallback64(
    AChoreographer* choreographer, AChoreographer_frameCallback64 callback,
    void* data) const {
  mAChoreographer_postFrameCallback64(choreographer, callback, data);
}

void AndroidChoreographerApi::AChoreographer_postFrameCallbackDelayed64(
    AChoreographer* choreographer, AChoreographer_frameCallback64 callback,
    void* data, uint32_t delayMillis) const {
  mAChoreographer_postFrameCallbackDelayed64(choreographer, callback, data,
                                             delayMillis);
}

void AndroidChoreographerApi::AChoreographer_postVsyncCallback(
    AChoreographer* choreographer, AChoreographer_vsyncCallback callback,
    void* data) const {
  mAChoreographer_postVsyncCallback(choreographer, callback, data);
}

void AndroidChoreographerApi::AChoreographer_registerRefreshRateCallback(
    AChoreographer* choreographer, AChoreographer_refreshRateCallback callback,
    void* data) const {
  mAChoreographer_registerRefreshRateCallback(choreographer, callback, data);
}

void AndroidChoreographerApi::AChoreographer_unregisterRefreshRateCallback(
    AChoreographer* choreographer, AChoreographer_refreshRateCallback callback,
    void* data) const {
  mAChoreographer_unregisterRefreshRateCallback(choreographer, callback, data);
}

int64_t
AndroidChoreographerApi::AChoreographerFrameCallbackData_getFrameTimeNanos(
    const AChoreographerFrameCallbackData* data) const {
  return mAChoreographerFrameCallbackData_getFrameTimeNanos(data);
}

size_t AndroidChoreographerApi::
    AChoreographerFrameCallbackData_getFrameTimelinesLength(
        const AChoreographerFrameCallbackData* data) const {
  return mAChoreographerFrameCallbackData_getFrameTimelinesLength(data);
}

size_t AndroidChoreographerApi::
    AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex(
        const AChoreographerFrameCallbackData* data) const {
  return mAChoreographerFrameCallbackData_getPreferredFrameTimelineIndex(data);
}

AVsyncId AndroidChoreographerApi::
    AChoreographerFrameCallbackData_getFrameTimelineVsyncId(
        const AChoreographerFrameCallbackData* data, size_t index) const {
  return mAChoreographerFrameCallbackData_getFrameTimelineVsyncId(data, index);
}

int64_t AndroidChoreographerApi::
    AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos(
        const AChoreographerFrameCallbackData* data, size_t index) const {
  return mAChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos(
      data, index);
}

int64_t AndroidChoreographerApi::
    AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos(
        const AChoreographerFrameCallbackData* data, size_t index) const {
  return mAChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos(data, index);
}

#define LOAD_FN(handle, fun)                   \
  m##fun = (_##fun)dlsym(handle, #fun);        \
  if (!m##fun) {                               \
    gfxCriticalNote << "Failed to load " #fun; \
    return false;                              \
  }

bool AndroidChoreographerApi::Load() {
  printf_stderr("jamiedbg AndroidChoreographerApi::Load()\n");
  void* handle = dlopen("libandroid.so", RTLD_LAZY | RTLD_LOCAL);
  MOZ_ASSERT(handle);
  if (!handle) {
    gfxCriticalNote << "Failed to load libandroid.so";
    return false;
  }

  int apiLevel = jni::GetAPIVersion();

  if (apiLevel >= 24) {
    LOAD_FN(handle, AChoreographer_getInstance);
    LOAD_FN(handle, AChoreographer_postFrameCallback);
    LOAD_FN(handle, AChoreographer_postFrameCallbackDelayed);
  }
  if (apiLevel >= 29) {
    LOAD_FN(handle, AChoreographer_postFrameCallback64);
    LOAD_FN(handle, AChoreographer_postFrameCallbackDelayed64);
  }
  if (apiLevel >= 33) {
    LOAD_FN(handle, AChoreographer_postVsyncCallback);
  }
  if (apiLevel >= 30) {
    LOAD_FN(handle, AChoreographer_registerRefreshRateCallback);
    LOAD_FN(handle, AChoreographer_unregisterRefreshRateCallback);
  }
  if (apiLevel >= 33) {
    LOAD_FN(handle, AChoreographerFrameCallbackData_getFrameTimeNanos);
    LOAD_FN(handle, AChoreographerFrameCallbackData_getFrameTimelinesLength);
    LOAD_FN(handle,
            AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex);
    LOAD_FN(handle, AChoreographerFrameCallbackData_getFrameTimelineVsyncId);
    LOAD_FN(
        handle,
        AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos);
    LOAD_FN(handle,
            AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos);
  }

  return true;
}

#undef LOAD_FN

}  // namespace mozilla::widget
