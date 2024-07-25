/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nullptr; c-basic-offset: 2
 * -*- This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SurfacePoolAndroid.h"

#include "AndroidHardwareBuffer.h"

#include <poll.h>

namespace mozilla::layers {

/* static */ UniquePtr<HardwareBufferSurface> HardwareBufferSurface::Create(
    const gfx::IntSize& aSize, gl::GLContext* aGL) {
  auto api = AndroidHardwareBufferApi::Get();

  AHardwareBuffer_Desc desc = {};
  desc.width = aSize.width;
  desc.height = aSize.height;
  desc.layers = 1;
  // Need both GPU_SAMPLED_IMAGE and COMPOSER_OVERLAY so
  // SurfaceFlinger can composite using HardwareComposer or GLES
  desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
               AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;
  if (aGL) {
    desc.usage |= AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                  AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
  } else {
    desc.usage |= AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                  AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
  }
  // FIXME: make format configurable?
  desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;

  AHardwareBuffer* buffer;
  api->Allocate(&desc, &buffer);

  if (!buffer) {
    printf_stderr("jamiedbg Failed to create AndroidHardwareBuffer\n");
    return nullptr;
  }

  if (aGL) {
    // FIXME: error handling. make buffer, clientBuffer, and eglImage UniquePtrs
    // with deleters?
    const auto& gle = gl::GLContextEGL::Cast(aGL);
    const auto& egl = gle->mEgl;
    EGLClientBuffer clientBuffer =
        egl->mLib->fGetNativeClientBufferANDROID(buffer);
    if (!clientBuffer) {
      printf_stderr("jamiedbg Failed to create EGLClientBuffer: 0x%x\n",
                    gle->fGetError());
      return nullptr;
    }
    EGLint attrs[] = {LOCAL_EGL_NONE};
    EGLImage eglImage = egl->fCreateImage(
        EGL_NO_CONTEXT, LOCAL_EGL_NATIVE_BUFFER_ANDROID, clientBuffer, attrs);
    if (!eglImage) {
      printf_stderr("jamiedbg Failed to create EGLImage: 0x%x\n",
                    gle->fGetError());
      return nullptr;
    }

    auto tex = MakeUnique<gl::Texture>(*aGL);
    {
      gl::ScopedBindTexture texture(gle, tex->name, mTextureTarget);
      aGL->fTexParameteri(mTextureTarget, LOCAL_GL_TEXTURE_MIN_FILTER,
                          LOCAL_GL_LINEAR);
      aGL->fTexParameteri(mTextureTarget, LOCAL_GL_TEXTURE_MAG_FILTER,
                          LOCAL_GL_LINEAR);
      aGL->fTexParameteri(mTextureTarget, LOCAL_GL_TEXTURE_WRAP_S,
                          LOCAL_GL_CLAMP_TO_EDGE);
      aGL->fTexParameteri(mTextureTarget, LOCAL_GL_TEXTURE_WRAP_T,
                          LOCAL_GL_CLAMP_TO_EDGE);
      gle->fEGLImageTargetTexture2D(mTextureTarget, eglImage);
    }
    egl->fDestroyImage(eglImage);

    return WrapUnique(new HardwareBufferSurface(buffer, aGL, std::move(tex)));
  } else {
    MOZ_ASSERT_UNREACHABLE("Haven't implemented SWGL support yet");
    return nullptr;
  }
}

HardwareBufferSurface::HardwareBufferSurface(AHardwareBuffer* aBuffer,
                                             gl::GLContext* aGL,
                                             UniquePtr<gl::Texture> aTexture)
    : mBuffer(aBuffer), mGL(aGL), mTexture(std::move(aTexture)) {
  auto api = AndroidHardwareBufferApi::Get();
  api->Describe(mBuffer, &mDesc);
}

HardwareBufferSurface::~HardwareBufferSurface() {
  auto api = AndroidHardwareBufferApi::Get();
  api->Release(mBuffer);
}

void HardwareBufferSurface::OnRelease(int aFence) {
  MOZ_ASSERT(mIsAttached);
  MOZ_ASSERT(!mIsLocked);
  MOZ_ASSERT(mReleaseFence.isNothing());
  mIsAttached = false;
  if (aFence != -1) {
    mReleaseFence = Some(aFence);
  }
}

bool HardwareBufferSurface::IsAttached() {
  if (mIsAttached) {
    return true;
  }

  if (!mReleaseFence) {
    return false;
  }

  pollfd p;
  p.fd = *mReleaseFence;
  p.events = POLLIN;
  int ret = ::poll(&p, 1, 0);
  if (ret == -1) {
    printf_stderr("jamiedbg Error in poll(): %s\n", strerror(errno));
    return true;
  } else if (ret == 0) {
    return true;
  } else {
    ::close(*mReleaseFence);
    mReleaseFence.reset();
    return false;
  }
}

RefPtr<gfx::DataSourceSurface> HardwareBufferSurface::ReadLock() {
  MOZ_ASSERT(!mIsLocked);
  MOZ_ASSERT(!IsAttached());
  MOZ_ASSERT((mDesc.usage & AHARDWAREBUFFER_USAGE_CPU_READ_MASK) !=
             AHARDWAREBUFFER_USAGE_CPU_READ_NEVER);

  auto* api = AndroidHardwareBufferApi::Get();
  uint8_t* buf;
  int err = api->Lock(mBuffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1,
                      nullptr, (void**)&buf);
  if (err) {
    printf_stderr("jamiedbg Failed to read lock AHardwareBuffer: %d\n", err);
    return nullptr;
  }

  mIsLocked = true;

  // FIXME: handle other formats.
  gfx::SurfaceFormat format = gfx::SurfaceFormat::B8G8R8A8;

  // FIXME: can we use the SourceSurfaceDeallocator to unlock?
  return gfx::Factory::CreateWrappingDataSourceSurface(
      buf, mDesc.stride * gfx::BytesPerPixel(format),
      gfx::IntSize(mDesc.width, mDesc.height), format);
}

RefPtr<gfx::DrawTarget> HardwareBufferSurface::WriteLock() {
  MOZ_ASSERT(!mIsLocked);
  MOZ_ASSERT(!IsAttached());
  MOZ_ASSERT((mDesc.usage & AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK) !=
             AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER);

  auto* api = AndroidHardwareBufferApi::Get();
  unsigned char* buf;
  // FIXME: handle synchronization
  int err = api->Lock(mBuffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1,
                      nullptr, (void**)&buf);
  if (err) {
    printf_stderr("jamiedbg Failed to write lock AHardwareBuffer: %d\n", err);
    return nullptr;
  }

  mIsLocked = true;

  // FIXME: handle other formats.
  gfx::SurfaceFormat format = gfx::SurfaceFormat::B8G8R8A8;
  return gfxPlatform::CreateDrawTargetForData(
      buf, gfx::IntSize(mDesc.width, mDesc.height),
      mDesc.stride * gfx::BytesPerPixel(format), format);
}

void HardwareBufferSurface::Unlock() {
  MOZ_ASSERT(mIsLocked);
  MOZ_ASSERT(!IsAttached());
  mIsLocked = false;

  auto* api = AndroidHardwareBufferApi::Get();
  // FIXME: obtain fence and plumb through to transaction
  int err = api->Unlock(mBuffer, nullptr);
  if (err) {
    printf_stderr("jamiedbg Failed to unlock AHardwareBuffer: %d\n", err);
    return;
  }
}

Maybe<GLuint> HardwareBufferSurface::GetFramebuffer(bool aNeedsDepthBuffer) {
  MOZ_ASSERT(!mIsLocked);
  MOZ_RELEASE_ASSERT(mGL);

  if (!mGL->MakeCurrent()) {
    printf_stderr("jamiedbg MakeCurrent failed\n");
    return Nothing();
  }

  if (mFramebuffer) {
    if (!aNeedsDepthBuffer || mFramebuffer->HasDepth()) {
      return Some(mFramebuffer->mFB);
    }
  }

  // FIXME: use shared depth buffer
  mFramebuffer = gl::MozFramebuffer::CreateForBacking(
      mGL, GetSize(), 0, aNeedsDepthBuffer, mTextureTarget, mTexture->name);

  if (!mFramebuffer) {
    printf_stderr("jamiedbg Failed to create framebuffer\n");
    return Nothing();
  }

  return Some(mFramebuffer->mFB);
}

/* static */ RefPtr<SurfacePool> SurfacePool::Create(size_t aPoolSizeLimit) {
  return new SurfacePoolAndroid(aPoolSizeLimit);
}

SurfacePoolAndroid::SurfacePoolAndroid(size_t aPoolSizeLimit)
    : mMutex("SurfacePoolAndroid"), mPoolSizeLimit(aPoolSizeLimit) {}

RefPtr<SurfacePoolHandle> SurfacePoolAndroid::GetHandleForGL(
    gl::GLContext* aGL) {
  return new SurfacePoolHandleAndroid(this, aGL);
}

void SurfacePoolAndroid::DestroyGLResourcesForContext(gl::GLContext* aGL) {}

UniquePtr<HardwareBufferSurface> SurfacePoolAndroid::ObtainSurfaceFromPool(
    const gfx::IntSize& aSize, gl::GLContext* aGL) {
  MutexAutoLock lock(mMutex);

  auto it = std::find_if(
      mAvailableEntries.begin(), mAvailableEntries.end(),
      [&](const auto& aEntry) { return aEntry->GetSize() == aSize; });
  if (it != mAvailableEntries.end()) {
    std::iter_swap(it, mAvailableEntries.end() - 1);
    return mAvailableEntries.PopLastElement();
  }

  return HardwareBufferSurface::Create(aSize, aGL);
}

void SurfacePoolAndroid::ReturnSurfaceToPool(
    UniquePtr<HardwareBufferSurface>&& aSurface) {
  MutexAutoLock lock(mMutex);
  if (aSurface->IsAttached()) {
    mPendingEntries.AppendElement(std::move(aSurface));
  } else {
    mAvailableEntries.AppendElement(std::move(aSurface));
  }
}

void SurfacePoolAndroid::EnforcePoolSizeLimit() {
  MutexAutoLock lock(mMutex);
  if (mAvailableEntries.Length() > mPoolSizeLimit) {
    mAvailableEntries.TruncateLength(mPoolSizeLimit);
  }
}

void SurfacePoolAndroid::CollectPendingSurfaces() {
  MutexAutoLock lock(mMutex);
  mPendingEntries.RemoveElementsBy([&](auto& surface) {
    if (!surface->IsAttached()) {
      mAvailableEntries.AppendElement(std::move(surface));
      return true;
    }
    return false;
  });
}

Maybe<GLuint> SurfacePoolAndroid::GetFramebufferForSurface(
    HardwareBufferSurface* aSurface, gl::GLContext* aGL,
    bool aNeedsDepthBuffer) {
  MutexAutoLock lock(mMutex);
  // FIXME: store resources in pool instead of on Surface?
  return aSurface->GetFramebuffer(aNeedsDepthBuffer);
}

SurfacePoolHandleAndroid::SurfacePoolHandleAndroid(
    RefPtr<SurfacePoolAndroid> aPool, gl::GLContext* aGL)
    : mPool(std::move(aPool)), mGL(aGL) {}

UniquePtr<HardwareBufferSurface>
SurfacePoolHandleAndroid::ObtainSurfaceFromPool(const gfx::IntSize& aSize) {
  return mPool->ObtainSurfaceFromPool(aSize, mGL);
}

void SurfacePoolHandleAndroid::ReturnSurfaceToPool(
    UniquePtr<HardwareBufferSurface>&& aSurface) {
  mPool->ReturnSurfaceToPool(std::move(aSurface));
}

Maybe<GLuint> SurfacePoolHandleAndroid::GetFramebufferForSurface(
    HardwareBufferSurface* aSurface, bool aNeedsDepthBuffer) {
  return mPool->GetFramebufferForSurface(aSurface, mGL, aNeedsDepthBuffer);
}

void SurfacePoolHandleAndroid::OnBeginFrame() {
  mPool->CollectPendingSurfaces();
}

void SurfacePoolHandleAndroid::OnEndFrame() { mPool->EnforcePoolSizeLimit(); }

}  // namespace mozilla::layers
