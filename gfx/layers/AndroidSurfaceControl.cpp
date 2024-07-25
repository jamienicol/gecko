/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidSurfaceControl.h"

#include "mozilla/gfx/Logging.h"
#include "mozilla/jni/Utils.h"
#include <dlfcn.h>

namespace mozilla::layers {

StaticAutoPtr<AndroidSurfaceControlApi> AndroidSurfaceControlApi::sInstance;

/* static */
void AndroidSurfaceControlApi::Init() {
  sInstance = new AndroidSurfaceControlApi();
  if (!sInstance->Load()) {
    sInstance = nullptr;
  }
}

/* static */ const AndroidSurfaceControlApi* AndroidSurfaceControlApi::Get() {
  return sInstance;
}

#define LOAD_FN(handle, fun)                   \
  m##fun = (_##fun)dlsym(handle, #fun);        \
  if (!m##fun) {                               \
    gfxCriticalNote << "Failed to load " #fun; \
    return false;                              \
  }

bool AndroidSurfaceControlApi::Load() {
  void* const handle = dlopen("libandroid.so", RTLD_LAZY | RTLD_LOCAL);
  MOZ_ASSERT(handle);
  if (!handle) {
    gfxCriticalNote << "Failed to load libandroid.so";
    return false;
  }

  const int sdkLevel = jni::GetAPIVersion();
  if (sdkLevel >= 29) {
    LOAD_FN(handle, ASurfaceControl_createFromWindow);
    LOAD_FN(handle, ASurfaceControl_create);
    LOAD_FN(handle, ASurfaceControl_release);
    LOAD_FN(handle, ASurfaceTransaction_create);
    LOAD_FN(handle, ASurfaceTransaction_delete);
    LOAD_FN(handle, ASurfaceTransaction_apply);
    LOAD_FN(handle, ASurfaceTransactionStats_getLatchTime);
    LOAD_FN(handle, ASurfaceTransactionStats_getPresentFenceFd);
    LOAD_FN(handle, ASurfaceTransactionStats_getASurfaceControls);
    LOAD_FN(handle, ASurfaceTransactionStats_releaseASurfaceControls);
    LOAD_FN(handle, ASurfaceTransactionStats_getAcquireTime);
    LOAD_FN(handle, ASurfaceTransactionStats_getPreviousReleaseFenceFd);
    LOAD_FN(handle, ASurfaceTransaction_setOnComplete);
    LOAD_FN(handle, ASurfaceTransaction_reparent);
    LOAD_FN(handle, ASurfaceTransaction_setVisibility);
    LOAD_FN(handle, ASurfaceTransaction_setZOrder);
    LOAD_FN(handle, ASurfaceTransaction_setBuffer);
    LOAD_FN(handle, ASurfaceTransaction_setColor);
    LOAD_FN(handle, ASurfaceTransaction_setGeometry);
    LOAD_FN(handle, ASurfaceTransaction_setBufferTransparency);
    LOAD_FN(handle, ASurfaceTransaction_setDamageRegion);
    LOAD_FN(handle, ASurfaceTransaction_setDesiredPresentTime);
    LOAD_FN(handle, ASurfaceTransaction_setBufferAlpha);
    LOAD_FN(handle, ASurfaceTransaction_setBufferDataSpace);
    LOAD_FN(handle, ASurfaceTransaction_setHdrMetadata_smpte2086);
    LOAD_FN(handle, ASurfaceTransaction_setHdrMetadata_cta861_3);
  }
  if (sdkLevel >= 30) {
    LOAD_FN(handle, ASurfaceTransaction_setFrameRate);
  }
  if (sdkLevel >= 31) {
    LOAD_FN(handle, ASurfaceControl_acquire);
    LOAD_FN(handle, ASurfaceTransaction_setOnCommit);
    LOAD_FN(handle, ASurfaceTransaction_setCrop);
    LOAD_FN(handle, ASurfaceTransaction_setPosition);
    LOAD_FN(handle, ASurfaceTransaction_setBufferTransform);
    LOAD_FN(handle, ASurfaceTransaction_setScale);
    LOAD_FN(handle, ASurfaceTransaction_setFrameRateWithChangeStrategy);
    LOAD_FN(handle, ASurfaceTransaction_setEnableBackPressure);
  }
  if (sdkLevel >= 31) {
    LOAD_FN(handle, ASurfaceTransaction_setFrameTimeline);
  }

  return true;
}

#undef LOAD_FN

ASurfaceControl* AndroidSurfaceControlApi::ASurfaceControl_createFromWindow(
    ANativeWindow* parent, const char* debug_name) const {
  return mASurfaceControl_createFromWindow(parent, debug_name);
}

ASurfaceControl* AndroidSurfaceControlApi::ASurfaceControl_create(
    ASurfaceControl* parent, const char* debug_name) const {
  return mASurfaceControl_create(parent, debug_name);
}

void AndroidSurfaceControlApi::ASurfaceControl_acquire(
    ASurfaceControl* surface_control) const {
  mASurfaceControl_acquire(surface_control);
}

void AndroidSurfaceControlApi::ASurfaceControl_release(
    ASurfaceControl* surface_control) const {
  mASurfaceControl_release(surface_control);
}

ASurfaceTransaction* AndroidSurfaceControlApi::ASurfaceTransaction_create()
    const {
  return mASurfaceTransaction_create();
}

void AndroidSurfaceControlApi::ASurfaceTransaction_delete(
    ASurfaceTransaction* transaction) const {
  mASurfaceTransaction_delete(transaction);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_apply(
    ASurfaceTransaction* transaction) const {
  mASurfaceTransaction_apply(transaction);
}

int64_t AndroidSurfaceControlApi::ASurfaceTransactionStats_getLatchTime(
    ASurfaceTransactionStats* surface_transaction_stats) const {
  return mASurfaceTransactionStats_getLatchTime(surface_transaction_stats);
}

int AndroidSurfaceControlApi::ASurfaceTransactionStats_getPresentFenceFd(
    ASurfaceTransactionStats* surface_transaction_stats) const {
  return mASurfaceTransactionStats_getPresentFenceFd(surface_transaction_stats);
}

void AndroidSurfaceControlApi::ASurfaceTransactionStats_getASurfaceControls(
    ASurfaceTransactionStats* surface_transaction_stats,
    ASurfaceControl*** outASurfaceControls,
    size_t* outASurfaceControlsSize) const {
  mASurfaceTransactionStats_getASurfaceControls(
      surface_transaction_stats, outASurfaceControls, outASurfaceControlsSize);
}

void AndroidSurfaceControlApi::ASurfaceTransactionStats_releaseASurfaceControls(
    ASurfaceControl** surface_controls) const {
  mASurfaceTransactionStats_releaseASurfaceControls(surface_controls);
}

int64_t AndroidSurfaceControlApi::ASurfaceTransactionStats_getAcquireTime(
    ASurfaceTransactionStats* surface_transaction_stats,
    ASurfaceControl* surface_control) {
  return mASurfaceTransactionStats_getAcquireTime(surface_transaction_stats,
                                                  surface_control);
}

int AndroidSurfaceControlApi::
    ASurfaceTransactionStats_getPreviousReleaseFenceFd(
        ASurfaceTransactionStats* surface_transaction_stats,
        ASurfaceControl* surface_control) const {
  return mASurfaceTransactionStats_getPreviousReleaseFenceFd(
      surface_transaction_stats, surface_control);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setOnComplete(
    ASurfaceTransaction* transaction, void* context,
    ASurfaceTransaction_OnComplete func) const {
  mASurfaceTransaction_setOnComplete(transaction, context, func);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setOnCommit(
    ASurfaceTransaction* transaction, void* context,
    ASurfaceTransaction_OnCommit func) const {
  mASurfaceTransaction_setOnCommit(transaction, context, func);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_reparent(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    ASurfaceControl* new_parent) const {
  mASurfaceTransaction_reparent(transaction, surface_control, new_parent);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setVisibility(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    int8_t visibility) const {
  mASurfaceTransaction_setVisibility(transaction, surface_control, visibility);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setZOrder(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    int32_t z_order) const {
  mASurfaceTransaction_setZOrder(transaction, surface_control, z_order);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setBuffer(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    AHardwareBuffer* buffer, int acquire_fence_fd) const {
  mASurfaceTransaction_setBuffer(transaction, surface_control, buffer,
                                 acquire_fence_fd);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setColor(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control, float r,
    float g, float b, float alpha, ADataSpace dataspace) const {
  mASurfaceTransaction_setColor(transaction, surface_control, r, g, b, alpha,
                                dataspace);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setGeometry(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    const ARect& source, const ARect& destination, int32_t transform) const {
  mASurfaceTransaction_setGeometry(transaction, surface_control, source,
                                   destination, transform);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setCrop(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    const ARect& crop) const {
  mASurfaceTransaction_setCrop(transaction, surface_control, crop);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setPosition(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    int32_t x, int32_t y) const {
  mASurfaceTransaction_setPosition(transaction, surface_control, x, y);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setBufferTransform(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    int32_t transform) const {
  mASurfaceTransaction_setBufferTransform(transaction, surface_control,
                                          transform);
}
void AndroidSurfaceControlApi::ASurfaceTransaction_setScale(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    float xScale, float yScale) const {
  mASurfaceTransaction_setScale(transaction, surface_control, xScale, yScale);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setBufferTransparency(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    int8_t transparency) const {
  mASurfaceTransaction_setBufferTransparency(transaction, surface_control,
                                             transparency);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setDamageRegion(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    const ARect rects[], uint32_t count) const {
  mASurfaceTransaction_setDamageRegion(transaction, surface_control, rects,
                                       count);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setDesiredPresentTime(
    ASurfaceTransaction* transaction, int64_t desiredPresentTime) const {
  mASurfaceTransaction_setDesiredPresentTime(transaction, desiredPresentTime);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setBufferAlpha(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    float alpha) const {
  mASurfaceTransaction_setBufferAlpha(transaction, surface_control, alpha);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setBufferDataSpace(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    ADataSpace data_space) const {
  mASurfaceTransaction_setBufferDataSpace(transaction, surface_control,
                                          data_space);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setHdrMetadata_smpte2086(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    struct AHdrMetadata_smpte2086* metadata) const {
  mASurfaceTransaction_setHdrMetadata_smpte2086(transaction, surface_control,
                                                metadata);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setHdrMetadata_cta861_3(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    struct AHdrMetadata_cta861_3* metadata) const {
  mASurfaceTransaction_setHdrMetadata_cta861_3(transaction, surface_control,
                                               metadata);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setFrameRate(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    float frameRate, int8_t compatibility) const {
  mASurfaceTransaction_setFrameRate(transaction, surface_control, frameRate,
                                    compatibility);
}

void AndroidSurfaceControlApi::
    ASurfaceTransaction_setFrameRateWithChangeStrategy(
        ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
        float frameRate, int8_t compatibility,
        int8_t changeFrameRateStrategy) const {
  mASurfaceTransaction_setFrameRateWithChangeStrategy(
      transaction, surface_control, frameRate, compatibility,
      changeFrameRateStrategy);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setEnableBackPressure(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    bool enableBackPressure) const {
  mASurfaceTransaction_setEnableBackPressure(transaction, surface_control,
                                             enableBackPressure);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setFrameTimeline(
    ASurfaceTransaction* transaction, AVsyncId vsyncId) const {
  mASurfaceTransaction_setFrameTimeline(transaction, vsyncId);
}

}  // namespace mozilla::layers
