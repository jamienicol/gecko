/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERANDROIDIMAGEREADERTEXTUREHOST_H
#define MOZILLA_GFX_RENDERANDROIDIMAGEREADERTEXTUREHOST_H

#include "mozilla/layers/AndroidImage.h"
#include "mozilla/layers/TextureHostOGL.h"
#include "RenderTextureHostSWGL.h"

namespace mozilla {

namespace gfx {
class DataSourceSurface;
}

namespace wr {

class RenderAndroidImageReaderTextureHost final : public RenderTextureHostSWGL {
 public:
  explicit RenderAndroidImageReaderTextureHost(
      RefPtr<layers::AndroidImageReader> aImageReader, int64_t aTimestamp,
      gfx::IntSize aSize, gfx::SurfaceFormat aFormat);

  wr::WrExternalImage Lock(uint8_t aChannelIndex, gl::GLContext* aGL) override;
  void Unlock() override;

  size_t Bytes() override {
    return mSize.width * mSize.height * BytesPerPixel(mFormat);
  }

  void PrepareForUse() override;
  void NotifyForUse() override;
  void NotifyNotUsed() override;

  // RenderTextureHostSWGL
  gfx::SurfaceFormat GetFormat() const override;
  gfx::ColorDepth GetColorDepth() const override {
    return gfx::ColorDepth::COLOR_8;
  }
  size_t GetPlaneCount() const override { return 1; }
  bool MapPlane(RenderCompositor* aCompositor, uint8_t aChannelIndex,
                PlaneInfo& aPlaneInfo) override;
  void UnmapPlanes() override;

  RenderAndroidImageReaderTextureHost* AsRenderAndroidImageReaderTextureHost()
      override {
    return this;
  }

  gfx::IntSize GetSize() const { return mSize; }

  RefPtr<layers::AndroidImage> GetImage();
  AHardwareBuffer* GetHardwareBuffer();
  Maybe<GLuint> GetTexture();

 private:
  virtual ~RenderAndroidImageReaderTextureHost();

  already_AddRefed<gfx::DataSourceSurface> ReadTexImage();

  const RefPtr<layers::AndroidImageReader> mImageReader;
  const int64_t mTimestamp;
  const gfx::IntSize mSize;
  const gfx::SurfaceFormat mFormat;

  RefPtr<gl::GLContext> mGL;
  RefPtr<layers::AndroidImage> mCurrentImage;
  // FIXME: refptr/uniqueptr?
  AHardwareBuffer* mHardwareBuffer = nullptr;
  Maybe<GLuint> mTexture;

  RefPtr<gfx::DataSourceSurface> mReadback;
};

}  // namespace wr
}  // namespace mozilla

#endif  // MOZILLA_GFX_RENDERANDROIDIMAGEREADERTEXTUREHOST_H
