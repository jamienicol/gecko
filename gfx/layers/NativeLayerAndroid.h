/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_NativeLayerAndroid_h
#define mozilla_layers_NativeLayerAndroid_h

#include "SurfacePoolAndroid.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/MatrixFwd.h"
#include "mozilla/layers/AndroidSurfaceControl.h"
#include "mozilla/layers/NativeLayer.h"
#include "mozilla/layers/SurfacePool.h"
#include <android/native_window.h>

namespace mozilla::layers {

class SurfacePoolHandleAndroid;

class NativeLayerRootAndroid : public NativeLayerRoot {
 public:
  // FIXME: do we recreate whenever the window changes? or have a
  // single instance and reparent when the window changes
  static already_AddRefed<NativeLayerRootAndroid> Create(
      ANativeWindow* aNativeWindow);

  NativeLayerRootAndroid* AsNativeLayerRootAndroid() override { return this; }

  already_AddRefed<NativeLayer> CreateLayer(
      const gfx::IntSize& aSize, bool aIsOpaque,
      SurfacePoolHandle* aSurfacePoolHandle) override;
  already_AddRefed<NativeLayer> CreateLayerForExternalTexture(
      bool aIsOpaque, SurfacePoolHandle* aSurfacePoolHandle) override;
  already_AddRefed<NativeLayer> CreateLayerForColor(
      gfx::DeviceColor aColor, SurfacePoolHandle* aSurfacePoolHandle) override;

  void AppendLayer(NativeLayer* aLayer) override;
  void RemoveLayer(NativeLayer* aLayer) override;
  void SetLayers(const nsTArray<RefPtr<NativeLayer>>& aLayers) override;

  void PrepareForCommit() override;
  bool CommitToScreen() override;

  // FIXME: better name
  void SetLayersRenderedFence(int aFence);

  struct ReleasedSurface {
    UniquePtr<HardwareBufferSurface> mSurface;
    RefPtr<layers::SurfacePoolHandleAndroid> mSurfacePoolHandle;
  };

 private:
  NativeLayerRootAndroid(UniquePtr<ASurfaceControl>&& aSurfaceControl);
  ~NativeLayerRootAndroid();

  void OnTransactionComplete(ASurfaceTransactionStats* stats);

  Maybe<int> mLayersRenderedFence;

  Mutex mMutex MOZ_UNANNOTATED;

  UniquePtr<ASurfaceControl> mSurfaceControl;

  nsTArray<RefPtr<NativeLayerAndroid>> mSublayers;
  nsTArray<RefPtr<NativeLayerAndroid>> mOldSublayers;
  bool mNewLayers = false;

  std::queue<std::map<ASurfaceControl*, ReleasedSurface>> mReleasedSurfaces;
  std::queue<nsTArray<RefPtr<NativeLayerAndroid>>> mRemovedLayers;
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

  // FIXME: combine in to single shared constructor?
  NativeLayerAndroid(UniquePtr<ASurfaceControl> aSurfaceControl,
                     const gfx::IntSize& aSize, bool aIsOpaque,
                     SurfacePoolHandleAndroid* aSurfacePoolHandle);
  NativeLayerAndroid(UniquePtr<ASurfaceControl> aSurfaceControl, bool aIsOpaque,
                     SurfacePoolHandleAndroid* aSurfacePoolHandle);
  NativeLayerAndroid(UniquePtr<ASurfaceControl> aSurfaceControl,
                     gfx::DeviceColor aColor,
                     SurfacePoolHandleAndroid* aSurfacePoolHandle);
  ~NativeLayerAndroid() override;

  void HandlePartialUpdate(const MutexAutoLock& aProofOfLock);

  void Update(ASurfaceTransaction* aTransaction, ASurfaceControl* aParent,
              std::map<ASurfaceControl*,
                       NativeLayerRootAndroid::ReleasedSurface>& aPrevBuffers,
              int z, const Maybe<int>& aFence);
  void Remove(ASurfaceTransaction* aTransaction,
              std::map<ASurfaceControl*,
                       NativeLayerRootAndroid::ReleasedSurface>& aPrevSurfaces);

  Mutex mMutex MOZ_UNANNOTATED;

  const RefPtr<SurfacePoolHandleAndroid> mSurfacePoolHandle;
  const gfx::IntSize mSize;
  const bool mIsOpaque = false;
  const Maybe<gfx::DeviceColor> mColor;
  gfx::IntPoint mPosition;
  gfx::Matrix4x4 mTransform;
  Maybe<gfx::IntRect> mClipRect;
  gfx::IntRect mDisplayRect;
  bool mSurfaceIsFlipped = false;
  gfx::SamplingFilter mSamplingFilter = gfx::SamplingFilter::POINT;
  gfx::IntRegion mDirtyRegion;

  UniquePtr<ASurfaceControl> mSurfaceControl;

  UniquePtr<HardwareBufferSurface> mInProgressBuffer;
  UniquePtr<HardwareBufferSurface> mFrontBuffer;
  bool mFrontBufferChanged = false;
  UniquePtr<HardwareBufferSurface> mPrevFrontBuffer;
};

}  // namespace mozilla::layers

#endif  // mozilla_layers_NativeLayerAndroid_h
