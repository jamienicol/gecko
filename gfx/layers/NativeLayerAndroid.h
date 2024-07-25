/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_NativeLayerAndroid_h
#define mozilla_layers_NativeLayerAndroid_h

#include "mozilla/layers/NativeLayer.h"

#include "mozilla/HashTable.h"
#include "mozilla/layers/AndroidSurfaceControl.h"
#include "mozilla/layers/SurfacePoolAndroid.h"
#include "mozilla/Variant.h"
#include "mozilla/layers/AndroidImage.h"

#include <unordered_map>
#include <queue>

namespace mozilla::layers {

  using NativeLayerAndroidBufferSource =
      Variant<UniquePtr<HardwareBufferSurface>, RefPtr<AndroidImage>>;

class NativeLayerRootAndroid final : public NativeLayerRoot {
 public:
  explicit NativeLayerRootAndroid();

  virtual NativeLayerRootAndroid* AsNativeLayerRootAndroid() override {
    return this;
  }

  already_AddRefed<NativeLayer> CreateLayer(
      const gfx::IntSize& aSize, bool aIsOpaque,
      SurfacePoolHandle* aSurfacePoolHandle) override;
  already_AddRefed<NativeLayer> CreateLayerForExternalTexture(
      bool aIsOpaque) override;
  already_AddRefed<NativeLayer> CreateLayerForColor(
      gfx::DeviceColor aColor) override;

  void AppendLayer(NativeLayer* aLayer) override;
  void RemoveLayer(NativeLayer* aLayer) override;
  void SetLayers(const nsTArray<RefPtr<NativeLayer>>& aLayers) override;

  void PrepareForCommit() override;
  bool CommitToScreen() override;

  UniquePtr<NativeLayerRootSnapshotter> CreateSnapshotter() override;

  void SetLayersRenderedFence(int aFence);
  void OnTransactionCommit(ASurfaceTransactionStats* stats);
  void OnTransactionComplete(ASurfaceTransactionStats* stats);

  bool Attach(ANativeWindow* aNativeWindow);
  void Detach();

 private:
  // FIXME: Currently needs to be a monitor so we can wait for OnCommit
  // callback. But we should instead be using timestamps/vsyncIds
  Monitor mMonitor MOZ_UNANNOTATED;

  UniquePtr<ASurfaceControl> mSurfaceControl;

  nsTArray<RefPtr<NativeLayerAndroid>> mSublayers;
  nsTArray<RefPtr<NativeLayerAndroid>> mOldSublayers;
  bool mMutatedLayers = false;

  Maybe<int> mLayersRenderedFence;

  struct ReleasedBuffer {
    RefPtr<NativeLayerAndroid> mLayer;
    NativeLayerAndroidBufferSource mSurface;
  };
  std::queue<std::unordered_map<ASurfaceControl*, ReleasedBuffer>>
      mReleasedBuffers;

  // FIXME: Used to ensure we don't commit multiple transactions before
  // receiving the OnCommit callback. It would be better to use present times /
  // vsync IDs
  bool mPendingCommit = false;
};

class NativeLayerAndroid final : public NativeLayer {
 public:
  NativeLayerAndroid* AsNativeLayerAndroid() override { return this; }

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

 private:
  friend class NativeLayerRootAndroid;

  explicit NativeLayerAndroid(UniquePtr<ASurfaceControl>&& aSurfaceControl,
                              const gfx::IntSize& aSize, bool aIsOpaque,
                              SurfacePoolHandleAndroid* aSurfacePoolHandle);
  explicit NativeLayerAndroid(UniquePtr<ASurfaceControl>&& aSurfaceControl,
                              bool aIsOpaque);
  ~NativeLayerAndroid() override;

  void HandlePartialUpdate(const MutexAutoLock& aProofOfLock);

  void Update(ASurfaceTransaction* aTransaction, ASurfaceControl* aParent,
              int aZOrder, const Maybe<int>& aFence,
              Maybe<NativeLayerAndroidBufferSource>& aOutPrevFrontBuffer);
  void Remove(ASurfaceTransaction* aTransaction,
              Maybe<NativeLayerAndroidBufferSource>& aOutFrontBuffer);

  Mutex mMutex MOZ_UNANNOTATED;

  const UniquePtr<ASurfaceControl> mSurfaceControl;
  gfx::IntSize mSize;
  const bool mIsOpaque;
  const RefPtr<SurfacePoolHandleAndroid> mSurfacePoolHandle;

  gfx::IntPoint mPosition;
  gfx::Matrix4x4 mTransform;
  Maybe<gfx::IntRect> mClipRect;
  gfx::IntRect mDisplayRect;
  gfx::IntRegion mDirtyRegion;
  bool mSurfaceIsFlipped = false;
  gfx::SamplingFilter mSamplingFilter;

  NativeLayerAndroidBufferSource mFrontBuffer;
  NativeLayerAndroidBufferSource mPrevFrontBuffer;
  bool mFrontBufferUpdated = false;
  UniquePtr<HardwareBufferSurface> mInProgressBuffer;
};

}  // namespace mozilla::layers

#endif
