/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_SurfacePoolAndroid_h
#define mozilla_layers_SurfacePoolAndroid_h

#include <android/hardware_buffer.h>
#include <cstdint>
#include "GLContext.h"
#include "GLContextEGL.h"
#include "GLTypes.h"
#include "MozFramebuffer.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/layers/SurfacePool.h"
#include "mozilla/layers/TextureHost.h"

namespace mozilla::layers {

class HardwareBufferSurface {
 public:
  static UniquePtr<HardwareBufferSurface> Create(const gfx::IntSize& aSize,
                                                 gl::GLContext* aGL);

  // FIXME: does this have to be public for UniquePtr to work?
  explicit HardwareBufferSurface(AHardwareBuffer* aBuffer);
  explicit HardwareBufferSurface(AHardwareBuffer* aBuffer, gl::GLContext* aGL,
                                 UniquePtr<gl::Texture> aTexture);
  // FIXME: make virtual if I add subclasses
  ~HardwareBufferSurface();

  AHardwareBuffer* GetBuffer() const { return mBuffer; }
  gfx::IntSize GetSize() const {
    return gfx::IntSize(mDesc.width, mDesc.height);
  }

  RefPtr<gfx::DataSourceSurface> ReadLock();
  RefPtr<gfx::DrawTarget> WriteLock();
  void Unlock();

  Maybe<GLuint> GetFramebuffer(bool aNeedsDepthBuffer);
  void UnlockFramebuffer();

  bool IsConsumerAttached() const;
  int32_t SetConsumerAttached();
  void OnConsumerRelease(int32_t aFence);

 private:
  AHardwareBuffer* mBuffer = nullptr;
  AHardwareBuffer_Desc mDesc;
  bool mLocked = false;
  bool mIsConsumerAttached = false;
  Maybe<int32_t> mConsumerReleaseFence;
  Maybe<int32_t> mConsumerAcquireFence;

  RefPtr<gl::GLContext> mGL;
  UniquePtr<gl::Texture> mTexture;
  const static GLenum mTextureTarget = LOCAL_GL_TEXTURE_2D;
  UniquePtr<gl::MozFramebuffer> mFramebuffer;
};

class SurfacePoolAndroid final : public SurfacePool {
  // Get a handle for a new window. aGL can be nullptr.
  RefPtr<SurfacePoolHandle> GetHandleForGL(gl::GLContext* aGL) override;

  // Destroy all GL resources associated with aGL managed by this pool.
  void DestroyGLResourcesForContext(gl::GLContext* aGL) override;

 private:
  friend class SurfacePoolHandleAndroid;
  friend RefPtr<SurfacePool> SurfacePool::Create(size_t aPoolSizeLimit);

  explicit SurfacePoolAndroid(size_t aPoolSizeLimit);
  ~SurfacePoolAndroid() override;

  UniquePtr<HardwareBufferSurface> ObtainBufferFromPool(
      const gfx::IntSize& aSize, gl::GLContext* aGL);
  void ReturnBufferToPool(UniquePtr<HardwareBufferSurface> aBuffer);
  void EnforcePoolSizeLimit();
  void CollectPendingSurfaces();

  Mutex mMutex MOZ_UNANNOTATED;

  std::vector<UniquePtr<HardwareBufferSurface>> mPendingEntries;
  std::vector<UniquePtr<HardwareBufferSurface>> mAvailableEntries;
  size_t mPoolSizeLimit;
};

// A surface pool handle that is stored on NativeLayerAndroid and keeps the
// SurfacePool alive.
class SurfacePoolHandleAndroid final : public SurfacePoolHandle {
 public:
  SurfacePoolHandleAndroid* AsSurfacePoolHandleAndroid() override {
    return this;
  }

  RefPtr<SurfacePool> Pool() override { return mPool; }
  void OnBeginFrame() override;
  void OnEndFrame() override;

  UniquePtr<HardwareBufferSurface> ObtainBufferFromPool(
      const gfx::IntSize& aSize);
  void ReturnBufferToPool(UniquePtr<HardwareBufferSurface> aBuffer);
  const auto& gl() { return mGL; }

 private:
  friend class SurfacePoolAndroid;
  SurfacePoolHandleAndroid(RefPtr<SurfacePoolAndroid> aPool,
                           gl::GLContext* aGL);

  const RefPtr<SurfacePoolAndroid> mPool;
  const RefPtr<gl::GLContext> mGL;
};

}  // namespace mozilla::layers
#endif
