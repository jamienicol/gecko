/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_NativeLayerAndroid_h
#define mozilla_layers_NativeLayerAndroid_h

#include "mozilla/Mutex.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/layers/NativeLayer.h"
#include "nsTArray.h"
#include <android/surface_control.h>
#include <queue>
#include <unordered_map>

namespace mozilla {

template <>
class DefaultDelete<ASurfaceControl> {
 public:
  void operator()(ASurfaceControl* aPtr) const {
    ASurfaceControl_release(aPtr);
  }
};

namespace layers {

class AndroidHardwareBuffer;
class SurfacePoolHandleAndroid;

class NativeLayerRootAndroid final : public NativeLayerRoot {
 public:
  static already_AddRefed<NativeLayerRootAndroid> Create();

  virtual NativeLayerRootAndroid* AsNativeLayerRootAndroid() override {
    return this;
  }

  // Overridden methods
  already_AddRefed<NativeLayer> CreateLayer(
      const gfx::IntSize& aSize, bool aIsOpaque,
      SurfacePoolHandle* aSurfacePoolHandle) override;
  already_AddRefed<NativeLayer> CreateLayerForExternalTexture(
      bool aIsOpaque) override;

  void AppendLayer(NativeLayer* aLayer) override;
  void RemoveLayer(NativeLayer* aLayer) override;
  void SetLayers(const nsTArray<RefPtr<NativeLayer>>& aLayers) override;

  bool CommitToScreen() override;

  bool Attach(ANativeWindow* aNativeWindow);
  void Detach();
  void SetLayersRenderedFence(UniqueFileHandle&& aFence);

 private:
  explicit NativeLayerRootAndroid();

  void OnTransactionComplete(ASurfaceTransactionStats* stats);

  Mutex mMutex MOZ_UNANNOTATED;

  UniquePtr<ASurfaceControl> mSurfaceControl;

  nsTArray<RefPtr<NativeLayerAndroid>> mSublayers;
  nsTArray<RefPtr<NativeLayerAndroid>> mRemovedSublayers;
  bool mMutatedLayers = false;
  UniqueFileHandle mLayersRenderedFence;

  struct PendingBuffer {
    RefPtr<NativeLayerAndroid> mLayer;
    RefPtr<AndroidHardwareBuffer> mBuffer;
  };
  std::queue<std::unordered_map<ASurfaceControl*, PendingBuffer>>
      mPendingBuffers;
};

class NativeLayerAndroid final : public NativeLayer {
 public:
  NativeLayerAndroid* AsNativeLayerAndroid() override { return this; }

  // Overridden methods
  gfx::IntSize GetSize() override;
  bool IsOpaque() override;
  void SetPosition(const gfx::IntPoint& aPosition) override;
  gfx::IntPoint GetPosition() override;
  void SetTransform(const gfx::Matrix4x4& aTransform) override;
  gfx::Matrix4x4 GetTransform() override;
  gfx::IntRect GetRect() override;
  void SetClipRect(const Maybe<gfx::IntRect>& aClipRect) override;
  Maybe<gfx::IntRect> ClipRect() override;
  gfx::IntRect CurrentSurfaceDisplayRect() override;
  void SetSurfaceIsFlipped(bool aIsFlipped) override;
  bool SurfaceIsFlipped() override;
  void SetSamplingFilter(gfx::SamplingFilter aSamplingFilter) override;
  RefPtr<gfx::DrawTarget> NextSurfaceAsDrawTarget(
      const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
      gfx::BackendType aBackendType) override;
  Maybe<GLuint> NextSurfaceAsFramebuffer(const gfx::IntRect& aDisplayRect,
                                         const gfx::IntRegion& aUpdateRegion,
                                         bool aNeedsDepth) override;
  void NotifySurfaceReady() override;
  void DiscardBackbuffers() override;
  void AttachExternalImage(wr::RenderTextureHost* aExternalImage) override;
  GpuFence* GetGpuFence() override { return nullptr; }

  void Commit();
  void Unmap();
  const auto& GetSurfacePoolHandle() { return mSurfacePoolHandle; };

 private:
  friend class NativeLayerRootAndroid;

  explicit NativeLayerAndroid(UniquePtr<ASurfaceControl>&& aSurfaceControl,
                              const gfx::IntSize& aSize, bool aIsOpaque,
                              SurfacePoolHandleAndroid* aSurfacePoolHandle);
  explicit NativeLayerAndroid(UniquePtr<ASurfaceControl>&& aSurfaceControl,
                              bool aIsOpaque);

  void HandlePartialUpdate(const MutexAutoLock& aProofOfLock);

  Mutex mMutex MOZ_UNANNOTATED;

  const RefPtr<SurfacePoolHandleAndroid> mSurfacePoolHandle;
  const gfx::IntSize mSize;
  const bool mIsOpaque = false;
  gfx::IntPoint mPosition;
  gfx::Matrix4x4 mTransform;
  gfx::IntRect mDisplayRect;
  gfx::IntRegion mDirtyRegion;
  Maybe<gfx::IntRect> mClipRect;
  gfx::SamplingFilter mSamplingFilter = gfx::SamplingFilter::POINT;
  bool mSurfaceIsFlipped = false;

  const UniquePtr<ASurfaceControl> mSurfaceControl;

  RefPtr<AndroidHardwareBuffer> mInProgressBuffer;
  RefPtr<AndroidHardwareBuffer> mFrontBuffer;
  RefPtr<AndroidHardwareBuffer> mPrevFrontBuffer;
  bool mFrontBufferUpdated = false;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_NativeLayerAndroid_h
