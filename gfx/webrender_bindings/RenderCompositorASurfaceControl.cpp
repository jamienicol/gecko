/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderCompositorASurfaceControl.h"

#include "GLContext.h"
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "GLLibraryEGL.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/BuildConstants.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/widget/CompositorWidget.h"

#include "mozilla/java/GeckoSurfaceTextureWrappers.h"
#include "mozilla/layers/AndroidHardwareBuffer.h"
#include "mozilla/layers/AndroidSurfaceControl.h"
#include "mozilla/layers/AndroidHardwareBuffer.h"
#include "mozilla/widget/AndroidCompositorWidget.h"
#include <android/native_window.h>
#include <android/native_window_jni.h>

namespace mozilla::wr {

/* static */
UniquePtr<RenderCompositor> RenderCompositorASurfaceControl::Create(
    RefPtr<widget::CompositorWidget> aWidget, nsACString& aError) {
  if (!RenderThread::Get()->SingletonGL()) {
    gfxCriticalNote << "Failed to get shared GL context";
    return nullptr;
  }

  UniquePtr<RenderCompositorASurfaceControl> compositor =
      MakeUnique<RenderCompositorASurfaceControl>(std::move(aWidget));
  if (!compositor->Initialize(aError)) {
    return nullptr;
  }
  return compositor;
}

RenderCompositorASurfaceControl::RenderCompositorASurfaceControl(
    RefPtr<widget::CompositorWidget> aWidget)
    : RenderCompositor(std::move(aWidget)), mColorRBO(0) {}

RenderCompositorASurfaceControl::~RenderCompositorASurfaceControl() {
  ReleaseNativeCompositorResources();

  java::GeckoSurfaceTexture::DestroyUnused((int64_t)gl());

  if (mNativeWindow) {
    ANativeWindow_release(mNativeWindow);
    mNativeWindow = nullptr;
  }
}

void RenderCompositorASurfaceControl::ReleaseNativeCompositorResources() {
  if (mColorRBO) {
    gl()->fDeleteRenderbuffers(1, &mColorRBO);
    mColorRBO = 0;
  }
}

bool RenderCompositorASurfaceControl::Initialize(nsACString& aError) {
  mSurfaceControlManager = layers::AndroidSurfaceControlManager::Create();
  if (!mSurfaceControlManager) {
    gfxCriticalNote << "Failed to create AndroidSurfaceControlManager";
    return false;
  }

  return true;
}

GLuint RenderCompositorASurfaceControl::GetOrCreateFramebuffer(
    ASurfaceControlTile* aTile, int aWidth, int aHeight) {
  MOZ_ASSERT(aTile);
  MOZ_ASSERT(aTile->mEGLImage);

  // Get the current FBO and RBO id, so we can restore them later
  GLint currentFboId, currentRboId;
  gl()->fGetIntegerv(LOCAL_GL_DRAW_FRAMEBUFFER_BINDING, &currentFboId);
  gl()->fGetIntegerv(LOCAL_GL_RENDERBUFFER_BINDING, &currentRboId);

  // Create a render buffer object that is backed by the EGL image.
  gl()->fGenRenderbuffers(1, &mColorRBO);
  gl()->fBindRenderbuffer(LOCAL_GL_RENDERBUFFER, mColorRBO);
  gl()->fEGLImageTargetRenderbufferStorage(LOCAL_GL_RENDERBUFFER,
                                           aTile->mEGLImage);

  // Get or create an FBO for the specified dimensions
  GLuint fboId = GetOrCreateFbo(aWidth, aHeight);

  // Attach the new renderbuffer to the FBO
  gl()->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, fboId);
  gl()->fFramebufferRenderbuffer(LOCAL_GL_DRAW_FRAMEBUFFER,
                                 LOCAL_GL_COLOR_ATTACHMENT0,
                                 LOCAL_GL_RENDERBUFFER, mColorRBO);

  // Restore previous FBO and RBO bindings
  gl()->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, currentFboId);
  gl()->fBindRenderbuffer(LOCAL_GL_RENDERBUFFER, currentRboId);

  return fboId;
}

GLuint RenderCompositorASurfaceControl::GetOrCreateFbo(int aWidth,
                                                       int aHeight) {
  GLuint fboId = 0;

  // Check if we have a cached FBO with matching dimensions
  for (auto it = mFrameBuffers.begin(); it != mFrameBuffers.end(); ++it) {
    if (it->width == aWidth && it->height == aHeight) {
      fboId = it->fboId;
      it->lastFrameUsed = mCurrentFrame;
      break;
    }
  }

  // If not, create a new FBO with attached depth buffer
  if (fboId == 0) {
    // Create the depth buffer
    GLuint depthRboId;
    gl()->fGenRenderbuffers(1, &depthRboId);
    gl()->fBindRenderbuffer(LOCAL_GL_RENDERBUFFER, depthRboId);
    gl()->fRenderbufferStorage(LOCAL_GL_RENDERBUFFER,
                               LOCAL_GL_DEPTH_COMPONENT24, aWidth, aHeight);

    // Create the framebuffer and attach the depth buffer to it
    gl()->fGenFramebuffers(1, &fboId);
    gl()->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, fboId);
    gl()->fFramebufferRenderbuffer(LOCAL_GL_DRAW_FRAMEBUFFER,
                                   LOCAL_GL_DEPTH_ATTACHMENT,
                                   LOCAL_GL_RENDERBUFFER, depthRboId);

    // Store this in the cache for future calls.
    // TODO(gw): Maybe we should periodically scan this list and remove old
    // entries that
    //           haven't been used for some time?
    RenderCompositorASurfaceControl::CachedFrameBuffer frameBufferInfo;
    frameBufferInfo.width = aWidth;
    frameBufferInfo.height = aHeight;
    frameBufferInfo.fboId = fboId;
    frameBufferInfo.depthRboId = depthRboId;
    frameBufferInfo.lastFrameUsed = mCurrentFrame;
    mFrameBuffers.AppendElement(frameBufferInfo);
  }

  return fboId;
}

bool RenderCompositorASurfaceControl::BeginFrame() {
  if (!MakeCurrent()) {
    gfxCriticalNote << "Failed to make render context current, can't draw.";
    return false;
  }

  java::GeckoSurfaceTexture::DestroyUnused((int64_t)gl());
  gl()->MakeCurrent();  // DestroyUnused can change the current context!

  return true;
}

RenderedFrameId RenderCompositorASurfaceControl::EndFrame(
    const nsTArray<DeviceIntRect>& aDirtyRects) {
  RenderedFrameId frameId = GetNextRenderFrameId();

  mPendingFrameIds.push(frameId);
  mSurfaceControlManager->Commit(frameId);
  return frameId;
}

bool RenderCompositorASurfaceControl::WaitForGPU() {
  size_t waitLatency = 2;

  auto begin = TimeStamp::Now();

  while (mPendingFrameIds.size() >= waitLatency) {
    auto frameId = mPendingFrameIds.front();

    mSurfaceControlManager->WaitForFrameComplete(frameId);
    mPendingFrameIds.pop();
  }

  auto end = TimeStamp::Now();

  printf_stderr(
      "RenderCompositorASurfaceControl::WaitForGPU() duration %f us this %p\n",
      (end - begin).ToMicroseconds(), this);

  return true;
}

void RenderCompositorASurfaceControl::Pause() {
  ReleaseNativeCompositorResources();
  // XXX clear background color if it exist
}

bool RenderCompositorASurfaceControl::Resume() {
  // Query the new surface size as this may have changed. We cannot use
  // mWidget->GetClientSize() due to a race condition between
  // nsWindow::Resize() being called and the frame being rendered after the
  // surface is resized.
  EGLNativeWindowType window = mWidget->AsAndroid()->GetEGLNativeWindow();
  JNIEnv* const env = jni::GetEnvForThread();
  mNativeWindow =
      ANativeWindow_fromSurface(env, reinterpret_cast<jobject>(window));
  const int32_t width = ANativeWindow_getWidth(mNativeWindow);
  const int32_t height = ANativeWindow_getHeight(mNativeWindow);
  mNativeWindowSize = LayoutDeviceIntSize(width, height);

  if (mClearColor) {
    mSurfaceControlClearColor =
        mSurfaceControlManager->createFromWindow(mNativeWindow);

    mSurfaceControlClearColor->SetColor(mClearColor->r, mClearColor->g,
                                        mClearColor->b, mClearColor->a);
    mSurfaceControlClearColor->SetZOrder(-1);
    const ARect src{0, 0, mNativeWindowSize.width, mNativeWindowSize.height};
    const ARect dst{0, 0, mNativeWindowSize.width, mNativeWindowSize.height};
    mSurfaceControlClearColor->SetGeometry(src, dst,
                                           ANATIVEWINDOW_TRANSFORM_IDENTITY);
    mSurfaceControlManager->Commit(GetNextRenderFrameId());
  }

  return true;
}

void RenderCompositorASurfaceControl::SetClearColor(wr::ColorF aColor) {
  mClearColor = Some(aColor);

  // XXX
}

gl::GLContext* RenderCompositorASurfaceControl::gl() const {
  return RenderThread::Get()->SingletonGL();
}

bool RenderCompositorASurfaceControl::MakeCurrent() {
  return gl()->MakeCurrent();
}

ipc::FileDescriptor RenderCompositorASurfaceControl::GetAndResetReleaseFence() {
  MOZ_ASSERT(!layers::AndroidHardwareBufferApi::Get() ||
             mReleaseFenceFd.IsValid());
  return std::move(mReleaseFenceFd);
}

LayoutDeviceIntSize RenderCompositorASurfaceControl::GetBufferSize() {
  return mNativeWindowSize;
}

bool RenderCompositorASurfaceControl::ShouldUseNativeCompositor() {
  return true;
}

// uint32_t RenderCompositorASurfaceControl::GetMaxUpdateRects() {
//   // XXX
//   return 0;
// }

void RenderCompositorASurfaceControl::CompositorBeginFrame() {
  mCurrentFrame++;
}

void RenderCompositorASurfaceControl::CompositorEndFrame() {
  const auto& gle = gl::GLContextEGL::Cast(gl());
  const auto& egl = gle->mEgl;

  // Clear previous fence.
  mAcquireFenceFd = ipc::FileDescriptor();

  // Get acquire fence.
  // XXX
  EGLSync sync = egl->fCreateSync(LOCAL_EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
  if (sync) {
    int fenceFd = egl->fDupNativeFenceFDANDROID(sync);
    if (fenceFd >= 0) {
      mAcquireFenceFd = ipc::FileDescriptor(UniqueFileHandle(fenceFd));
    }
    egl->fDestroySync(sync);
    sync = nullptr;
  }

  // Check if the visual tree of surfaces is the same as last frame.
  // bool same = mPrevLayers == mCurrentLayers;

  int zIndex = 0;

  for (auto it = mCurrentLayers.begin(); it != mCurrentLayers.end(); ++it) {
    auto surface_it = mSurfaces.find(*it);
    MOZ_RELEASE_ASSERT(surface_it != mSurfaces.end());
    const auto surface = surface_it->second.get();

    surface->UpdateAllocatedRect(zIndex);
  }

  mPrevLayers.swap(mCurrentLayers);
  mCurrentLayers.clear();
}

void RenderCompositorASurfaceControl::Bind(wr::NativeTileId aId,
                                           wr::DeviceIntPoint* aOffset,
                                           uint32_t* aFboId,
                                           wr::DeviceIntRect aDirtyRect,
                                           wr::DeviceIntRect aValidRect) {
  auto surface = GetSurface(aId.surface_id);
  auto tile = surface->GetTile(aId.x, aId.y);

  gfx::IntRect validRect(aValidRect.min.x, aValidRect.min.y,
                         aValidRect.width(), aValidRect.height());
  if (!tile->mValidRect.IsEqualEdges(validRect)) {
    // XXX
    tile->mValidRect = validRect;
    // surface->DirtyAllocatedRect();
  }
  wr::DeviceIntSize tileSize = surface->GetTileSize();

  *aFboId = GetOrCreateFramebuffer(tile, tileSize.width, tileSize.height);
  *aOffset = wr::DeviceIntPoint{0, 0};
}

void RenderCompositorASurfaceControl::Unbind() {
  if (mColorRBO) {
    gl()->fDeleteRenderbuffers(1, &mColorRBO);
    mColorRBO = 0;
  }
}

void RenderCompositorASurfaceControl::CreateSurface(
    wr::NativeSurfaceId aId, wr::DeviceIntPoint aVirtualOffset,
    wr::DeviceIntSize aTileSize, bool aIsOpaque) {
  auto it = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(it == mSurfaces.end());
  if (it != mSurfaces.end()) {
    // Surface already exists.
    return;
  }

  auto surface = MakeUnique<ASurfaceControlSurface>(aTileSize, aIsOpaque, this);
  if (!surface->Initialize()) {
    gfxCriticalNote << "Failed to initialize Surface: " << wr::AsUint64(aId);
    return;
  }

  mSurfaces[aId] = std::move(surface);
}

void RenderCompositorASurfaceControl::CreateExternalSurface(
    wr::NativeSurfaceId aId, bool aIsOpaque) {}

void RenderCompositorASurfaceControl::DestroySurface(NativeSurfaceId aId) {
  auto it = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(it != mSurfaces.end());

  mSurfaces.erase(it);
}

void RenderCompositorASurfaceControl::CreateTile(wr::NativeSurfaceId aId,
                                                 int32_t aX, int32_t aY) {
  auto surface = GetSurface(aId);
  surface->CreateTile(aX, aY);
}

void RenderCompositorASurfaceControl::DestroyTile(wr::NativeSurfaceId aId,
                                                  int32_t aX, int32_t aY) {
  auto surface = GetSurface(aId);
  surface->DestroyTile(aX, aY);
}

void RenderCompositorASurfaceControl::AttachExternalImage(
    wr::NativeSurfaceId aId, wr::ExternalImageId aExternalImage) {}

void RenderCompositorASurfaceControl::AddSurface(
    wr::NativeSurfaceId aId, const wr::CompositorSurfaceTransform& aTransform,
    wr::DeviceIntRect aClipRect, wr::ImageRendering aImageRendering) {
  auto it = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(it != mSurfaces.end());
  const auto surface = it->second.get();

  // XXX
  surface->mX = aTransform.m41;
  surface->mY = aTransform.m42;
  surface->mClipRect = aClipRect;

  mCurrentLayers.push_back(aId);
}

void RenderCompositorASurfaceControl::EnableNativeCompositor(bool aEnable) {}

void
RenderCompositorASurfaceControl::GetCompositorCapabilities(CompositorCapabilities* aCaps) {
  // Does not use virtual surfaces
  aCaps->virtual_surface_size = 0;
}

ASurfaceControlSurface* RenderCompositorASurfaceControl::GetSurface(
    wr::NativeSurfaceId aId) const {
  auto it = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(it != mSurfaces.end());
  return it->second.get();
}

ASurfaceControlSurface::ASurfaceControlSurface(
    wr::DeviceIntSize aTileSize, bool aIsOpaque,
    RenderCompositorASurfaceControl* aRenderCompositor)
    : mRenderCompositor(aRenderCompositor),
      mTileSize(aTileSize),
      mIsOpaque(aIsOpaque),
      mAllocatedRectDirty(true) {}

ASurfaceControlSurface::~ASurfaceControlSurface() {}

bool ASurfaceControlSurface::Initialize() { return true; }

void ASurfaceControlSurface::CreateTile(int aX, int aY) {
  TileKey key(aX, aY);
  MOZ_RELEASE_ASSERT(mTiles.find(key) == mTiles.end());

  auto tile = MakeUnique<ASurfaceControlTile>(mRenderCompositor);
  if (!tile->Initialize(mTileSize, mIsOpaque)) {
    gfxCriticalNote << "Failed to initialize Tile: " << aX << aY;
    return;
  }

  mAllocatedRectDirty = true;

  mTiles[key] = std::move(tile);
}

void ASurfaceControlSurface::DestroyTile(int aX, int aY) {
  TileKey key(aX, aY);
  mAllocatedRectDirty = true;
  mTiles.erase(key);
}

void ASurfaceControlSurface::DirtyAllocatedRect() {
  mAllocatedRectDirty = true;
}

void ASurfaceControlSurface::HideAllTiles() {
  for (auto it = mTiles.begin(); it != mTiles.end(); ++it) {
    auto tile = GetTile(it->first.mX, it->first.mY);

    tile->mSurfaceControl->SetVisibility(ASURFACE_TRANSACTION_VISIBILITY_HIDE);
  }
}

void ASurfaceControlSurface::UpdateAllocatedRect(int& aZIndex) {
  for (auto it = mTiles.begin(); it != mTiles.end(); ++it) {
    auto tile = GetTile(it->first.mX, it->first.mY);

    // XXX
    aZIndex++;
    tile->mSurfaceControl->SetZOrder(aZIndex);

    const auto transparency =
        mIsOpaque ? ASURFACE_TRANSACTION_TRANSPARENCY_OPAQUE
                  : ASURFACE_TRANSACTION_TRANSPARENCY_TRANSLUCENT;
    tile->mSurfaceControl->SetBufferTransparency(transparency);

    int32_t left = mX + it->first.mX * mTileSize.width + tile->mValidRect.x;
    int32_t top = mY + it->first.mY * mTileSize.height + tile->mValidRect.y;
    int32_t right = left + tile->mValidRect.width;
    int32_t bottom = top + tile->mValidRect.height;

    // ARect needs to be >= 0
    if (bottom < 0 || right < 0) {
      tile->mSurfaceControl->SetVisibility(
          ASURFACE_TRANSACTION_VISIBILITY_HIDE);
      continue;
    } else {
      tile->mSurfaceControl->SetVisibility(
          ASURFACE_TRANSACTION_VISIBILITY_SHOW);
    }

    // XXX Add clip and mValidRect.x, mValidRect.y handling
    int srcLeft = 0;
    int srcTop = 0;
    int srcRight = tile->mValidRect.width;
    int srcBottom = tile->mValidRect.height;

    if (left < 0) {
      srcLeft = -left;
      left = 0;
    }

    if (top < 0) {
      srcTop = -top;
      top = 0;
    }

    int32_t fenceFd = -1;
    if (mRenderCompositor->mAcquireFenceFd.IsValid()) {
      // copy fence
      ipc::FileDescriptor acquireFenceFd = mRenderCompositor->mAcquireFenceFd;

      auto rawFd = acquireFenceFd.TakePlatformHandle();
      fenceFd = rawFd.release();
    }

    // XXX
    tile->mSurfaceControl->SetBuffer(
        tile->mAndroidHardwareBuffer->GetNativeBuffer(), fenceFd);

    // XXX ARect needs to be >= 0.
    const ARect src{srcLeft, srcTop, srcRight, srcBottom};
    const ARect dst{left, top, right, bottom};

    tile->mSurfaceControl->SetGeometry(src, dst,
                                       ANATIVEWINDOW_TRANSFORM_IDENTITY);

    tile->mSurfaceControl->SetDamageRegion(&dst, 1);
  }
}

ASurfaceControlTile* ASurfaceControlSurface::GetTile(int aX, int aY) const {
  TileKey key(aX, aY);
  auto it = mTiles.find(key);
  MOZ_RELEASE_ASSERT(it != mTiles.end());
  return it->second.get();
}

ASurfaceControlTile::ASurfaceControlTile(
    RenderCompositorASurfaceControl* aRenderCompositor)
    : mRenderCompositor(aRenderCompositor), mEGLImage(EGL_NO_IMAGE) {}

ASurfaceControlTile::~ASurfaceControlTile() {
  auto begin = TimeStamp::Now();

  // XXX
  if (mEGLImage) {
    const auto& gle = gl::GLContextEGL::Cast(mRenderCompositor->gl());
    const auto& egl = gle->mEgl;
    egl->fDestroyImage(mEGLImage);
    mEGLImage = EGL_NO_IMAGE;
  }

  auto end = TimeStamp::Now();

  printf_stderr(
      "ASurfaceControlTile::~ASurfaceControlTile() duration %f us this %p\n",
      (end - begin).ToMicroseconds(), this);
}

bool ASurfaceControlTile::Initialize(wr::DeviceIntSize aSize, bool aIsOpaque) {
  if (aSize.width <= 0 || aSize.height <= 0) {
    return false;
  }

  auto begin1 = TimeStamp::Now();

  const auto& gle = gl::GLContextEGL::Cast(mRenderCompositor->gl());
  const auto& egl = gle->mEgl;

  mAndroidHardwareBuffer = layers::AndroidHardwareBuffer::Create(
      gfx::IntSize(aSize.width, aSize.height), gfx::SurfaceFormat::R8G8B8A8);
  if (!mAndroidHardwareBuffer) {
    gfxCriticalNote << "Failed to create AndroidHardwareBuffer";
    return false;
  }

  auto end1 = TimeStamp::Now();

  const EGLint attrs[] = {
      LOCAL_EGL_IMAGE_PRESERVED,
      LOCAL_EGL_TRUE,
      LOCAL_EGL_NONE,
      LOCAL_EGL_NONE,
  };

  auto begin2 = TimeStamp::Now();

  EGLClientBuffer clientBuffer = egl->mLib->fGetNativeClientBufferANDROID(
      mAndroidHardwareBuffer->GetNativeBuffer());
  mEGLImage = egl->fCreateImage(EGL_NO_CONTEXT, LOCAL_EGL_NATIVE_BUFFER_ANDROID,
                                clientBuffer, attrs);
  if (!mEGLImage) {
    gfxCriticalNote << "Failed to create EGLImage";
    return false;
  }

  auto end2 = TimeStamp::Now();

  auto begin3 = TimeStamp::Now();

  mSurfaceControl =
      mRenderCompositor->GetSurfaceControlManager()->createFromWindow(
          mRenderCompositor->GetNativeWindow());
  if (!mSurfaceControl) {
    gfxCriticalNote << "Failed to create SurfaceControl";
    return false;
  }

  // Initially, the entire tile is considered valid, unless it is set by
  // the SetTileProperties method.
  mValidRect.x = 0;
  mValidRect.y = 0;
  mValidRect.width = aSize.width;
  mValidRect.height = aSize.height;

  auto end = TimeStamp::Now();

  printf_stderr(
      "ASurfaceControlTile::Initialize() dur1 %f us dur2 %f us dur3 %f "
      "duration %f us this %p\n",
      (end1 - begin1).ToMicroseconds(), (end2 - begin2).ToMicroseconds(),
      (end - begin3).ToMicroseconds(), (end - begin1).ToMicroseconds(), this);

  return true;
}

}  // namespace mozilla::wr
