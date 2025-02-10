/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nullptr; c-basic-offset: 2
 * -*- This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SurfacePoolAndroid.h"

#include "AndroidHardwareBuffer.h"
#include "ScopedGLHelpers.h"
#include "GLContextEGL.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/glue/Debug.h"

namespace mozilla::layers {

/* static */ RefPtr<SurfacePool> SurfacePool::Create(size_t aPoolSizeLimit) {
  return new SurfacePoolAndroid(aPoolSizeLimit);
}

SurfacePoolAndroid::SurfacePoolAndroid(size_t aPoolSizeLimit)
    : mMutex("SurfacePoolAndroid"), mPoolSizeLimit(aPoolSizeLimit) {}

RefPtr<SurfacePoolHandle> SurfacePoolAndroid::GetHandleForGL(
    gl::GLContext* aGL) {
  return new SurfacePoolHandleAndroid(this, aGL);
}

template <typename F>
void SurfacePoolAndroid::ForEachEntry(F aFn) {
  for (auto& iter : mInUseEntries) {
    aFn(iter.second);
  }
  for (auto& entry : mPendingEntries) {
    aFn(entry);
  }
  for (auto& entry : mAvailableEntries) {
    aFn(entry);
  }
}

void SurfacePoolAndroid::DestroyGLResourcesForContext(gl::GLContext* aGL) {
  MutexAutoLock lock(mMutex);

  ForEachEntry([&](SurfacePoolEntry& entry) {
    if (entry.mGLResources && entry.mGLResources->mGL == aGL) {
      entry.mGLResources = Nothing();
    }
  });
  mDepthBuffers.RemoveElementsBy(
      [&](const DepthBufferEntry& entry) { return entry.mGL == aGL; });
}

bool SurfacePoolAndroid::CanRecycleSurfaceForRequest(
    const MutexAutoLock& aProofOfLock, const SurfacePoolEntry& aEntry,
    const gfx::IntSize& aSize, gl::GLContext* aGL) {
  if (aEntry.mSize != aSize) {
    return false;
  }
  if (aEntry.mGLResources) {
    return aEntry.mGLResources->mGL == aGL;
  }
  return aGL == nullptr;
}

RefPtr<AndroidHardwareBuffer> SurfacePoolAndroid::ObtainBufferFromPool(
    const gfx::IntSize& aSize, gl::GLContext* aGL) {
  MutexAutoLock lock(mMutex);

  auto iterToRecycle = std::find_if(
      mAvailableEntries.begin(), mAvailableEntries.end(),
      [&](const SurfacePoolEntry& aEntry) {
        return CanRecycleSurfaceForRequest(lock, aEntry, aSize, aGL);
      });
  if (iterToRecycle != mAvailableEntries.end()) {
    RefPtr<AndroidHardwareBuffer> buffer = iterToRecycle->mHardwareBuffer;
    mInUseEntries.insert({buffer.get(), std::move(*iterToRecycle)});
    mAvailableEntries.RemoveElementAt(iterToRecycle);
    return buffer;
  }

  RefPtr<AndroidHardwareBuffer> buffer;
  if (aGL) {
    // FIXME: RGBA vs RGBX for opaque?
    buffer = AndroidHardwareBuffer::Create(aSize, gfx::SurfaceFormat::B8G8R8A8);
  } else {
    MOZ_RELEASE_ASSERT(false);
  }
  if (buffer) {
    mInUseEntries.insert({buffer.get(), SurfacePoolEntry{aSize, buffer, {}}});
  }

  return buffer;
}

void SurfacePoolAndroid::ReturnBufferToPool(
    RefPtr<AndroidHardwareBuffer>&& aBuffer) {
  MutexAutoLock lock(mMutex);
  auto inUseEntryIter = mInUseEntries.find(aBuffer.get());
  MOZ_RELEASE_ASSERT(inUseEntryIter != mInUseEntries.end());

  if (aBuffer->CheckReleaseFence()) {
    mAvailableEntries.AppendElement(std::move(inUseEntryIter->second));
    mInUseEntries.erase(inUseEntryIter);
  } else {
    mPendingEntries.AppendElement(std::move(inUseEntryIter->second));
    mInUseEntries.erase(inUseEntryIter);
  }
}

void SurfacePoolAndroid::EnforcePoolSizeLimit() {
  MutexAutoLock lock(mMutex);

  // Enforce the pool size limit, removing least-recently-used entries as
  // necessary.
  while (mAvailableEntries.Length() > mPoolSizeLimit) {
    mAvailableEntries.RemoveElementAt(0);
  }

  NS_WARNING_ASSERTION(mPendingEntries.Length() < mPoolSizeLimit * 2,
                       "Are we leaking pending entries?");
  NS_WARNING_ASSERTION(mInUseEntries.size() < mPoolSizeLimit * 2,
                       "Are we leaking in-use entries?");
}

void SurfacePoolAndroid::CollectPendingSurfaces() {
  MutexAutoLock lock(mMutex);
  mPendingEntries.RemoveElementsBy([&](auto& entry) {
    if (entry.mHardwareBuffer->CheckReleaseFence()) {
      mAvailableEntries.AppendElement(std::move(entry));
      return true;
    }
    return false;
  });
}

Maybe<GLuint> SurfacePoolAndroid::GetFramebufferForBuffer(
    const RefPtr<AndroidHardwareBuffer>& aBuffer, gl::GLContext* aGL,
    bool aNeedsDepthBuffer) {
  MutexAutoLock lock(mMutex);
  MOZ_RELEASE_ASSERT(aGL);

  auto inUseEntryIter = mInUseEntries.find(aBuffer.get());
  MOZ_RELEASE_ASSERT(inUseEntryIter != mInUseEntries.end());

  SurfacePoolEntry& entry = inUseEntryIter->second;
  if (entry.mGLResources) {
    // We have an existing framebuffer.
    MOZ_RELEASE_ASSERT(entry.mGLResources->mGL == aGL,
                       "Recycled surface that still had GL resources from a "
                       "different GL context. "
                       "This shouldn't happen.");
    if (!aNeedsDepthBuffer || entry.mGLResources->mFramebuffer->HasDepth()) {
      return Some(entry.mGLResources->mFramebuffer->mFB);
    }
  }

  // No usable existing framebuffer, we need to create one.

  if (!aGL->MakeCurrent()) {
    // Context may have been destroyed.
    return {};
  }

  const auto& gle = gl::GLContextEGL::Cast(aGL);
  const auto& egl = gle->mEgl;

  const EGLint attrs[] = {
      LOCAL_EGL_IMAGE_PRESERVED,
      LOCAL_EGL_TRUE,
      LOCAL_EGL_NONE,
      LOCAL_EGL_NONE,
  };

  EGLClientBuffer clientBuffer =
      egl->mLib->fGetNativeClientBufferANDROID(aBuffer->GetNativeBuffer());
  EGLImage eglImage = egl->fCreateImage(
      EGL_NO_CONTEXT, LOCAL_EGL_NATIVE_BUFFER_ANDROID, clientBuffer, attrs);

  GLuint tex;
  aGL->fGenTextures(1, &tex);
  aGL->fBindTexture(LOCAL_GL_TEXTURE_EXTERNAL, tex);
  gle->fEGLImageTargetTexture2D(LOCAL_GL_TEXTURE_EXTERNAL, eglImage);
  egl->fDestroyImage(eglImage);

  auto fb = CreateFramebufferForTexture(lock, aGL, entry.mSize, tex,
                                        aNeedsDepthBuffer);
  if (!fb) {
    // Framebuffer completeness check may have failed.
    return {};
  }

  GLuint fbo = fb->mFB;
  entry.mGLResources = Some(GLResourcesForBuffer{aGL, tex, std::move(fb)});
  return Some(fbo);
}

RefPtr<gl::DepthAndStencilBuffer> SurfacePoolAndroid::GetDepthBufferForSharing(
    const MutexAutoLock& aProofOfLock, gl::GLContext* aGL,
    const gfx::IntSize& aSize) {
  // Clean out entries for which the weak pointer has become null.
  mDepthBuffers.RemoveElementsBy(
      [&](const DepthBufferEntry& entry) { return !entry.mBuffer; });

  for (const auto& entry : mDepthBuffers) {
    if (entry.mGL == aGL && entry.mSize == aSize) {
      return entry.mBuffer.get();
    }
  }
  return nullptr;
}

UniquePtr<gl::MozFramebuffer> SurfacePoolAndroid::CreateFramebufferForTexture(
    const MutexAutoLock& aProofOfLock, gl::GLContext* aGL,
    const gfx::IntSize& aSize, GLuint aTexture, bool aNeedsDepthBuffer) {
  if (aNeedsDepthBuffer) {
    // Try to find an existing depth buffer of aSize in aGL and create a
    // framebuffer that shares it.
    if (auto buffer = GetDepthBufferForSharing(aProofOfLock, aGL, aSize)) {
      return gl::MozFramebuffer::CreateForBackingWithSharedDepthAndStencil(
          aSize, 0, LOCAL_GL_TEXTURE_EXTERNAL_OES, aTexture, buffer);
    }
  }

  // No depth buffer needed or we didn't find one. Create a framebuffer with a
  // new depth buffer and store a weak pointer to the new depth buffer in
  // mDepthBuffers.
  UniquePtr<gl::MozFramebuffer> fb = gl::MozFramebuffer::CreateForBacking(
      aGL, aSize, 0, aNeedsDepthBuffer, LOCAL_GL_TEXTURE_EXTERNAL_OES,
      aTexture);
  if (fb && fb->GetDepthAndStencilBuffer()) {
    mDepthBuffers.AppendElement(
        DepthBufferEntry{aGL, aSize, fb->GetDepthAndStencilBuffer().get()});
  }

  return fb;
}

SurfacePoolHandleAndroid::SurfacePoolHandleAndroid(
    RefPtr<SurfacePoolAndroid> aPool, gl::GLContext* aGL)
    : mPool(std::move(aPool)), mGL(aGL) {}

void SurfacePoolHandleAndroid::OnBeginFrame() {
  mPool->CollectPendingSurfaces();
}

void SurfacePoolHandleAndroid::OnEndFrame() { mPool->EnforcePoolSizeLimit(); }

RefPtr<AndroidHardwareBuffer> SurfacePoolHandleAndroid::ObtainBufferFromPool(
    const gfx::IntSize& aSize) {
  return mPool->ObtainBufferFromPool(aSize, mGL);
}

void SurfacePoolHandleAndroid::ReturnBufferToPool(
    RefPtr<AndroidHardwareBuffer>&& aBuffer) {
  mPool->ReturnBufferToPool(std::move(aBuffer));
}

Maybe<GLuint> SurfacePoolHandleAndroid::GetFramebufferForBuffer(
    const RefPtr<AndroidHardwareBuffer>& aBuffer, bool aNeedsDepthBuffer) {
  return mPool->GetFramebufferForBuffer(aBuffer, mGL, aNeedsDepthBuffer);
}

}  // namespace mozilla::layers
