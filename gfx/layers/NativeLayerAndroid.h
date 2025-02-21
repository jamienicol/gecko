/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_NativeLayerAndroid_h
#define mozilla_layers_NativeLayerAndroid_h

#include "android/surface_control.h"
#include "mozilla/Mutex.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/Variant.h"
#include "mozilla/layers/NativeLayer.h"
#include "nsTArray.h"
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

namespace wr {
class RenderAndroidHardwareBufferTextureHost;
}

namespace layers {

class AndroidHardwareBuffer;
class SurfacePoolHandleAndroid;

class NativeLayerAndroidBufferSource {
 public:
  explicit NativeLayerAndroidBufferSource(
      RefPtr<AndroidHardwareBuffer> aBuffer);
  explicit NativeLayerAndroidBufferSource(
      RefPtr<wr::RenderAndroidHardwareBufferTextureHost> aTextureHost);

  auto operator=(RefPtr<AndroidHardwareBuffer> aBuffer);
  auto operator=(
      RefPtr<wr::RenderAndroidHardwareBufferTextureHost> aTextureHost);
  operator bool() const;
  RefPtr<AndroidHardwareBuffer> Buffer() const;
  RefPtr<wr::RenderAndroidHardwareBufferTextureHost> AsTextureHost() const;

  bool IsExternal() const;

  Variant<RefPtr<AndroidHardwareBuffer>,
          RefPtr<wr::RenderAndroidHardwareBufferTextureHost>>
      mBuffer;
};

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
  UniquePtr<NativeLayerRootSnapshotter> CreateSnapshotter() override;

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
    RefPtr<SurfacePoolHandleAndroid> mSurfacePoolHandle;
    NativeLayerAndroidBufferSource mBuffer;
  };
  std::queue<std::unordered_map<ASurfaceControl*, PendingBuffer>>
      mPendingBuffers;
};

class NativeLayerRootSnapshotterAndroid final
    : public NativeLayerRootSnapshotter {
 public:
  static UniquePtr<NativeLayerRootSnapshotterAndroid> Create() {
    return WrapUnique(new NativeLayerRootSnapshotterAndroid());
  }

  bool ReadbackPixels(const gfx::IntSize& aReadbackSize,
                      gfx::SurfaceFormat aReadbackFormat,
                      const Range<uint8_t>& aReadbackBuffer) override;
  already_AddRefed<profiler_screenshots::RenderSource> GetWindowContents(
      const gfx::IntSize& aWindowSize) override;
  already_AddRefed<profiler_screenshots::DownscaleTarget> CreateDownscaleTarget(
      const gfx::IntSize& aSize) override;
  already_AddRefed<profiler_screenshots::AsyncReadbackBuffer>
  CreateAsyncReadbackBuffer(const gfx::IntSize& aSize) override;

 private:
  NativeLayerRootSnapshotterAndroid() = default;
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
  gfx::IntSize mSize;
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
  NativeLayerAndroidBufferSource mFrontBuffer;
  NativeLayerAndroidBufferSource mPrevFrontBuffer;
  bool mFrontBufferUpdated = false;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_NativeLayerAndroid_h
