/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERCOMPOSITOR_ASURFACECONTROL_H
#define MOZILLA_GFX_RENDERCOMPOSITOR_ASURFACECONTROL_H

#include "GLTypes.h"
#include "mozilla/Maybe.h"
#include "mozilla/webrender/RenderCompositor.h"

struct ANativeWindow;

namespace mozilla {

namespace layers {
class AndroidHardwareBuffer;
class AndroidSurfaceControl;
class AndroidSurfaceControlManager;
}  // namespace layers

namespace wr {

class ASurfaceControlSurface;
class ASurfaceControlTile;

class RenderCompositorASurfaceControl : public RenderCompositor {
 public:
  static UniquePtr<RenderCompositor> Create(
      RefPtr<widget::CompositorWidget> aWidget, nsACString& aError);

  explicit RenderCompositorASurfaceControl(
      RefPtr<widget::CompositorWidget> aWidget);
  virtual ~RenderCompositorASurfaceControl();
  bool Initialize(nsACString& aError);

  bool BeginFrame() override;
  RenderedFrameId EndFrame(const nsTArray<DeviceIntRect>& aDirtyRects) final;
  bool WaitForGPU() override;
  void Pause() override;
  bool Resume() override;

  void SetClearColor(wr::ColorF aColor) override;

  gl::GLContext* gl() const override;

  bool MakeCurrent() override;

  bool UseANGLE() const override { return false; }

  LayoutDeviceIntSize GetBufferSize() override;

  bool ShouldUseNativeCompositor() override;
  // uint32_t GetMaxUpdateRects() override;

  // Interface for wr::Compositor
  void CompositorBeginFrame() override;
  void CompositorEndFrame() override;
  void Bind(wr::NativeTileId aId, wr::DeviceIntPoint* aOffset, uint32_t* aFboId,
            wr::DeviceIntRect aDirtyRect,
            wr::DeviceIntRect aValidRect) override;
  void Unbind() override;
  void CreateSurface(wr::NativeSurfaceId aId, wr::DeviceIntPoint aVirtualOffset,
                     wr::DeviceIntSize aTileSize, bool aIsOpaque) override;
  void CreateExternalSurface(wr::NativeSurfaceId aId, bool aIsOpaque) override;
  void DestroySurface(NativeSurfaceId aId) override;
  void CreateTile(wr::NativeSurfaceId aId, int32_t aX, int32_t aY) override;
  void DestroyTile(wr::NativeSurfaceId aId, int32_t aX, int32_t aY) override;
  void AttachExternalImage(wr::NativeSurfaceId aId,
                           wr::ExternalImageId aExternalImage) override;
  void AddSurface(wr::NativeSurfaceId aId,
                  const wr::CompositorSurfaceTransform& aTransform,
                  wr::DeviceIntRect aClipRect,
                  wr::ImageRendering aImageRendering) override;
  void EnableNativeCompositor(bool aEnable) override;
  void GetCompositorCapabilities(CompositorCapabilities* aCaps) override;

  ipc::FileDescriptor GetAndResetReleaseFence() override;

  ANativeWindow* GetNativeWindow() { return mNativeWindow; }

  layers::AndroidSurfaceControlManager* GetSurfaceControlManager() {
    return mSurfaceControlManager;
  }

 protected:
  void ReleaseNativeCompositorResources();

  // Get or create an FBO with depth buffer suitable for specified dimensions
  GLuint GetOrCreateFbo(int aWidth, int aHeight);

  GLuint GetOrCreateFramebuffer(ASurfaceControlTile* aTile, int aWidth,
                                int aHeight);

  ASurfaceControlSurface* GetSurface(wr::NativeSurfaceId aId) const;

  // On android we must track our own surface size.
  LayoutDeviceIntSize mNativeWindowSize;

  ipc::FileDescriptor mReleaseFenceFd;

  struct SurfaceIdHashFn {
    std::size_t operator()(const wr::NativeSurfaceId& aId) const {
      return HashGeneric(wr::AsUint64(aId));
    }
  };

  std::unordered_map<wr::NativeSurfaceId, UniquePtr<ASurfaceControlSurface>,
                     SurfaceIdHashFn>
      mSurfaces;

  // A list of layer IDs as they are added to the visual tree this frame.
  std::vector<wr::NativeSurfaceId> mCurrentLayers;

  // The previous frame's list of layer IDs in visual order.
  std::vector<wr::NativeSurfaceId> mPrevLayers;

  // Information about a cached FBO that is retained between frames.
  struct CachedFrameBuffer {
    int width;
    int height;
    GLuint fboId;
    GLuint depthRboId;
    int lastFrameUsed;
  };

  // A cache of FBOs, containing a depth buffer allocated to a specific size.
  // TODO(gw): Might be faster as a hashmap? The length is typically much less
  // than 10.
  nsTArray<CachedFrameBuffer> mFrameBuffers;

  // The GL render buffer ID that maps the EGLImage to an RBO for attaching to
  // an FBO.
  GLuint mColorRBO;

  int mCurrentFrame = 0;

  RefPtr<layers::AndroidSurfaceControlManager> mSurfaceControlManager;

  ANativeWindow* mNativeWindow;

  Maybe<wr::ColorF> mClearColor;
  RefPtr<layers::AndroidSurfaceControl> mSurfaceControlClearColor;

  // FileDescriptor of acquire fence.
  // Acquire fence is a fence that is used for waiting until rendering to
  // its AHardwareBuffer is completed.
  ipc::FileDescriptor mAcquireFenceFd;

  std::queue<wr::RenderedFrameId> mPendingFrameIds;

  friend class ASurfaceControlSurface;
};

class ASurfaceControlSurface {
 public:
  explicit ASurfaceControlSurface(
      wr::DeviceIntSize aTileSize, bool aIsOpaque,
      RenderCompositorASurfaceControl* aRenderCompositor);
  virtual ~ASurfaceControlSurface();

  bool Initialize();
  void CreateTile(int32_t aX, int32_t aY);
  void DestroyTile(int32_t aX, int32_t aY);

  ASurfaceControlTile* GetTile(int32_t aX, int32_t aY) const;

  struct TileKey {
    TileKey(int32_t aX, int32_t aY) : mX(aX), mY(aY) {}

    int32_t mX;
    int32_t mY;
  };

  wr::DeviceIntSize GetTileSize() const { return mTileSize; }

  void HideAllTiles();

  void UpdateAllocatedRect(int& aZIndex);

  void DirtyAllocatedRect();

 protected:
  RenderCompositorASurfaceControl* mRenderCompositor;

  struct TileKeyHashFn {
    std::size_t operator()(const TileKey& aId) const {
      return HashGeneric(aId.mX, aId.mY);
    }
  };

  wr::DeviceIntSize mTileSize;
  bool mIsOpaque;
  bool mAllocatedRectDirty;
  wr::DeviceIntRect mClipRect;

  int32_t mX = 0;
  int32_t mY = 0;

  std::unordered_map<TileKey, UniquePtr<ASurfaceControlTile>, TileKeyHashFn>
      mTiles;

  friend class RenderCompositorASurfaceControl;
};

class ASurfaceControlTile {
 public:
  explicit ASurfaceControlTile(
      RenderCompositorASurfaceControl* aRenderCompositor);
  ~ASurfaceControlTile();
  bool Initialize(wr::DeviceIntSize aSize, bool aIsOpaque);

 protected:
  gfx::IntRect mValidRect;

  RenderCompositorASurfaceControl* mRenderCompositor;
  RefPtr<layers::AndroidSurfaceControl> mSurfaceControl;

  // XXX needs triple buffering? Single buffer does not work well

  RefPtr<layers::AndroidHardwareBuffer> mAndroidHardwareBuffer;

  // The EGL image that is bound to AndroidHardwareBuffer.
  EGLImage mEGLImage;

  friend class RenderCompositorASurfaceControl;
  friend class ASurfaceControlSurface;
};

static inline bool operator==(const ASurfaceControlSurface::TileKey& a0,
                              const ASurfaceControlSurface::TileKey& a1) {
  return a0.mX == a1.mX && a0.mY == a1.mY;
}

}  // namespace wr
}  // namespace mozilla

#endif  // MOZILLA_GFX_RENDERCOMPOSITOR_EGL_H
