/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidImage.h"

#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Types.h"
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <media/NdkMediaError.h>

namespace mozilla::layers {

AndroidImage::AndroidImage(AImage* aImage, UniqueFileHandle&& aAcquireFenceFd,
                           const RefPtr<AndroidImageReader>& aImageReader)
    : mImage(aImage),
      mAcquireFenceFd(std::move(aAcquireFenceFd)),
      mSurfaceFormat(aImageReader->SurfaceFormat()),
      mImageReader(aImageReader) {}

AndroidImage::~AndroidImage() {
  const RefPtr<AndroidImageReader> reader(mImageReader);
  MOZ_ASSERT(reader, "Image should not outlive its ImageReader");
  if (reader) {
    reader->ReleaseImage(this);
  } else {
    AImage_delete(mImage);
  }
}

RefPtr<AndroidHardwareBuffer> AndroidImage::GetHardwareBuffer() {
  if (mHardwareBuffer) {
    return mHardwareBuffer;
  }

  AHardwareBuffer* buffer;
  media_status_t res = AImage_getHardwareBuffer(mImage, &buffer);
  if (res != AMEDIA_OK) {
    gfxCriticalNote << "AImage_getHardwareBuffer failed:" << gfx::hexa(res);
    return nullptr;
  }

  mHardwareBuffer = AndroidHardwareBuffer::FromNativeBuffer(
      buffer, mSurfaceFormat, Some(GetCropRect()));
  if (mHardwareBuffer) {
    mHardwareBuffer->SetAcquireFence(std::move(mAcquireFenceFd));
  }
  return mHardwareBuffer;
}

gfx::IntRect AndroidImage::GetCropRect() const {
  AImageCropRect cropRect;
  media_status_t res = AImage_getCropRect(mImage, &cropRect);
  MOZ_RELEASE_ASSERT(res == AMEDIA_OK);

  return gfx::IntRect(cropRect.left, cropRect.top,
                      cropRect.right - cropRect.left,
                      cropRect.bottom - cropRect.top);
}

int64_t AndroidImage::GetTimestamp() const {
  int64_t timestamp;
  media_status_t res = AImage_getTimestamp(mImage, &timestamp);
  MOZ_RELEASE_ASSERT(res == AMEDIA_OK);

  return timestamp;
}

AndroidImageReader::AndroidImageReader(AImageReader* aImageReader,
                                       gfx::SurfaceFormat aSurfaceFormat,
                                       int aMaxImages)
    : mImageReader(aImageReader),
      mMonitor("AndroidImageReader"),
      mSurfaceFormat(aSurfaceFormat),
      mMaxAcquiredImages(aMaxImages) {
  mListener.context = this;
  mListener.onImageAvailable = [](void* context, AImageReader* reader) {
    AndroidImageReader* self = (AndroidImageReader*)context;
    MOZ_ASSERT(self->mImageReader == reader);
    self->OnImageAvailable();
  };
  media_status_t res = AImageReader_setImageListener(mImageReader, &mListener);
  MOZ_RELEASE_ASSERT(res == AMEDIA_OK);
}

AndroidImageReader::~AndroidImageReader() {
  AImageReader_setImageListener(mImageReader, nullptr);
  AImageReader_delete(mImageReader);
}

/* static */ RefPtr<AndroidImageReader> AndroidImageReader::Create(
    gfx::SurfaceFormat aSurfaceFormat, int aWidth, int aHeight, int aFormat,
    int aMaxImages, int64_t aUsage) {
  // We require SDK level 26 for AImageReader_newWithUsage(),
  // AImage_getHardwareBuffer(), and all AHardwareBuffer APIs.
  if (jni::GetAPIVersion() < 26) {
    return nullptr;
  }

  AImageReader* imageReader = nullptr;
  media_status_t res = AImageReader_newWithUsage(
      aWidth, aHeight, aFormat, aUsage, aMaxImages, &imageReader);
  if (res != AMEDIA_OK) {
    gfxCriticalNote << "AImageReader_newWithUsage failed: " << gfx::hexa(res);
    return nullptr;
  }

  return new AndroidImageReader(imageReader, aSurfaceFormat, aMaxImages);
}

java::sdk::Surface::LocalRef AndroidImageReader::GetSurface() {
  // ANativeWindow_toSurface() requires API level 26.
  if (jni::GetAPIVersion() < 26) {
    return nullptr;
  }

  ANativeWindow* window = nullptr;
  AImageReader_getWindow(mImageReader, &window);
  jobject surface = ANativeWindow_toSurface(jni::GetEnvForThread(), window);
  // No need to release window as AImageReader_getWindow() does not acquire a
  // reference. The java object will acquire its own reference, which will be
  // released when the java object is destroyed.
  return java::sdk::Surface::Ref::From(surface);
}

RefPtr<AndroidImage> AndroidImageReader::AcquireLatestImage() {
  MonitorAutoLock lock(mMonitor);
  MOZ_DIAGNOSTIC_ASSERT(mAcquiredImages < mMaxAcquiredImages);

  while (mPendingImages <= 0 || mAcquiredImages >= mMaxAcquiredImages) {
    const CVStatus status = lock.Wait(TimeDuration::FromSeconds(10));
    if (status == CVStatus::Timeout) {
      gfxCriticalError() << "Timeout in AcquireNextImage(): " << mPendingImages
                         << " pending, " << mAcquiredImages << " acquired";
      MOZ_DIAGNOSTIC_ASSERT(false);
    }
  }

  AImage* image = nullptr;
  int acquireFenceFd = -1;
  media_status_t res = AImageReader_acquireLatestImageAsync(
      mImageReader, &image, &acquireFenceFd);
  if (res != AMEDIA_OK) {
    gfxCriticalNote << "AImageReader_acquireNextImage failed:"
                    << gfx::hexa(res);
    return nullptr;
  }
  mPendingImages = 0;
  mAcquiredImages++;
  
  printf_stderr("jamiedbg Acquired image. %d acquired", mAcquiredImages);

  return new AndroidImage(image, UniqueFileHandle(acquireFenceFd), this);
}

void AndroidImageReader::OnImageAvailable() {
  MonitorAutoLock lock(mMonitor);

  if (++mPendingImages == 1) {
    lock.NotifyAll();
  }
}

void AndroidImageReader::ReleaseImage(AndroidImage* aImage) {
  MonitorAutoLock lock(mMonitor);

  MOZ_RELEASE_ASSERT(aImage->mHardwareBuffer->hasOneRef());
  UniqueFileHandle releaseFence =
      aImage->mHardwareBuffer->GetAndResetReleaseFence();
  aImage->mHardwareBuffer = nullptr;
  AImage_deleteAsync(aImage->mImage, releaseFence.release());

  aImage->mImage = nullptr;
  if (--mAcquiredImages < mMaxAcquiredImages) {
    lock.NotifyAll();
  }
  
  printf_stderr("jamiedbg Released image. %d acquired", mAcquiredImages);
}

}  // namespace mozilla::layers
