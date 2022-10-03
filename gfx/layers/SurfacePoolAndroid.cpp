/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nullptr; c-basic-offset: 2
 * -*- This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SurfacePoolAndroid.h"
#include "AndroidSurfaceControl.h"
#include "GLContext.h"
#include "GLTypes.h"
#include "gfxPlatform.h"
#include <android/hardware_buffer.h>
#include <algorithm>
#include <cstdint>
#include "AndroidHardwareBuffer.h"
#include "mozilla/Assertions.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Point.h"
#include "ScopedGLHelpers.h"

namespace mozilla::layers {

using gl::GLContext;

/* static */ UniquePtr<HardwareBufferSurface> HardwareBufferSurface::Create(
    const gfx::IntSize& aSize, gl::GLContext* aGL) {
  auto api = AndroidHardwareBufferApi::Get();
  if (!api) {
    // printf_stderr("jamiedbg API not initialized\n");
    return nullptr;
  }

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
    // printf_stderr("jamiedbg Failed to create AndroidHardwareBuffer\n");
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
      // printf_stderr("jamiedbg Failed to create EGLClientBuffer: 0x%x\n",
      //               gle->fGetError());
      return nullptr;
    }
    EGLint attrs[] = {LOCAL_EGL_NONE};
    EGLImage eglImage = egl->fCreateImage(
        EGL_NO_CONTEXT, LOCAL_EGL_NATIVE_BUFFER_ANDROID, clientBuffer, attrs);
    if (!eglImage) {
      // printf_stderr("jamiedbg Failed to create EGLImage: 0x%x\n",
      //               gle->fGetError());
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

    return MakeUnique<HardwareBufferSurface>(buffer, aGL, std::move(tex));
  } else {
    return MakeUnique<HardwareBufferSurface>(buffer);
  }
}

HardwareBufferSurface::HardwareBufferSurface(AHardwareBuffer* aBuffer)
    : mBuffer(aBuffer) {
  // printf_stderr("jamiedbg HardwareBufferSurface CPU constructor() %p\n",
  // this);
  auto api = AndroidHardwareBufferApi::Get();
  api->Describe(mBuffer, &mDesc);
}

HardwareBufferSurface::HardwareBufferSurface(AHardwareBuffer* aBuffer,
                                             gl::GLContext* aGL,
                                             UniquePtr<gl::Texture> aTexture)
    : mBuffer(aBuffer), mGL(aGL), mTexture(std::move(aTexture)) {
  // printf_stderr("jamiedbg HardwareBufferSurface GPU constructor() %p\n",
  // this);
  auto api = AndroidHardwareBufferApi::Get();
  api->Describe(mBuffer, &mDesc);
}

HardwareBufferSurface::~HardwareBufferSurface() {
  // printf_stderr("jamiedbg HardwareBufferSurface destructor() %p\n", this);
  MOZ_CRASH(
      "Until I start enforcing pool size, these should never be destructed");
  if (mConsumerReleaseFence) {
    close(*mConsumerReleaseFence);
  }
  if (mConsumerAcquireFence) {
    close(*mConsumerAcquireFence);
  }
  auto api = AndroidHardwareBufferApi::Get();
  api->Release(mBuffer);
}

RefPtr<gfx::DataSourceSurface> HardwareBufferSurface::ReadLock() {
  MOZ_ASSERT(!mLocked);
  MOZ_ASSERT((mDesc.usage & AHARDWAREBUFFER_USAGE_CPU_READ_MASK) !=
             AHARDWAREBUFFER_USAGE_CPU_READ_NEVER);

  auto api = AndroidHardwareBufferApi::Get();
  uint8_t* buf;
  int err = api->Lock(GetBuffer(), AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1,
                      nullptr, (void**)&buf);
  if (err) {
    gfxCriticalError() << "Failed to read lock AHardwareBuffer: " << err;
    return nullptr;
  }

  mLocked = true;

  // FIXME: handle other formats.
  gfx::SurfaceFormat format = gfx::SurfaceFormat::B8G8R8A8;

  // FIXME: can we use the SourceSurfaceDeallocator to unlock?
  return gfx::Factory::CreateWrappingDataSourceSurface(
      buf, mDesc.stride * gfx::BytesPerPixel(format),
      gfx::IntSize(mDesc.width, mDesc.height), format);
}

RefPtr<gfx::DrawTarget> HardwareBufferSurface::WriteLock() {
  MOZ_ASSERT(!mLocked);
  MOZ_ASSERT(!mIsConsumerAttached);
  MOZ_ASSERT((mDesc.usage & AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK) !=
             AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER);

  auto api = AndroidHardwareBufferApi::Get();
  unsigned char* buf;
  int32_t fence = mConsumerReleaseFence.take().valueOr(-1);
  int err = api->Lock(GetBuffer(), AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, fence,
                      nullptr, (void**)&buf);
  if (err) {
    gfxCriticalError() << "Failed to write lock AHardwareBuffer: " << err;
    return nullptr;
  }

  mLocked = true;

  // FIXME: handle other formats.
  gfx::SurfaceFormat format = gfx::SurfaceFormat::B8G8R8A8;
  return gfxPlatform::CreateDrawTargetForData(
      buf, gfx::IntSize(mDesc.width, mDesc.height),
      mDesc.stride * gfx::BytesPerPixel(format), format);
}

void HardwareBufferSurface::Unlock() {
  MOZ_ASSERT(mLocked);
  mLocked = false;

  auto api = AndroidHardwareBufferApi::Get();
  // FIXME: obtain fence and plumb through to transaction
  int err = api->Unlock(GetBuffer(), nullptr);
  if (err) {
    gfxCriticalError() << "Failed to unlock AHardwareBuffer: " << err;
    return;
  }
}

Maybe<GLuint> HardwareBufferSurface::GetFramebuffer(bool aNeedsDepthBuffer) {
  // printf_stderr("jamiedbg HardwareBufferSurface::GetFramebuffer() %p\n",
  // this);
  MOZ_RELEASE_ASSERT(mGL);
  if (!mGL->MakeCurrent()) {
    // printf_stderr("jamiedbg MakeCurrent failed\n");
    return Nothing();
  }

  if (mConsumerReleaseFence) {
    // printf_stderr("jamiedbg mConsumerReleaseFence is set\n");
    int32_t fence = *mConsumerReleaseFence.take();
    const EGLint attribs[] = {LOCAL_EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fence,
                              LOCAL_EGL_NONE};

    const auto& gle = gl::GLContextEGL::Cast(mGL);
    const auto& egl = gle->mEgl;

    EGLSync sync =
        egl->fCreateSync(LOCAL_EGL_SYNC_NATIVE_FENCE_ANDROID, attribs);
    if (sync) {
      // printf_stderr("jamiedbg eglCreateSync() success\n");
      if (egl->IsExtensionSupported(gl::EGLExtension::KHR_wait_sync)) {
        egl->fWaitSync(sync, 0);
      } else {
        egl->fClientWaitSync(sync, 0, LOCAL_EGL_FOREVER);
      }
      egl->fDestroySync(sync);
    } else {
      // printf_stderr("jamiedbg eglCreateSync() failed\n");
      // FIXME: If eglCreateSync fails, presumably EGL does _not_
      // take ownership of the fd, meaning we must manually close
      // it.
      close(fence);
    }
  } else {
    // printf_stderr("jamiedbg mConsumerReleaseFence is nothing\n");
  }

  if (mFramebuffer) {
    if (!aNeedsDepthBuffer || mFramebuffer->HasDepth()) {
      // printf_stderr("jamiedbg Reusing previous framebuffer\n");
      return Some(mFramebuffer->mFB);
    }
  }

  // FIXME: use shared depth buffer
  mFramebuffer = gl::MozFramebuffer::CreateForBacking(
      mGL, GetSize(), 0, true, mTextureTarget, mTexture->name);

  if (!mFramebuffer) {
    // printf_stderr("jamiedbg Failed to create framebuffer\n");
    return Nothing();
  }

  return Some(mFramebuffer->mFB);
}

void HardwareBufferSurface::UnlockFramebuffer() {
  // printf_stderr("jamiedbg HardwareBufferSurface::UnlockFramebuffer()\n");
  MOZ_ASSERT(mConsumerAcquireFence.isNothing());
  MOZ_ASSERT(mConsumerReleaseFence.isNothing());
  MOZ_ASSERT(!mIsConsumerAttached);
  MOZ_ASSERT(!mLocked);

  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;

  EGLSync sync = egl->fCreateSync(LOCAL_EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
  if (sync) {
    // printf_stderr("jamiedbg eglCreateSync succeeded\n");
    int fence = egl->fDupNativeFenceFDANDROID(sync);
    if (fence >= 0) {
      // printf_stderr("jamiedbg eglDupNativeFenceFDANDROID succeeded\n");
      mConsumerAcquireFence = Some(fence);
    } else {
      // printf_stderr("jamiedbg eglDupNativeFenceFDANDROID failed\n");
    }
    egl->fDestroySync(sync);
  } else {
    // printf_stderr("jamiedbg eglCreateSync failed\n");
  }
}

bool HardwareBufferSurface::IsConsumerAttached() const {
  return mIsConsumerAttached;
}

int32_t HardwareBufferSurface::SetConsumerAttached() {
  // printf_stderr("jamiedbg HardwareBufferSurface::SetConsumerAttached() %p\n",
  //               this);
  MOZ_ASSERT(!mLocked);
  MOZ_ASSERT(!mIsConsumerAttached);
  MOZ_ASSERT(mConsumerReleaseFence.isNothing());
  // MOZ_ASSERT(mConsumerAcquireFence.isNothing());
  mIsConsumerAttached = true;

  int32_t fence = mConsumerAcquireFence.take().valueOr(-1);
  return fence;
}

void HardwareBufferSurface::OnConsumerRelease(int32_t aFence) {
  // printf_stderr("jamiedbg HardwareBufferSurface::OnConsumerRelease() %p\n",
  //               this);
  mIsConsumerAttached = false;
  // FIXME: assert nothing, or close existing fence?
  MOZ_ASSERT(mConsumerReleaseFence.isNothing());
  // if (mConsumerReleaseFence) {
  //   // FIXME: confirm we need to do this
  //   close(*mConsumerReleaseFence);
  // }
  MOZ_ASSERT(mConsumerAcquireFence.isNothing());

  mConsumerReleaseFence = Some(aFence);
}

/* static */ RefPtr<SurfacePool> SurfacePool::Create(size_t aPoolSizeLimit) {
  return new SurfacePoolAndroid(aPoolSizeLimit);
}

SurfacePoolAndroid::SurfacePoolAndroid(size_t aPoolSizeLimit)
    : mMutex("SurfacePoolAndroid")
// ,
// mPoolSizeLimit(aPoolSizeLimit)
{}

SurfacePoolAndroid::~SurfacePoolAndroid() {}

RefPtr<SurfacePoolHandle> SurfacePoolAndroid::GetHandleForGL(GLContext* aGL) {
  return new SurfacePoolHandleAndroid(this, aGL);
}

void SurfacePoolAndroid::DestroyGLResourcesForContext(GLContext* aGL) {
  MutexAutoLock lock(mMutex);
}

UniquePtr<HardwareBufferSurface> SurfacePoolAndroid::ObtainBufferFromPool(
    const gfx::IntSize& aSize, gl::GLContext* aGL) {
  MutexAutoLock lock(mMutex);
  // printf_stderr("jamiedbg SurfacePoolAndroid::ObtainBufferFromPool()
  // gl=%p\n",
  //               aGL);

  UniquePtr<HardwareBufferSurface> buffer = nullptr;
  for (auto it = mAvailableEntries.begin(); it != mAvailableEntries.end();
       it++) {
    if ((*it)->GetSize() == aSize) {
      // printf_stderr("jamiedbg Found available buffer %p to recycle\n",
      //               it->get());
      std::iter_swap(it, mAvailableEntries.end() - 1);
      buffer = std::move(mAvailableEntries.back());
      mAvailableEntries.pop_back();
      break;
    }
  }

  if (!buffer) {
    // printf_stderr("jamiedbg No available buffer to recycle. Allocating
    // new.\n");
    buffer = HardwareBufferSurface::Create(aSize, aGL);
  }

  return buffer;
}

void SurfacePoolAndroid::ReturnBufferToPool(
    UniquePtr<HardwareBufferSurface> aBuffer) {
  MutexAutoLock lock(mMutex);
  if (aBuffer->IsConsumerAttached()) {
    // printf_stderr(
    //     "jamiedbg SurfacePoolAndroid::ReturnBufferToPool() %p pending\n",
    //     aBuffer.get());
    mPendingEntries.push_back(std::move(aBuffer));
  } else {
    // FIXME: if no longer attached but fence not signalled we should put in
    // pending entries.
    // printf_stderr(
    //     "jamiedbg SurfacePoolAndroid::ReturnBufferToPool() %p available\n",
    //     aBuffer.get());
    mAvailableEntries.push_back(std::move(aBuffer));
  }
  // FIXME: keep track of in use entries like we do on wayland?
}

UniquePtr<ASurfaceControl> SurfacePoolAndroid::ObtainSurfaceControl(
    ASurfaceControl* aParent) {
  if (mSurfaceControls.empty()) {
    auto api = AndroidSurfaceControlApi::Get();
    UniquePtr<ASurfaceControl> surfaceControl(
        api->ASurfaceControl_create(aParent, "NativeLayerAndroid"));
    return surfaceControl;
  }

  auto surfaceControl = std::move(mSurfaceControls.back());
  mSurfaceControls.pop_back();
  return surfaceControl;
}

void SurfacePoolAndroid::ReturnSurfaceControl(
    UniquePtr<ASurfaceControl> aSurfaceControl) {
  mSurfaceControls.push_back(std::move(aSurfaceControl));
}

void SurfacePoolAndroid::EnforcePoolSizeLimit() {
  MutexAutoLock lock(mMutex);
  // FIXME: enforce pool size limit
}

void SurfacePoolAndroid::CollectPendingSurfaces() {
  MutexAutoLock lock(mMutex);
  mPendingEntries.erase(
      std::remove_if(
          mPendingEntries.begin(), mPendingEntries.end(),
          [&](auto& entry) {
            if (!entry->IsConsumerAttached()) {
              // printf_stderr("jamiedbg Moving buffer %p to available pool\n",
              //               entry.get());
              // FIXME: If fence not signalled we shouldn't move to available
              // yet
              mAvailableEntries.push_back(std::move(entry));
              return true;
            }
            return false;
          }),
      mPendingEntries.end());
}

SurfacePoolHandleAndroid::SurfacePoolHandleAndroid(
    RefPtr<SurfacePoolAndroid> aPool, GLContext* aGL)
    : mPool(std::move(aPool)), mGL(aGL) {}

void SurfacePoolHandleAndroid::OnBeginFrame() {
  mPool->CollectPendingSurfaces();
}

void SurfacePoolHandleAndroid::OnEndFrame() {}

UniquePtr<HardwareBufferSurface> SurfacePoolHandleAndroid::ObtainBufferFromPool(
    const gfx::IntSize& aSize) {
  return mPool->ObtainBufferFromPool(aSize, mGL);
}

void SurfacePoolHandleAndroid::ReturnBufferToPool(
    UniquePtr<HardwareBufferSurface> aBuffer) {
  mPool->ReturnBufferToPool(std::move(aBuffer));
}

UniquePtr<ASurfaceControl> SurfacePoolHandleAndroid::ObtainSurfaceControl(
    ASurfaceControl* aParent) {
  return mPool->ObtainSurfaceControl(aParent);
}

void SurfacePoolHandleAndroid::ReturnSurfaceControl(
    UniquePtr<ASurfaceControl> aSurfaceControl) {
  mPool->ReturnSurfaceControl(std::move(aSurfaceControl));
}

}  // namespace mozilla::layers
