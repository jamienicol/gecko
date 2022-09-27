/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidSurfaceControl.h"

#include <dlfcn.h>
#include "mozilla/gfx/Logging.h"

namespace mozilla {
namespace layers {

StaticAutoPtr<AndroidSurfaceControlApi> AndroidSurfaceControlApi::sInstance;

/* static */
void AndroidSurfaceControlApi::Init() {
  sInstance = new AndroidSurfaceControlApi();
  if (!sInstance->Load()) {
    sInstance = nullptr;
  }
}

/* static */
void AndroidSurfaceControlApi::Shutdown() { sInstance = nullptr; }

bool AndroidSurfaceControlApi::Load() {
  void* handle = dlopen("libandroid.so", RTLD_LAZY | RTLD_LOCAL);
  MOZ_ASSERT(handle);
  if (!handle) {
    gfxCriticalNote << "Failed to load libandroid.so";
    return false;
  }

  mASurfaceControl_createFromWindow = (_ASurfaceControl_createFromWindow)dlsym(
      handle, "ASurfaceControl_createFromWindow");
  mASurfaceControl_create =
      (_ASurfaceControl_create)dlsym(handle, "ASurfaceControl_create");
  mASurfaceControl_release =
      (_ASurfaceControl_release)dlsym(handle, "ASurfaceControl_release");
  mASurfaceTransaction_create =
      (_ASurfaceTransaction_create)dlsym(handle, "ASurfaceTransaction_create");
  mASurfaceTransaction_delete =
      (_ASurfaceTransaction_delete)dlsym(handle, "ASurfaceTransaction_delete");
  mASurfaceTransaction_apply =
      (_ASurfaceTransaction_apply)dlsym(handle, "ASurfaceTransaction_apply");
  mASurfaceTransaction_reparent = (_ASurfaceTransaction_reparent)dlsym(
      handle, "ASurfaceTransaction_reparent");
  mASurfaceTransaction_setVisibility =
      (_ASurfaceTransaction_setVisibility)dlsym(
          handle, "ASurfaceTransaction_setVisibility");
  mASurfaceTransaction_setZOrder = (_ASurfaceTransaction_setZOrder)dlsym(
      handle, "ASurfaceTransaction_setZOrder");
  mASurfaceTransaction_setBuffer = (_ASurfaceTransaction_setBuffer)dlsym(
      handle, "ASurfaceTransaction_setBuffer");
  mASurfaceTransaction_setColor = (_ASurfaceTransaction_setColor)dlsym(
      handle, "ASurfaceTransaction_setColor");
  mASurfaceTransaction_setGeometry = (_ASurfaceTransaction_setGeometry)dlsym(
      handle, "ASurfaceTransaction_setGeometry");
  mASurfaceTransaction_setBufferTransparency =
      (_ASurfaceTransaction_setBufferTransparency)dlsym(
          handle, "ASurfaceTransaction_setBufferTransparency");
  mASurfaceTransaction_setDamageRegion =
      (_ASurfaceTransaction_setDamageRegion)dlsym(
          handle, "ASurfaceTransaction_setDamageRegion");
  mASurfaceTransaction_setBufferAlpha =
      (_ASurfaceTransaction_setBufferAlpha)dlsym(
          handle, "ASurfaceTransaction_setBufferAlpha");
  mASurfaceTransaction_setOnComplete =
      (_ASurfaceTransaction_setOnComplete)dlsym(
          handle, "ASurfaceTransaction_setOnComplete");
  mASurfaceTransactionStats_getASurfaceControls =
      (_ASurfaceTransactionStats_getASurfaceControls)dlsym(
          handle, "ASurfaceTransactionStats_getASurfaceControls");
  mASurfaceTransactionStats_releaseASurfaceControls =
      (_ASurfaceTransactionStats_releaseASurfaceControls)dlsym(
          handle, "ASurfaceTransactionStats_releaseASurfaceControls");
  mASurfaceTransactionStats_getPreviousReleaseFenceFd =
      (_ASurfaceTransactionStats_getPreviousReleaseFenceFd)dlsym(
          handle, "ASurfaceTransactionStats_getPreviousReleaseFenceFd");

  if (!mASurfaceControl_createFromWindow || !mASurfaceControl_create ||
      !mASurfaceControl_release || !mASurfaceTransaction_create ||
      !mASurfaceTransaction_delete || !mASurfaceTransaction_apply ||
      !mASurfaceTransaction_reparent || !mASurfaceTransaction_setVisibility ||
      !mASurfaceTransaction_setZOrder || !mASurfaceTransaction_setBuffer ||
      !mASurfaceTransaction_setColor || !mASurfaceTransaction_setGeometry ||
      !mASurfaceTransaction_setBufferTransparency ||
      !mASurfaceTransaction_setDamageRegion ||
      !mASurfaceTransaction_setBufferAlpha ||
      !mASurfaceTransaction_setOnComplete ||
      !mASurfaceTransactionStats_getASurfaceControls ||
      !mASurfaceTransactionStats_releaseASurfaceControls ||
      !mASurfaceTransactionStats_getPreviousReleaseFenceFd) {
    gfxCriticalNote << "Failed to load AndroidSurfaceControlApi";
    return false;
  }

  return true;
}

ASurfaceControl* AndroidSurfaceControlApi::ASurfaceControl_createFromWindow(
    ANativeWindow* parent, const char* debug_name) const {
  return mASurfaceControl_createFromWindow(parent, debug_name);
}

ASurfaceControl* AndroidSurfaceControlApi::ASurfaceControl_create(
    ASurfaceControl* parent, const char* debug_name) const {
  return mASurfaceControl_create(parent, debug_name);
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

void AndroidSurfaceControlApi::ASurfaceTransaction_setBufferAlpha(
    ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
    float alpha) const {
  mASurfaceTransaction_setBufferAlpha(transaction, surface_control, alpha);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setOnComplete(
    ASurfaceTransaction* transaction, void* context,
    ASurfaceTransaction_OnComplete func) const {
  mASurfaceTransaction_setOnComplete(transaction, context, func);
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

int AndroidSurfaceControlApi::
    ASurfaceTransactionStats_getPreviousReleaseFenceFd(
        ASurfaceTransactionStats* surface_transaction_stats,
        ASurfaceControl* surface_control) const {
  return mASurfaceTransactionStats_getPreviousReleaseFenceFd(
      surface_transaction_stats, surface_control);
}

}  // namespace layers
}  // namespace mozilla
