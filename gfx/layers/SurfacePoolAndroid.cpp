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
#include <cstring>
#include <iterator>
#include <poll.h>
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
    printf_stderr("jamiedbg HardwareBufferSurface::Create()\n");
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
  desc.usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
               AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;

  // FIXME: make format configurable?
  desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;

  AHardwareBuffer* buffer;
  api->Allocate(&desc, &buffer);

  if (!buffer) {
    // printf_stderr("jamiedbg Failed to create AndroidHardwareBuffer\n");
    return nullptr;
  }

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
  auto api = AndroidHardwareBufferApi::Get();
  api->Release(mBuffer);
}

Maybe<GLuint> HardwareBufferSurface::GetFramebuffer(bool aNeedsDepthBuffer) {
  // printf_stderr("jamiedbg HardwareBufferSurface::GetFramebuffer() %p\n",
  // this);
  MOZ_RELEASE_ASSERT(mGL);
  if (!mGL->MakeCurrent()) {
    // printf_stderr("jamiedbg MakeCurrent failed\n");
    return Nothing();
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

bool HardwareBufferSurface::IsConsumerAttached() {
    printf_stderr("jamiedbg HardwareBufferSurface::IsConsumerAttached() %p\n", this);
  if (mIsConsumerAttached) {
      printf_stderr("jamiedbg mIsConsumerAttached == true. ATTACHED\n");
    return true;
  }

  if (mConsumerReleaseFence == -1) {
      printf_stderr("jamiedbg mConsumerReleaseFence == -1. NOT ATTACHED\n");
    return false;
  }

  pollfd p;
  p.fd = mConsumerReleaseFence;
  p.events = POLLIN;
  int ret = ::poll(&p, 1, 0);
  MOZ_ASSERT(ret >= 0, "Error polling release fence");
  if (ret == 0) {
      printf_stderr("jamiedbg Fence not yet signalled. ATTACHED\n");
    // Fence not yet signalled
    return true;
  }

  close(mConsumerReleaseFence);
  printf_stderr("jamedbg Fence signalled, setting to -1. NOT ATTACHED");
  mConsumerReleaseFence = -1;
  return false;
}

bool HardwareBufferSurface::IsConsumerReleasedOrPendingRelease() const {
  return !mIsConsumerAttached;
}

void HardwareBufferSurface::OnConsumerAttach() {
    printf_stderr("jamiedbg OnConsumerAttach() %p\n", this);
    printf_stderr("jamiedbg mIsConsumerAttached: %d, mConsumerReleaseFence: %d\n", mIsConsumerAttached, mConsumerReleaseFence);
  MOZ_RELEASE_ASSERT(!mIsConsumerAttached && mConsumerReleaseFence == -1);
  mIsConsumerAttached = true;
}

void HardwareBufferSurface::OnConsumerRelease(int aFence) {
    printf_stderr("jamiedbg OnConsumerRelease() %p fence: %d\n", this, aFence);
    printf_stderr("jamiedbg mIsConsumerAttached: %d, mConsumerReleaseFence: %d\n", mIsConsumerAttached, mConsumerReleaseFence);
  MOZ_RELEASE_ASSERT(mIsConsumerAttached && mConsumerReleaseFence == -1);
  mIsConsumerAttached = false;
  mConsumerReleaseFence = aFence;
}

/* static */ RefPtr<SurfacePool> SurfacePool::Create(size_t aPoolSizeLimit) {
  return new SurfacePoolAndroid(aPoolSizeLimit);
}

SurfacePoolAndroid::SurfacePoolAndroid(size_t aPoolSizeLimit)
    : mMutex("SurfacePoolAndroid"), mPoolSizeLimit(aPoolSizeLimit) {}

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
  printf_stderr("jamiedbg SurfacePoolAndroid::ObtainBufferFromPool() %s\n", mozilla::ToString(aSize).c_str());

  UniquePtr<HardwareBufferSurface> buffer = nullptr;
  for (auto it = mAvailableEntries.begin(); it != mAvailableEntries.end();
       it++) {
    if ((*it)->GetSize() == aSize) {
      printf_stderr("jamiedbg Found available buffer %p to recycle\n",
                    it->get());
      std::iter_swap(it, mAvailableEntries.end() - 1);
      buffer = std::move(mAvailableEntries.back());
      mAvailableEntries.pop_back();
      break;
    }
  }

  if (!buffer) {
    printf_stderr("jamiedbg No available buffer to recycle. Allocating new.\n");
    buffer = HardwareBufferSurface::Create(aSize, aGL);
  }

  return buffer;
}

void SurfacePoolAndroid::ReturnBufferToPool(
    UniquePtr<HardwareBufferSurface> aBuffer) {
    printf_stderr("jamiedbg ReturnBufferToPool() %p\n", aBuffer.get());
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(aBuffer->IsConsumerReleasedOrPendingRelease());
  if (aBuffer->IsConsumerAttached()) {
    printf_stderr("jamiedbg adding to pending entries\n");
    mPendingEntries.push_back(std::move(aBuffer));
  } else {
    printf_stderr("jamiedbg adding to available entries\n");
    mAvailableEntries.push_back(std::move(aBuffer));
  }
  // FIXME: keep track of in use entries like we do on wayland?
}

UniquePtr<ASurfaceControl> SurfacePoolAndroid::ObtainSurfaceControl(
    ASurfaceControl* aParent) {
  if (mAvailableSurfaceControls.empty()) {
    auto api = AndroidSurfaceControlApi::Get();
    UniquePtr<ASurfaceControl> surfaceControl(
        api->ASurfaceControl_create(aParent, "NativeLayerAndroid"));
    return surfaceControl;
  }

  auto surfaceControl = std::move(mAvailableSurfaceControls.back());
  mAvailableSurfaceControls.pop_back();
  return surfaceControl;
}

void SurfacePoolAndroid::ReturnSurfaceControl(
    UniquePtr<ASurfaceControl> aSurfaceControl) {
  mPendingSurfaceControls.push_back(std::move(aSurfaceControl));
}

void SurfacePoolAndroid::EnforcePoolSizeLimit() {
  MutexAutoLock lock(mMutex);
  // FIXME: enforce pool size limit.
  // Do we include pending?
  if (mAvailableEntries.size() > mPoolSizeLimit) {
  }
}

void SurfacePoolAndroid::CollectPendingSurfaces() {
  printf_stderr("jamiedbg CollectPendingSurfaces()\n");
  MutexAutoLock lock(mMutex);
  mPendingEntries.erase(
      std::remove_if(mPendingEntries.begin(), mPendingEntries.end(),
                     [&](auto& entry) {
                       if (!entry->IsConsumerAttached()) {
                         printf_stderr(
                             "jamiedbg Moving buffer %p to available pool\n",
                             entry.get());
                         mAvailableEntries.push_back(std::move(entry));
                         return true;
                       }
                       return false;
                     }),
      mPendingEntries.end());

  mAvailableSurfaceControls.insert(
      mAvailableSurfaceControls.end(),
      std::make_move_iterator(mPendingSurfaceControls.begin()),
      std::make_move_iterator(mPendingSurfaceControls.end()));
  mPendingSurfaceControls.clear();
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
