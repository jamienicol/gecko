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
    RefPtr<layers::AndroidImageReader> aImageReader, int64_t aTimestamp,
    gfx::IntSize aSize, gfx::SurfaceFormat aFormat)
    : mImageReader(std::move(aImageReader)),
      mTimestamp(aTimestamp),
      mSize(aSize),
      mFormat(aFormat) {
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
  if (mEglImage != EGL_NO_IMAGE) {
    const auto& gle = gl::GLContextEGL::Cast(mGL);
    const auto& egl = gle->mEgl;
    egl->fDestroyImage(mEglImage);
  }
  if (mHardwareBuffer) {
    // Hardware buffer is owned by the image, we have nothing to clean up
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

RefPtr<layers::TextureSource>
RenderAndroidImageReaderTextureHost::CreateTextureSource(
    layers::TextureSourceProvider* aProvider) {
  gl::GLContext* const gl = aProvider->GetGLContext();
  if (mGL.get() != gl) {
    if (mGL) {
      // This should not happen. On android, SingletonGL is used.
      MOZ_ASSERT_UNREACHABLE("Unexpected GL context");
      return nullptr;
    }
    mGL = gl;
  }

  GetEglImage();
  if (mEglImage == EGL_NO_IMAGE) {
    return nullptr;
  }

  return new layers::EGLImageTextureSource(aProvider, mEglImage, mFormat,
                                           LOCAL_GL_TEXTURE_EXTERNAL,
                                           LOCAL_GL_CLAMP_TO_EDGE, mSize);
}

RefPtr<layers::AndroidImage> RenderAndroidImageReaderTextureHost::GetImage() {
  if (mCurrentImage) {
    return mCurrentImage;
  }

  if (!mImageReader) {
    return nullptr;
  }

  mCurrentImage = mImageReader->GetCurrentImage();
  if (!mCurrentImage || mCurrentImage->GetTimestamp() != mTimestamp) {
    mCurrentImage = mImageReader->AcquireLatestImage();
  }

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
  return mHardwareBuffer;
}

EGLImage RenderAndroidImageReaderTextureHost::GetEglImage() {
  if (mEglImage != EGL_NO_IMAGE) {
    return mEglImage;
  }

  GetHardwareBuffer();
  if (!mHardwareBuffer) {
    return EGL_NO_IMAGE;
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
    gfxCriticalNote << "eglGetNativeClientBufferANDROID failed: "
                    << gfx::hexa(egl->mLib->fGetError());
    return EGL_NO_IMAGE;
  }
  mEglImage = egl->fCreateImage(EGL_NO_CONTEXT, LOCAL_EGL_NATIVE_BUFFER_ANDROID,
                                clientBuffer, attrs);
  if (mEglImage == EGL_NO_IMAGE) {
    gfxCriticalNote << "eglCreateImage failed: "
                    << gfx::hexa(egl->mLib->fGetError());
  }

  return mEglImage;
}

Maybe<GLuint> RenderAndroidImageReaderTextureHost::GetTexture() {
  if (mTexture) {
    return mTexture;
  }

  GetEglImage();
  if (mEglImage == EGL_NO_IMAGE) {
    return Nothing();
  }

  MOZ_ASSERT(mGL);
  // FIXME: use helper to gen texture?
  GLuint tex;
  mGL->fGenTextures(1, &tex);
  ActivateBindAndTexParameteri(mGL, LOCAL_GL_TEXTURE0,
                               LOCAL_GL_TEXTURE_EXTERNAL, tex);
  mGL->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_T,
                      LOCAL_GL_CLAMP_TO_EDGE);
  mGL->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_S,
                      LOCAL_GL_CLAMP_TO_EDGE);
  mGL->fEGLImageTargetTexture2D(LOCAL_GL_TEXTURE_EXTERNAL, mEglImage);

  mTexture.emplace(tex);
  return mTexture;
}

already_AddRefed<gfx::DataSourceSurface>
RenderAndroidImageReaderTextureHost::ReadTexImage() {
  if (!mGL) {
    mGL = RenderThread::Get()->SingletonGL();
    if (!mGL) {
      return nullptr;
    }
  }

  GetTexture();
  if (!mTexture) {
    return nullptr;
  }

  // Allocate resulting image surface.
  // Use GetFormat() rather than mFormat for the DataSourceSurface. eg BGRA
  // rather than RGBA, as the latter is not supported by swgl.
  // ReadTexImageHelper will take care of converting the data for us.
  int32_t stride = mSize.width * BytesPerPixel(GetFormat());
  RefPtr<gfx::DataSourceSurface> surf =
      gfx::Factory::CreateDataSourceSurfaceWithStride(mSize, GetFormat(),
                                                      stride);
  if (!surf) {
    return nullptr;
  }

  layers::ShaderConfigOGL config = layers::ShaderConfigFromTargetAndFormat(
      LOCAL_GL_TEXTURE_EXTERNAL, mFormat);
  int shaderConfig = config.mFeatures;

  bool ret = mGL->ReadTexImageHelper()->ReadTexImage(
      surf, *mTexture, LOCAL_GL_TEXTURE_EXTERNAL, mSize, shaderConfig,
      /* aYInvert */ false);
  if (!ret) {
    return nullptr;
  }

  return surf.forget();
}

}  // namespace wr
}  // namespace mozilla
