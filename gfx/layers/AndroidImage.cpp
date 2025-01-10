/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidImage.h"
#include "mozilla/gfx/Logging.h"

#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

namespace mozilla::layers {

AndroidImage::AndroidImage(AImage* aImage,
                           const RefPtr<AndroidImageReader>& aImageReader)
    : mImage(aImage), mImageReader(aImageReader) {}

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

  // FIXME: don't hard code this format
  mHardwareBuffer = AndroidHardwareBuffer::FromNativeBuffer(
      buffer, gfx::SurfaceFormat::R8G8B8X8, Some(GetCropRect()));
  return mHardwareBuffer;
}

gfx::IntRect AndroidImage::GetCropRect() const {
  const auto* api = layers::AndroidImageApi::Get();

  AImageCropRect cropRect;
  media_status_t res = api->AImage_getCropRect(mImage, &cropRect);
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
                                       int aMaxImages)
    : mImageReader(aImageReader),
      mMonitor("AndroidImageReader"),
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
    int aWidth, int aHeight, int aFormat, int aMaxImages, int64_t aUsage) {
  AImageReader* imageReader = nullptr;
  media_status_t res = AImageReader_newWithUsage(
      aWidth, aHeight, aFormat, aUsage, aMaxImages, &imageReader);
  if (res != AMEDIA_OK) {
    gfxCriticalNote << "AImageReader_newWithUsage failed: " << gfx::hexa(res);
    return nullptr;
  }

  return new AndroidImageReader(imageReader, aMaxImages);
}

java::sdk::Surface::LocalRef AndroidImageReader::GetSurface() {
  ANativeWindow* window = nullptr;
  AImageReader_getWindow(mImageReader, &window);
  jobject surface = ANativeWindow_toSurface(jni::GetEnvForThread(), window);
  // No need to release window as AImageReader_getWindow does not acquire a
  // reference. The java object will acquire its own reference, which will be
  // released when the java object is destroyed.
  return java::sdk::Surface::Ref::From(surface);
}

// FIXME: switch to using latest. If we have too many acquired
// images then just return null instead of waiting?
RefPtr<AndroidImage> AndroidImageReader::AcquireNextImage() {
  MonitorAutoLock lock(mMonitor);

  while (mPendingImages <= 0 || mAcquiredImages >= mMaxAcquiredImages) {
    const CVStatus status = lock.Wait(TimeDuration::FromSeconds(10));
    if (status == CVStatus::Timeout) {
      gfxCriticalError() << "Timeout in AcquireNextImage(): " << mPendingImages
                         << " pending, " << mAcquiredImages << " acquired";
      MOZ_DIAGNOSTIC_ASSERT(false);
    }
  }

  AImage* image;
  media_status_t res = AImageReader_acquireNextImage(mImageReader, &image);
  if (res != AMEDIA_OK) {
    gfxCriticalNote << "AImageReader_acquireNextImage failed:"
                    << gfx::hexa(res);
    return nullptr;
  }
  mPendingImages--;
  mAcquiredImages++;

  return new AndroidImage(image, this);
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
  ipc::FileDescriptor releaseFence =
      aImage->mHardwareBuffer->GetAndResetReleaseFence();
  aImage->mHardwareBuffer = nullptr;

  if (releaseFence.IsValid()) {
    AImage_deleteAsync(aImage->mImage,
                       releaseFence.TakePlatformHandle().release());
  } else {
    AImage_delete(aImage->mImage);
  }
  aImage->mImage = nullptr;
  if (--mAcquiredImages < mMaxAcquiredImages) {
    lock.NotifyAll();
  }
}

}  // namespace mozilla::layers
