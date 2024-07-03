/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderAndroidImageReaderTextureHost.h"

#include "GLReadTexImageHelper.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/webrender/RenderThread.h"
#include "GLContext.h"

namespace mozilla {
namespace wr {

RenderAndroidImageReaderTextureHost::RenderAndroidImageReaderTextureHost(
    RefPtr<layers::AndroidImageReader> aImageReader, gfx::IntSize aSize,
    gfx::SurfaceFormat aFormat)
    : mImageReader(std::move(aImageReader)), mSize(aSize), mFormat(aFormat) {
  MOZ_COUNT_CTOR_INHERITED(RenderAndroidImageReaderTextureHost,
                           RenderTextureHost);
}

RenderAndroidImageReaderTextureHost::~RenderAndroidImageReaderTextureHost() {
  MOZ_ASSERT(RenderThread::IsInRenderThread());
  MOZ_COUNT_DTOR_INHERITED(RenderAndroidImageReaderTextureHost,
                           RenderTextureHost);
  MOZ_ASSERT(!mCurrentImage);
  MOZ_ASSERT(!mHardwareBuffer);
  MOZ_ASSERT(mTexture.isNothing());
}

RefPtr<layers::AndroidImage> RenderAndroidImageReaderTextureHost::GetImage() {
  if (mCurrentImage) {
    return mCurrentImage;
  }

  const auto* api = layers::AndroidImageApi::Get();
  AImage* image;
  // FIXME: use async version and then return the fence or poll() it?
  // FIXME: chrome uses either acquireNextImage or acquireLatestImage in certain
  // cases. why?
  media_status_t res =
      api->AImageReader_acquireNextImage(mImageReader->mImageReader, &image);
  if (res != AMEDIA_OK) {
    printf_stderr("jamiedbg AImageReader_acquireNextImage failed: %d\n", res);
    return nullptr;
  }

  mCurrentImage = new layers::AndroidImage(image);
  return mCurrentImage;
}

AHardwareBuffer* RenderAndroidImageReaderTextureHost::GetHardwareBuffer() {
  if (mHardwareBuffer) {
    return mHardwareBuffer;
  }

  if (!GetImage()) {
    return nullptr;
  }

  mHardwareBuffer = mCurrentImage->GetHardwareBuffer();

  // FIXME: return some sort of fence as well? or is that handled by
  // AcquireImage()?
  return mHardwareBuffer;
}

Maybe<GLuint> RenderAndroidImageReaderTextureHost::GetTexture() {
  if (mTexture) {
    return mTexture;
  }

  GetHardwareBuffer();
  if (!mHardwareBuffer) {
    return Nothing();
  }

  MOZ_ASSERT(mGL);
  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;

  // FIXME: Chrome sets EGL_IMAGE_PRESERVED false. I don't know if it makes a
  // difference.
  const EGLint attrs[] = {
      LOCAL_EGL_IMAGE_PRESERVED,
      LOCAL_EGL_FALSE,
      LOCAL_EGL_NONE,
  };

  EGLClientBuffer clientBuffer =
      egl->mLib->fGetNativeClientBufferANDROID(mHardwareBuffer);
  if (clientBuffer == nullptr) {
    printf_stderr("jamiedbg eglGetNativeClientBufferANDROID failed: 0x%x\n",
                  egl->mLib->fGetError());
    return Nothing();
  }
  EGLImage eglImage = egl->fCreateImage(
      EGL_NO_CONTEXT, LOCAL_EGL_NATIVE_BUFFER_ANDROID, clientBuffer, attrs);
  if (eglImage == EGL_NO_IMAGE) {
    printf_stderr("jamiedbg eglCreateImage failed: 0x%x\n",
                  egl->mLib->fGetError());
  }

  // FIXME: use helper to gen texture?
  GLuint tex;
  mGL->fGenTextures(1, &tex);
  mGL->fBindTexture(LOCAL_GL_TEXTURE_EXTERNAL, tex);
  mGL->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_T,
                      LOCAL_GL_CLAMP_TO_EDGE);
  mGL->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_S,
                      LOCAL_GL_CLAMP_TO_EDGE);
  mGL->fEGLImageTargetTexture2D(LOCAL_GL_TEXTURE_EXTERNAL, eglImage);

  ActivateBindAndTexParameteri(mGL, LOCAL_GL_TEXTURE0,
                               LOCAL_GL_TEXTURE_EXTERNAL_OES, tex);
  egl->fDestroyImage(eglImage);

  mTexture.emplace(tex);
  return mTexture;
}

wr::WrExternalImage RenderAndroidImageReaderTextureHost::Lock(
    uint8_t aChannelIndex, gl::GLContext* aGL) {
  MOZ_ASSERT(aChannelIndex == 0);
  MOZ_ASSERT_IF(mGL, mGL.get() == aGL);
  if (!mGL) {
    mGL = aGL;
  }

  GetTexture();
  if (!mTexture) {
    return wr::InvalidToWrExternalImage();
  }

  const auto uvs = GetUvCoords(mSize);
  return NativeTextureToWrExternalImage(*mTexture, uvs.first.x, uvs.first.y,
                                        uvs.second.x, uvs.second.y);
}

void RenderAndroidImageReaderTextureHost::Unlock() {}

void RenderAndroidImageReaderTextureHost::PrepareForUse() {
  MOZ_ASSERT(RenderThread::IsInRenderThread());
}

void RenderAndroidImageReaderTextureHost::NotifyForUse() {
  MOZ_ASSERT(RenderThread::IsInRenderThread());
}

void RenderAndroidImageReaderTextureHost::NotifyNotUsed() {
  MOZ_ASSERT(RenderThread::IsInRenderThread());
  if (mTexture) {
    mGL->fDeleteTextures(1, &*mTexture);
    mTexture.reset();
  }
  if (mHardwareBuffer) {
    // Don't need to release any reference as we didn't manually acquire one
    // FIXME: verify this is true. or do we want to take a reference?
    // What happens if the image is deleted and we attempt to use it's buffer?
    // or if we have manually acquired the buffer and attempt to delete the
    // image?
    mHardwareBuffer = nullptr;
  }
  mCurrentImage = nullptr;
}

gfx::SurfaceFormat RenderAndroidImageReaderTextureHost::GetFormat() const {
  MOZ_ASSERT(mFormat == gfx::SurfaceFormat::R8G8B8A8 ||
             mFormat == gfx::SurfaceFormat::R8G8B8X8);

  if (mFormat == gfx::SurfaceFormat::R8G8B8A8) {
    return gfx::SurfaceFormat::B8G8R8A8;
  }

  if (mFormat == gfx::SurfaceFormat::R8G8B8X8) {
    return gfx::SurfaceFormat::B8G8R8X8;
  }

  gfxCriticalNoteOnce
      << "Unexpected color format of RenderAndroidImageReaderTextureHost";

  return gfx::SurfaceFormat::UNKNOWN;
}

already_AddRefed<gfx::DataSourceSurface>
RenderAndroidImageReaderTextureHost::ReadTexImage() {
  // FIXME: implement
  return nullptr;
  // if (!mGL) {
  //   mGL = RenderThread::Get()->SingletonGL();
  //   if (!mGL) {
  //     return nullptr;
  //   }
  // }

  // /* Allocate resulting image surface */
  // int32_t stride = mSize.width * BytesPerPixel(GetFormat());
  // RefPtr<gfx::DataSourceSurface> surf =
  //     gfx::Factory::CreateDataSourceSurfaceWithStride(mSize, GetFormat(),
  //                                                     stride);
  // if (!surf) {
  //   return nullptr;
  // }

  // layers::ShaderConfigOGL config = layers::ShaderConfigFromTargetAndFormat(
  //     LOCAL_GL_TEXTURE_EXTERNAL, mFormat);
  // int shaderConfig = config.mFeatures;

  // bool ret = mGL->ReadTexImageHelper()->ReadTexImage(
  //     surf, mSurfTex->GetTexName(), LOCAL_GL_TEXTURE_EXTERNAL, mSize,
  //     shaderConfig, /* aYInvert */ false);
  // if (!ret) {
  //   return nullptr;
  // }

  // return surf.forget();
}

bool RenderAndroidImageReaderTextureHost::MapPlane(
    RenderCompositor* aCompositor, uint8_t aChannelIndex,
    PlaneInfo& aPlaneInfo) {
  RefPtr<gfx::DataSourceSurface> readback = ReadTexImage();
  if (!readback) {
    return false;
  }

  gfx::DataSourceSurface::MappedSurface map;
  if (!readback->Map(gfx::DataSourceSurface::MapType::READ, &map)) {
    return false;
  }

  mReadback = readback;
  aPlaneInfo.mSize = mSize;
  aPlaneInfo.mStride = map.mStride;
  aPlaneInfo.mData = map.mData;
  return true;
}

void RenderAndroidImageReaderTextureHost::UnmapPlanes() {
  if (mReadback) {
    mReadback->Unmap();
    mReadback = nullptr;
  }
}

}  // namespace wr
}  // namespace mozilla
