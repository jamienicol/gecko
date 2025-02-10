/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_SurfacePoolAndroid_h
#define mozilla_layers_SurfacePoolAndroid_h

#include "mozilla/layers/SurfacePool.h"

#include "AndroidHardwareBuffer.h"
#include "GLContext.h"
#include "mozilla/Maybe.h"
#include "mozilla/UniquePtr.h"
#include "nsTArray.h"

namespace mozilla::layers {

class AndroidHardwareBuffer;

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

  RefPtr<AndroidHardwareBuffer> ObtainBufferFromPool(const gfx::IntSize& aSize,
                                                     gl::GLContext* aGL);
  void ReturnBufferToPool(RefPtr<AndroidHardwareBuffer>&& aBuffer);
  void EnforcePoolSizeLimit();
  void CollectPendingSurfaces();
  Maybe<GLuint> GetFramebufferForBuffer(
      const RefPtr<AndroidHardwareBuffer>& aBuffer, gl::GLContext* aGL,
      bool aNeedsDepthBuffer);

  struct GLResourcesForBuffer final {
    RefPtr<gl::GLContext> mGL;  // non-null
    GLuint mTexture;
    UniquePtr<gl::MozFramebuffer> mFramebuffer;  // non-null
  };

  struct SurfacePoolEntry final {
    const gfx::IntSize mSize;
    RefPtr<AndroidHardwareBuffer> mHardwareBuffer;  // non-null
    Maybe<GLResourcesForBuffer> mGLResources;
  };

  bool CanRecycleSurfaceForRequest(const MutexAutoLock& aProofOfLock,
                                   const SurfacePoolEntry& aEntry,
                                   const gfx::IntSize& aSize,
                                   gl::GLContext* aGL);

  RefPtr<gl::DepthAndStencilBuffer> GetDepthBufferForSharing(
      const MutexAutoLock& aProofOfLock, gl::GLContext* aGL,
      const gfx::IntSize& aSize);
  UniquePtr<gl::MozFramebuffer> CreateFramebufferForTexture(
      const MutexAutoLock& aProofOfLock, gl::GLContext* aGL,
      const gfx::IntSize& aSize, GLuint aTexture, bool aNeedsDepthBuffer);

  Mutex mMutex MOZ_UNANNOTATED;

  // Stores the entries for surfaces that are in use by NativeLayerAndroid, i.e.
  // an entry is inside mInUseEntries between calls to ObtainSurfaceFromPool()
  // and ReturnSurfaceToPool().
  std::unordered_map<AndroidHardwareBuffer*, SurfacePoolEntry> mInUseEntries;

  // Stores entries which are no longer in use by NativeLayerAndroid but are
  // still in use by the window server, i.e. for which
  // AndroidHardwareBuffer::CheckReleaseFence() still returns false.
  // These entries are checked once per frame inside
  // CollectPendingSurfaces(), and returned to mAvailableEntries once the
  // window server is done.
  nsTArray<SurfacePoolEntry> mPendingEntries;

  // Stores entries which are available for recycling. These entries are not
  // in use by a NativeLayerAndroid or by the window server.
  nsTArray<SurfacePoolEntry> mAvailableEntries;
  size_t mPoolSizeLimit;

  template <typename F>
  void ForEachEntry(F aFn);

  struct DepthBufferEntry final {
    RefPtr<gl::GLContext> mGL;
    gfx::IntSize mSize;
    WeakPtr<gl::DepthAndStencilBuffer> mBuffer;
  };

  nsTArray<DepthBufferEntry> mDepthBuffers;
};

// A surface pool handle that is stored on NativeLayerAndroid and keeps the
// SurfacePool alive.
class SurfacePoolHandleAndroid final : public SurfacePoolHandle {
 public:
  SurfacePoolHandleAndroid* AsSurfacePoolHandleAndroid() override {
    return this;
  }

  RefPtr<AndroidHardwareBuffer> ObtainBufferFromPool(const gfx::IntSize& aSize);
  void ReturnBufferToPool(RefPtr<AndroidHardwareBuffer>&& aBuffer);
  Maybe<GLuint> GetFramebufferForBuffer(
      const RefPtr<AndroidHardwareBuffer>& aBuffer, bool aNeedsDepthBuffer);
  const auto& gl() { return mGL; }

  RefPtr<SurfacePool> Pool() override { return mPool; }
  void OnBeginFrame() override;
  void OnEndFrame() override;

 private:
  friend class SurfacePoolAndroid;
  SurfacePoolHandleAndroid(RefPtr<SurfacePoolAndroid> aPool,
                           gl::GLContext* aGL);

  const RefPtr<SurfacePoolAndroid> mPool;
  const RefPtr<gl::GLContext> mGL;
};

}  // namespace mozilla::layers

#endif
