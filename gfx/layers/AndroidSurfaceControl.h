/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_ANDROID_SURFACE_CONTROL
#define MOZILLA_LAYERS_ANDROID_SURFACE_CONTROL

#include <android/surface_control.h>
// surface_control.h very unhelpfully doesn't even declare the types
// when __ANDROID_API__ < 29, so we must declare them ourselves.
#if __ANDROID_API__ < 29
extern "C" {
typedef struct ASurfaceControl ASurfaceControl;
typedef struct ASurfaceTransaction ASurfaceTransaction;
typedef struct ASurfaceTransactionStats ASurfaceTransactionStats;
typedef void (*ASurfaceTransaction_OnComplete)(void* context,
                                               ASurfaceTransactionStats* stats);
}
enum {
  ASURFACE_TRANSACTION_VISIBILITY_HIDE = 0,
  ASURFACE_TRANSACTION_VISIBILITY_SHOW = 1,
};
#endif
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"

namespace mozilla {
namespace layers {

/**
 * FIXME: add comment
 */
class AndroidSurfaceControlApi final {
 public:
  static void Init();
  static void Shutdown();

  static const AndroidSurfaceControlApi* Get() { return sInstance; }

  ASurfaceControl* ASurfaceControl_createFromWindow(
      ANativeWindow* parent, const char* debug_name) const;
  ASurfaceControl* ASurfaceControl_create(ASurfaceControl* parent,
                                          const char* debug_name) const;
  void ASurfaceControl_release(ASurfaceControl* surface_control) const;

  ASurfaceTransaction* ASurfaceTransaction_create() const;
  void ASurfaceTransaction_delete(ASurfaceTransaction* transaction) const;
  void ASurfaceTransaction_apply(ASurfaceTransaction* transaction) const;
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
  void ASurfaceTransaction_setBufferTransparency(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      int8_t transparency) const;
  void ASurfaceTransaction_setDamageRegion(ASurfaceTransaction* transaction,
                                           ASurfaceControl* surface_control,
                                           const ARect rects[],
                                           uint32_t count) const;
  void ASurfaceTransaction_setBufferAlpha(ASurfaceTransaction* transaction,
                                          ASurfaceControl* surface_control,
                                          float alpha) const;
  void ASurfaceTransaction_setOnComplete(
      ASurfaceTransaction* transaction, void* context,
      ASurfaceTransaction_OnComplete func) const;
  void ASurfaceTransactionStats_getASurfaceControls(
      ASurfaceTransactionStats* surface_transaction_stats,
      ASurfaceControl*** outASurfaceControls,
      size_t* outASurfaceControlsSize) const;
  void ASurfaceTransactionStats_releaseASurfaceControls(
      ASurfaceControl** surface_controls) const;
  int ASurfaceTransactionStats_getPreviousReleaseFenceFd(
      ASurfaceTransactionStats* surface_transaction_stats,
      ASurfaceControl* surface_control) const;

 private:
  AndroidSurfaceControlApi() = default;
  bool Load();

  using _ASurfaceControl_createFromWindow =
      ASurfaceControl* (*)(ANativeWindow* parent, const char* debug_name);
  using _ASurfaceControl_create = ASurfaceControl* (*)(ASurfaceControl* parent,
                                                       const char* debug_name);
  using _ASurfaceControl_release = void (*)(ASurfaceControl* surface_control);
  using _ASurfaceTransaction_create = ASurfaceTransaction* (*)();
  using _ASurfaceTransaction_delete =
      void (*)(ASurfaceTransaction* transaction);
  using _ASurfaceTransaction_apply = void (*)(ASurfaceTransaction* transaction);
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
  using _ASurfaceTransaction_setBufferTransparency =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, int8_t transparency);
  using _ASurfaceTransaction_setDamageRegion = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      const ARect rects[], uint32_t count);
  using _ASurfaceTransaction_setBufferAlpha =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, float alpha);
  using _ASurfaceTransaction_setOnComplete =
      void (*)(ASurfaceTransaction* transaction, void* context,
               ASurfaceTransaction_OnComplete func);
  using _ASurfaceTransactionStats_getASurfaceControls = void (*)(
      ASurfaceTransactionStats* surface_transaction_stats,
      ASurfaceControl*** outASurfaceControls, size_t* outASurfaceControlsSize);
  using _ASurfaceTransactionStats_releaseASurfaceControls =
      void (*)(ASurfaceControl** surface_controls);
  using _ASurfaceTransactionStats_getPreviousReleaseFenceFd =
      int (*)(ASurfaceTransactionStats* surface_transaction_stats,
              ASurfaceControl* surface_control);

  _ASurfaceControl_createFromWindow mASurfaceControl_createFromWindow = nullptr;
  _ASurfaceControl_create mASurfaceControl_create = nullptr;
  _ASurfaceControl_release mASurfaceControl_release = nullptr;
  _ASurfaceTransaction_create mASurfaceTransaction_create = nullptr;
  _ASurfaceTransaction_delete mASurfaceTransaction_delete = nullptr;
  _ASurfaceTransaction_apply mASurfaceTransaction_apply = nullptr;
  _ASurfaceTransaction_reparent mASurfaceTransaction_reparent = nullptr;
  _ASurfaceTransaction_setVisibility mASurfaceTransaction_setVisibility =
      nullptr;
  _ASurfaceTransaction_setZOrder mASurfaceTransaction_setZOrder = nullptr;
  _ASurfaceTransaction_setBuffer mASurfaceTransaction_setBuffer = nullptr;
  _ASurfaceTransaction_setColor mASurfaceTransaction_setColor = nullptr;
  _ASurfaceTransaction_setGeometry mASurfaceTransaction_setGeometry = nullptr;
  _ASurfaceTransaction_setBufferTransparency
      mASurfaceTransaction_setBufferTransparency = nullptr;
  ;
  _ASurfaceTransaction_setDamageRegion mASurfaceTransaction_setDamageRegion =
      nullptr;
  _ASurfaceTransaction_setBufferAlpha mASurfaceTransaction_setBufferAlpha =
      nullptr;
  _ASurfaceTransaction_setOnComplete mASurfaceTransaction_setOnComplete =
      nullptr;
  _ASurfaceTransactionStats_getASurfaceControls
      mASurfaceTransactionStats_getASurfaceControls = nullptr;
  _ASurfaceTransactionStats_releaseASurfaceControls
      mASurfaceTransactionStats_releaseASurfaceControls = nullptr;
  _ASurfaceTransactionStats_getPreviousReleaseFenceFd
      mASurfaceTransactionStats_getPreviousReleaseFenceFd = nullptr;

  static StaticAutoPtr<AndroidSurfaceControlApi> sInstance;
};

}  // namespace layers

template <>
class DefaultDelete<ASurfaceControl> {
 public:
  void operator()(ASurfaceControl* aPtr) const {
    printf_stderr("jamiedbg Calling ASurfaceControl_release()\n");
    layers::AndroidSurfaceControlApi::Get()->ASurfaceControl_release(aPtr);
  }
};

}  // namespace mozilla

#endif
