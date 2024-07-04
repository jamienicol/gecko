/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_ANDROID_SURFACE_CONTROL
#define MOZILLA_LAYERS_ANDROID_SURFACE_CONTROL

#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"

// Including and using types defined in android/surface_control.h causes
// compilation errors when the min SDK level is too low. So we instead directly
// include its dependencies and declare the required types ourselves.
#include <android/choreographer.h>
#include <android/native_window.h>
extern "C" {
typedef struct ASurfaceControl ASurfaceControl;
typedef struct ASurfaceTransaction ASurfaceTransaction;
typedef struct ASurfaceTransactionStats ASurfaceTransactionStats;
typedef void (*ASurfaceTransaction_OnComplete)(void* context,
                                               ASurfaceTransactionStats* stats);
typedef void (*ASurfaceTransaction_OnCommit)(void* context,
                                             ASurfaceTransactionStats* stats);
enum {
    ASURFACE_TRANSACTION_VISIBILITY_HIDE = 0,
    ASURFACE_TRANSACTION_VISIBILITY_SHOW = 1,
};
enum {
  ASURFACE_TRANSACTION_TRANSPARENCY_TRANSPARENT = 0,
  ASURFACE_TRANSACTION_TRANSPARENCY_TRANSLUCENT = 1,
  ASURFACE_TRANSACTION_TRANSPARENCY_OPAQUE = 2,
};
}

namespace mozilla {
namespace layers {

class AndroidSurfaceControlApi final {
 public:
  static void Init();

  static const AndroidSurfaceControlApi* Get();

  ASurfaceControl* ASurfaceControl_createFromWindow(
      ANativeWindow* parent, const char* debug_name) const;
  ASurfaceControl* ASurfaceControl_create(ASurfaceControl* parent,
                                          const char* debug_name) const;
  void ASurfaceControl_acquire(ASurfaceControl* surface_control) const;
  void ASurfaceControl_release(ASurfaceControl* surface_control) const;

  ASurfaceTransaction* ASurfaceTransaction_create() const;
  void ASurfaceTransaction_delete(ASurfaceTransaction* transaction) const;
  void ASurfaceTransaction_apply(ASurfaceTransaction* transaction) const;

  int64_t ASurfaceTransactionStats_getLatchTime(
      ASurfaceTransactionStats* surface_transaction_stats) const;
  int ASurfaceTransactionStats_getPresentFenceFd(
      ASurfaceTransactionStats* surface_transaction_stats) const;
  void ASurfaceTransactionStats_getASurfaceControls(
      ASurfaceTransactionStats* surface_transaction_stats,
      ASurfaceControl*** outASurfaceControls,
      size_t* outASurfaceControlsSize) const;
  void ASurfaceTransactionStats_releaseASurfaceControls(
      ASurfaceControl** surface_controls) const;
  int64_t ASurfaceTransactionStats_getAcquireTime(
      ASurfaceTransactionStats* surface_transaction_stats,
      ASurfaceControl* surface_control);
  int ASurfaceTransactionStats_getPreviousReleaseFenceFd(
      ASurfaceTransactionStats* surface_transaction_stats,
      ASurfaceControl* surface_control) const;

  void ASurfaceTransaction_setOnComplete(
      ASurfaceTransaction* transaction, void* context,
      ASurfaceTransaction_OnComplete func) const;
  void ASurfaceTransaction_setOnCommit(ASurfaceTransaction* transaction,
                                       void* context,
                                       ASurfaceTransaction_OnCommit func) const;
  void ASurfaceTransaction_reparent(ASurfaceTransaction* transaction,
                                    ASurfaceControl* surface_control,
                                    ASurfaceControl* new_parent) const;
  void ASurfaceTransaction_setVisibility(ASurfaceTransaction* transaction,
                                         ASurfaceControl* surface_control,
                                         int8_t visibility) const;
  void ASurfaceTransaction_setZOrder(ASurfaceTransaction* transaction,
                                     ASurfaceControl* surface_control,
                                     int32_t z_order) const;
  void ASurfaceTransaction_setBuffer(ASurfaceTransaction* transaction,
                                     ASurfaceControl* surface_control,
                                     AHardwareBuffer* buffer,
                                     int acquire_fence_fd = -1) const;
  void ASurfaceTransaction_setColor(ASurfaceTransaction* transaction,
                                    ASurfaceControl* surface_control, float r,
                                    float g, float b, float alpha,
                                    ADataSpace dataspace) const;
  void ASurfaceTransaction_setGeometry(ASurfaceTransaction* transaction,
                                       ASurfaceControl* surface_control,
                                       const ARect& source,
                                       const ARect& destination,
                                       int32_t transform) const;
  void ASurfaceTransaction_setCrop(ASurfaceTransaction* transaction,
                                   ASurfaceControl* surface_control,
                                   const ARect& crop) const;
  void ASurfaceTransaction_setPosition(ASurfaceTransaction* transaction,
                                       ASurfaceControl* surface_control,
                                       int32_t x, int32_t y) const;
  void ASurfaceTransaction_setBufferTransform(ASurfaceTransaction* transaction,
                                              ASurfaceControl* surface_control,
                                              int32_t transform) const;
  void ASurfaceTransaction_setScale(ASurfaceTransaction* transaction,
                                    ASurfaceControl* surface_control,
                                    float xScale, float yScale) const;
  void ASurfaceTransaction_setBufferTransparency(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      int8_t transparency) const;
  void ASurfaceTransaction_setDamageRegion(ASurfaceTransaction* transaction,
                                           ASurfaceControl* surface_control,
                                           const ARect rects[],
                                           uint32_t count) const;
  void ASurfaceTransaction_setDesiredPresentTime(
      ASurfaceTransaction* transaction, int64_t desiredPresentTime) const;
  void ASurfaceTransaction_setBufferAlpha(ASurfaceTransaction* transaction,
                                          ASurfaceControl* surface_control,
                                          float alpha) const;
  void ASurfaceTransaction_setBufferDataSpace(ASurfaceTransaction* transaction,
                                              ASurfaceControl* surface_control,
                                              ADataSpace data_space) const;
  void ASurfaceTransaction_setHdrMetadata_smpte2086(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      struct AHdrMetadata_smpte2086* metadata) const;
  void ASurfaceTransaction_setHdrMetadata_cta861_3(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      struct AHdrMetadata_cta861_3* metadata) const;
  void ASurfaceTransaction_setFrameRate(ASurfaceTransaction* transaction,
                                        ASurfaceControl* surface_control,
                                        float frameRate,
                                        int8_t compatibility) const;
  void ASurfaceTransaction_setFrameRateWithChangeStrategy(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      float frameRate, int8_t compatibility,
      int8_t changeFrameRateStrategy) const;
  void ASurfaceTransaction_setEnableBackPressure(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      bool enableBackPressure) const;
  void ASurfaceTransaction_setFrameTimeline(ASurfaceTransaction* transaction,
                                            AVsyncId vsyncId) const;

 private:
  AndroidSurfaceControlApi() = default;
  bool Load();

  using _ASurfaceControl_createFromWindow =
      ASurfaceControl* (*)(ANativeWindow* parent, const char* debug_name);
  using _ASurfaceControl_create = ASurfaceControl* (*)(ASurfaceControl* parent,
                                                       const char* debug_name);
  using _ASurfaceControl_acquire = void (*)(ASurfaceControl* surface_control);
  using _ASurfaceControl_release = void (*)(ASurfaceControl* surface_control);

  using _ASurfaceTransaction_create = ASurfaceTransaction* (*)();
  using _ASurfaceTransaction_delete =
      void (*)(ASurfaceTransaction* transaction);
  using _ASurfaceTransaction_apply = void (*)(ASurfaceTransaction* transaction);

  using _ASurfaceTransactionStats_getLatchTime =
      int64_t (*)(ASurfaceTransactionStats* surface_transaction_stats);
  using _ASurfaceTransactionStats_getPresentFenceFd =
      int (*)(ASurfaceTransactionStats* surface_transaction_stats);
  using _ASurfaceTransactionStats_getASurfaceControls = void (*)(
      ASurfaceTransactionStats* surface_transaction_stats,
      ASurfaceControl*** outASurfaceControls, size_t* outASurfaceControlsSize);
  using _ASurfaceTransactionStats_releaseASurfaceControls =
      void (*)(ASurfaceControl** surface_controls);
  using _ASurfaceTransactionStats_getAcquireTime =
      int64_t (*)(ASurfaceTransactionStats* surface_transaction_stats,
                  ASurfaceControl* surface_control);
  using _ASurfaceTransactionStats_getPreviousReleaseFenceFd =
      int (*)(ASurfaceTransactionStats* surface_transaction_stats,
              ASurfaceControl* surface_control);

  using _ASurfaceTransaction_setOnComplete =
      void (*)(ASurfaceTransaction* transaction, void* context,
               ASurfaceTransaction_OnComplete func);
  using _ASurfaceTransaction_setOnCommit =
      void (*)(ASurfaceTransaction* transaction, void* context,
               ASurfaceTransaction_OnCommit func);
  using _ASurfaceTransaction_reparent =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, ASurfaceControl* new_parent);
  using _ASurfaceTransaction_setVisibility =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, int8_t visibility);
  using _ASurfaceTransaction_setZOrder =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, int32_t z_order);
  using _ASurfaceTransaction_setBuffer = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      AHardwareBuffer* buffer, int acquire_fence_fd);
  using _ASurfaceTransaction_setColor = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      float r, float g, float b, float alpha, ADataSpace dataspace);
  using _ASurfaceTransaction_setGeometry = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      const ARect& source, const ARect& destination, int32_t transform);
  using _ASurfaceTransaction_setCrop =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, const ARect& crop);
  using _ASurfaceTransaction_setPosition =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, int32_t x, int32_t y);
  using _ASurfaceTransaction_setBufferTransform =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, int32_t transform);
  using _ASurfaceTransaction_setScale =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, float xScale, float yScale);
  using _ASurfaceTransaction_setBufferTransparency =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, int8_t transparency);
  using _ASurfaceTransaction_setDamageRegion = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      const ARect rects[], uint32_t count);
  using _ASurfaceTransaction_setDesiredPresentTime =
      void (*)(ASurfaceTransaction* transaction, int64_t desiredPresentTime);
  using _ASurfaceTransaction_setBufferAlpha =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, float alpha);
  using _ASurfaceTransaction_setBufferDataSpace =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, ADataSpace data_space);
  using _ASurfaceTransaction_setHdrMetadata_smpte2086 = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      struct AHdrMetadata_smpte2086* metadata);
  using _ASurfaceTransaction_setHdrMetadata_cta861_3 = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      struct AHdrMetadata_cta861_3* metadata);
  using _ASurfaceTransaction_setFrameRate = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      float frameRate, int8_t compatibility);
  using _ASurfaceTransaction_setFrameRateWithChangeStrategy = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      float frameRate, int8_t compatibility, int8_t changeFrameRateStrategy);
  using _ASurfaceTransaction_setEnableBackPressure =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, bool enableBackPressure);
  using _ASurfaceTransaction_setFrameTimeline =
      void (*)(ASurfaceTransaction* transaction, AVsyncId vsyncId);

  _ASurfaceControl_createFromWindow mASurfaceControl_createFromWindow = nullptr;
  _ASurfaceControl_create mASurfaceControl_create = nullptr;
  _ASurfaceControl_acquire mASurfaceControl_acquire = nullptr;
  _ASurfaceControl_release mASurfaceControl_release = nullptr;

  _ASurfaceTransaction_create mASurfaceTransaction_create = nullptr;
  _ASurfaceTransaction_delete mASurfaceTransaction_delete = nullptr;
  _ASurfaceTransaction_apply mASurfaceTransaction_apply = nullptr;

  _ASurfaceTransactionStats_getLatchTime
      mASurfaceTransactionStats_getLatchTime = nullptr;
  _ASurfaceTransactionStats_getPresentFenceFd
      mASurfaceTransactionStats_getPresentFenceFd = nullptr;
  _ASurfaceTransactionStats_getASurfaceControls
      mASurfaceTransactionStats_getASurfaceControls = nullptr;
  _ASurfaceTransactionStats_releaseASurfaceControls
      mASurfaceTransactionStats_releaseASurfaceControls = nullptr;
  _ASurfaceTransactionStats_getAcquireTime
      mASurfaceTransactionStats_getAcquireTime = nullptr;
  _ASurfaceTransactionStats_getPreviousReleaseFenceFd
      mASurfaceTransactionStats_getPreviousReleaseFenceFd = nullptr;

  _ASurfaceTransaction_setOnComplete mASurfaceTransaction_setOnComplete =
      nullptr;
  _ASurfaceTransaction_setOnCommit mASurfaceTransaction_setOnCommit = nullptr;
  _ASurfaceTransaction_reparent mASurfaceTransaction_reparent = nullptr;
  _ASurfaceTransaction_setVisibility mASurfaceTransaction_setVisibility =
      nullptr;
  _ASurfaceTransaction_setZOrder mASurfaceTransaction_setZOrder = nullptr;
  _ASurfaceTransaction_setBuffer mASurfaceTransaction_setBuffer = nullptr;
  _ASurfaceTransaction_setColor mASurfaceTransaction_setColor = nullptr;
  _ASurfaceTransaction_setGeometry mASurfaceTransaction_setGeometry = nullptr;
  _ASurfaceTransaction_setCrop mASurfaceTransaction_setCrop = nullptr;
  _ASurfaceTransaction_setPosition mASurfaceTransaction_setPosition = nullptr;
  _ASurfaceTransaction_setBufferTransform
      mASurfaceTransaction_setBufferTransform = nullptr;
  _ASurfaceTransaction_setScale mASurfaceTransaction_setScale = nullptr;
  _ASurfaceTransaction_setBufferTransparency
      mASurfaceTransaction_setBufferTransparency = nullptr;
  _ASurfaceTransaction_setDamageRegion mASurfaceTransaction_setDamageRegion =
      nullptr;
  _ASurfaceTransaction_setDesiredPresentTime
      mASurfaceTransaction_setDesiredPresentTime = nullptr;
  _ASurfaceTransaction_setBufferAlpha mASurfaceTransaction_setBufferAlpha =
      nullptr;
  _ASurfaceTransaction_setBufferDataSpace
      mASurfaceTransaction_setBufferDataSpace = nullptr;
  _ASurfaceTransaction_setHdrMetadata_smpte2086
      mASurfaceTransaction_setHdrMetadata_smpte2086 = nullptr;
  _ASurfaceTransaction_setHdrMetadata_cta861_3
      mASurfaceTransaction_setHdrMetadata_cta861_3 = nullptr;
  _ASurfaceTransaction_setFrameRate mASurfaceTransaction_setFrameRate = nullptr;
  _ASurfaceTransaction_setFrameRateWithChangeStrategy
      mASurfaceTransaction_setFrameRateWithChangeStrategy = nullptr;
  _ASurfaceTransaction_setEnableBackPressure
      mASurfaceTransaction_setEnableBackPressure = nullptr;
  _ASurfaceTransaction_setFrameTimeline mASurfaceTransaction_setFrameTimeline =
      nullptr;

  static StaticAutoPtr<AndroidSurfaceControlApi> sInstance;
};

}  // namespace layers

template <>
class DefaultDelete<ASurfaceControl> {
 public:
  void operator()(ASurfaceControl* aPtr) const {
    layers::AndroidSurfaceControlApi::Get()->ASurfaceControl_release(aPtr);
  }
};

}  // namespace mozilla

#endif
