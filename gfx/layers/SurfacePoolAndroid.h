/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_SurfacePoolAndroid_h
#define mozilla_layers_SurfacePoolAndroid_h

#include "mozilla/layers/SurfacePool.h"

#include "AndroidHardwareBuffer.h"
#include "GLContextEGL.h"
#include "mozilla/Maybe.h"
#include "mozilla/UniquePtr.h"
#include "nsTArray.h"

namespace mozilla::layers {

class HardwareBufferSurface {
 public:
  static UniquePtr<HardwareBufferSurface> Create(const gfx::IntSize& aSize,
                                                 gl::GLContext* aGL);

  virtual ~HardwareBufferSurface();

  AHardwareBuffer* GetBuffer() const { return mBuffer; }
  gfx::IntSize GetSize() const {
    return gfx::IntSize(mDesc.width, mDesc.height);
  }

  bool IsAttached();
  void SetAttached() { mIsAttached = true; }
  void OnRelease(int aFence);
  RefPtr<gfx::DataSourceSurface> ReadLock();
  RefPtr<gfx::DrawTarget> WriteLock();
  void Unlock();
  Maybe<GLuint> GetFramebuffer(bool aNeedsDepthBuffer);

 private:
  explicit HardwareBufferSurface(AHardwareBuffer* aBuffer, gl::GLContext* aGL,
                                 UniquePtr<gl::Texture> aTexture);

  AHardwareBuffer* mBuffer = nullptr;
  AHardwareBuffer_Desc mDesc;

  bool mIsLocked = false;
  bool mIsAttached = false;
  Maybe<int> mReleaseFence;

  RefPtr<gl::GLContext> mGL;
  const static GLenum mTextureTarget = LOCAL_GL_TEXTURE_2D;
  UniquePtr<gl::Texture> mTexture;
  UniquePtr<gl::MozFramebuffer> mFramebuffer;
};

class SurfacePoolAndroid final : public SurfacePool {
 public:
  // Get a handle for a new window. aGL can be nullptr.
  RefPtr<SurfacePoolHandle> GetHandleForGL(gl::GLContext* aGL) override;

  // Destroy all GL resources associated with aGL managed by this pool.
  void DestroyGLResourcesForContext(gl::GLContext* aGL) override;

 private:
  friend class SurfacePoolHandleAndroid;
  friend RefPtr<SurfacePool> SurfacePool::Create(size_t aPoolSizeLimit);

  explicit SurfacePoolAndroid(size_t aPoolSizeLimit);

  UniquePtr<HardwareBufferSurface> ObtainSurfaceFromPool(
      const gfx::IntSize& aSize, gl::GLContext* aGL);
  void ReturnSurfaceToPool(UniquePtr<HardwareBufferSurface>&& aSurface);
  void EnforcePoolSizeLimit();
  void CollectPendingSurfaces();

  Maybe<GLuint> GetFramebufferForSurface(HardwareBufferSurface* aSurface,
                                         gl::GLContext* aGL,
                                         bool aNeedsDepthBuffer);

  Mutex mMutex MOZ_UNANNOTATED;

  size_t mPoolSizeLimit;
  nsTArray<UniquePtr<HardwareBufferSurface>> mAvailableEntries;
  nsTArray<UniquePtr<HardwareBufferSurface>> mPendingEntries;
};

class SurfacePoolHandleAndroid final : public SurfacePoolHandle {
 public:
  SurfacePoolHandleAndroid* AsSurfacePoolHandleAndroid() override {
    return this;
  }

  const auto& gl() { return mGL; }

  UniquePtr<HardwareBufferSurface> ObtainSurfaceFromPool(
      const gfx::IntSize& aSize);
  void ReturnSurfaceToPool(UniquePtr<HardwareBufferSurface>&& aSurface);

  Maybe<GLuint> GetFramebufferForSurface(HardwareBufferSurface* aSurface,
                                         bool aNeedsDepthBuffer);

  RefPtr<SurfacePool> Pool() override { return mPool; }
  void OnBeginFrame() override;
  void OnEndFrame() override;

 private:
  friend class SurfacePoolAndroid;
  explicit SurfacePoolHandleAndroid(RefPtr<SurfacePoolAndroid> aPool,
                                    gl::GLContext* aGL);

  const RefPtr<SurfacePoolAndroid> mPool;
  const RefPtr<gl::GLContext> mGL;
};

}  // namespace mozilla::layers

#endif
