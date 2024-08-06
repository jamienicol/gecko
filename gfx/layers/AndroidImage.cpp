/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidImage.h"
#include "mozilla/gfx/Logging.h"

#include <dlfcn.h>

namespace mozilla::layers {

StaticAutoPtr<AndroidImageApi> AndroidImageApi::sInstance;

/* static */ void AndroidImageApi::Init() {
  sInstance = new AndroidImageApi();
  if (!sInstance->Load()) {
    sInstance = nullptr;
  }
}

/* static */ const AndroidImageApi* AndroidImageApi::Get() { return sInstance; }

#define LOAD_FN(handle, fun)                   \
  m##fun = (_##fun)dlsym(handle, #fun);        \
  if (!m##fun) {                               \
    gfxCriticalNote << "Failed to load " #fun; \
    return false;                              \
  }

bool AndroidImageApi::Load() {
  void* const libMediaNdk = dlopen("libmediandk.so", RTLD_LAZY | RTLD_LOCAL);
  MOZ_ASSERT(libMediaNdk);
  if (!libMediaNdk) {
    gfxCriticalNote << "Failed to load libmediandk.so";
    return false;
  }
  void* const libAndroid = dlopen("libandroid.so", RTLD_LAZY | RTLD_LOCAL);
  MOZ_ASSERT(libAndroid);
  if (!libAndroid) {
    gfxCriticalNote << "Failed to load libandroid.so";
    return false;
  }

  const int sdkLevel = jni::GetAPIVersion();
  if (sdkLevel >= 24) {
    LOAD_FN(libMediaNdk, AImageReader_new);
    LOAD_FN(libMediaNdk, AImageReader_delete);
    LOAD_FN(libMediaNdk, AImageReader_getWindow);
    LOAD_FN(libMediaNdk, AImageReader_getWidth);
    LOAD_FN(libMediaNdk, AImageReader_getHeight);
    LOAD_FN(libMediaNdk, AImageReader_getFormat);
    LOAD_FN(libMediaNdk, AImageReader_getMaxImages);
    LOAD_FN(libMediaNdk, AImageReader_acquireNextImage);
    LOAD_FN(libMediaNdk, AImageReader_acquireLatestImage);
    LOAD_FN(libMediaNdk, AImageReader_setImageListener);
    LOAD_FN(libMediaNdk, AImage_delete);
    LOAD_FN(libMediaNdk, AImage_getWidth);
    LOAD_FN(libMediaNdk, AImage_getHeight);
    LOAD_FN(libMediaNdk, AImage_getFormat);
    LOAD_FN(libMediaNdk, AImage_getCropRect);
    LOAD_FN(libMediaNdk, AImage_getTimestamp);
    LOAD_FN(libMediaNdk, AImage_getNumberOfPlanes);
    LOAD_FN(libMediaNdk, AImage_getPlanePixelStride);
    LOAD_FN(libMediaNdk, AImage_getPlaneRowStride);
    LOAD_FN(libMediaNdk, AImage_getPlaneData);
  }
  if (sdkLevel >= 26) {
    LOAD_FN(libMediaNdk, AImageReader_newWithUsage);
    LOAD_FN(libMediaNdk, AImageReader_acquireNextImageAsync);
    LOAD_FN(libMediaNdk, AImageReader_acquireLatestImageAsync);
    LOAD_FN(libMediaNdk, AImageReader_setBufferRemovedListener);
    LOAD_FN(libMediaNdk, AImage_deleteAsync);
    LOAD_FN(libMediaNdk, AImage_getHardwareBuffer);
    LOAD_FN(libAndroid, ANativeWindow_toSurface);
  }

  return true;
}

#undef LOAD_FN

media_status_t AndroidImageApi::AImageReader_new(int32_t width, int32_t height,
                                                 int32_t format,
                                                 int32_t maxImages,
                                                 AImageReader** reader) const {
  MOZ_RELEASE_ASSERT(mAImageReader_new);
  return mAImageReader_new(width, height, format, maxImages, reader);
}

void AndroidImageApi::AImageReader_delete(AImageReader* reader) const {
  MOZ_RELEASE_ASSERT(mAImageReader_delete);
  mAImageReader_delete(reader);
}

media_status_t AndroidImageApi::AImageReader_getWindow(
    AImageReader* reader, ANativeWindow** window) const {
  MOZ_RELEASE_ASSERT(mAImageReader_getWindow);
  return mAImageReader_getWindow(reader, window);
}

media_status_t AndroidImageApi::AImageReader_getWidth(
    const AImageReader* reader, int32_t* width) const {
  MOZ_RELEASE_ASSERT(mAImageReader_getWidth);
  return mAImageReader_getWidth(reader, width);
}

media_status_t AndroidImageApi::AImageReader_getHeight(
    const AImageReader* reader, int32_t* height) const {
  MOZ_RELEASE_ASSERT(mAImageReader_getHeight);
  return mAImageReader_getHeight(reader, height);
}

media_status_t AndroidImageApi::AImageReader_getFormat(
    const AImageReader* reader, int32_t* format) const {
  MOZ_RELEASE_ASSERT(mAImageReader_getFormat);
  return mAImageReader_getFormat(reader, format);
}

media_status_t AndroidImageApi::AImageReader_getMaxImages(
    const AImageReader* reader, int32_t* maxImages) const {
  MOZ_RELEASE_ASSERT(mAImageReader_getMaxImages);
  return mAImageReader_getMaxImages(reader, maxImages);
}

media_status_t AndroidImageApi::AImageReader_acquireNextImage(
    AImageReader* reader, AImage** image) const {
  MOZ_RELEASE_ASSERT(mAImageReader_acquireNextImage);
  return mAImageReader_acquireNextImage(reader, image);
}

media_status_t AndroidImageApi::AImageReader_acquireLatestImage(
    AImageReader* reader, AImage** image) const {
  MOZ_RELEASE_ASSERT(mAImageReader_acquireLatestImage);
  return mAImageReader_acquireLatestImage(reader, image);
}

media_status_t AndroidImageApi::AImageReader_setImageListener(
    AImageReader* reader, AImageReader_ImageListener* listener) const {
  MOZ_RELEASE_ASSERT(mAImageReader_setImageListener);
  return mAImageReader_setImageListener(reader, listener);
}

media_status_t AndroidImageApi::AImageReader_newWithUsage(
    int32_t width, int32_t height, int32_t format, uint64_t usage,
    int32_t maxImages, AImageReader** reader) const {
  MOZ_RELEASE_ASSERT(mAImageReader_newWithUsage);
  return mAImageReader_newWithUsage(width, height, format, usage, maxImages,
                                    reader);
}

media_status_t AndroidImageApi::AImageReader_acquireNextImageAsync(
    AImageReader* reader, AImage** image, int* acquireFenceFd) const {
  MOZ_RELEASE_ASSERT(mAImageReader_acquireNextImageAsync);
  return mAImageReader_acquireNextImageAsync(reader, image, acquireFenceFd);
}

media_status_t AndroidImageApi::AImageReader_acquireLatestImageAsync(
    AImageReader* reader, AImage** image, int* acquireFenceFd) const {
  MOZ_RELEASE_ASSERT(mAImageReader_acquireLatestImageAsync);
  return mAImageReader_acquireLatestImageAsync(reader, image, acquireFenceFd);
}

media_status_t AndroidImageApi::AImageReader_setBufferRemovedListener(
    AImageReader* reader, AImageReader_BufferRemovedListener* listener) const {
  MOZ_RELEASE_ASSERT(mAImageReader_setBufferRemovedListener);
  return mAImageReader_setBufferRemovedListener(reader, listener);
}

void AndroidImageApi::AImage_delete(AImage* image) const {
  MOZ_RELEASE_ASSERT(mAImage_delete);
  return mAImage_delete(image);
}

media_status_t AndroidImageApi::AImage_getWidth(const AImage* image,
                                                int32_t* width) const {
  MOZ_RELEASE_ASSERT(mAImage_getWidth);
  return mAImage_getWidth(image, width);
}

media_status_t AndroidImageApi::AImage_getHeight(const AImage* image,
                                                 int32_t* height) const {
  MOZ_RELEASE_ASSERT(mAImage_getHeight);
  return mAImage_getHeight(image, height);
}

media_status_t AndroidImageApi::AImage_getFormat(const AImage* image,
                                                 int32_t* format) const {
  MOZ_RELEASE_ASSERT(mAImage_getFormat);
  return mAImage_getFormat(image, format);
}

media_status_t AndroidImageApi::AImage_getCropRect(const AImage* image,
                                                   AImageCropRect* rect) const {
  MOZ_RELEASE_ASSERT(mAImage_getCropRect);
  return mAImage_getCropRect(image, rect);
}

media_status_t AndroidImageApi::AImage_getTimestamp(
    const AImage* image, int64_t* timestampNs) const {
  MOZ_RELEASE_ASSERT(mAImage_getTimestamp);
  return mAImage_getTimestamp(image, timestampNs);
}

media_status_t AndroidImageApi::AImage_getNumberOfPlanes(
    const AImage* image, int32_t* numPlanes) const {
  MOZ_RELEASE_ASSERT(mAImage_getNumberOfPlanes);
  return mAImage_getNumberOfPlanes(image, numPlanes);
}

media_status_t AndroidImageApi::AImage_getPlanePixelStride(
    const AImage* image, int planeIdx, int32_t* pixelStride) const {
  MOZ_RELEASE_ASSERT(mAImage_getPlanePixelStride);
  return mAImage_getPlanePixelStride(image, planeIdx, pixelStride);
}

media_status_t AndroidImageApi::AImage_getPlaneRowStride(
    const AImage* image, int planeIdx, int32_t* rowStride) const {
  MOZ_RELEASE_ASSERT(mAImage_getPlaneRowStride);
  return mAImage_getPlaneRowStride(image, planeIdx, rowStride);
}

media_status_t AndroidImageApi::AImage_getPlaneData(const AImage* image,
                                                    int planeIdx,
                                                    uint8_t** data,
                                                    int* dataLength) {
  MOZ_RELEASE_ASSERT(mAImage_getPlaneData);
  return mAImage_getPlaneData(image, planeIdx, data, dataLength);
}

void AndroidImageApi::AImage_deleteAsync(AImage* image,
                                         int releaseFenceFd) const {
  MOZ_RELEASE_ASSERT(mAImage_deleteAsync);
  return mAImage_deleteAsync(image, releaseFenceFd);
}

media_status_t AndroidImageApi::AImage_getHardwareBuffer(
    const AImage* image, AHardwareBuffer** buffer) const {
  MOZ_RELEASE_ASSERT(mAImage_getHardwareBuffer);
  return mAImage_getHardwareBuffer(image, buffer);
}

jobject AndroidImageApi::ANativeWindow_toSurface(JNIEnv* env,
                                                 ANativeWindow* window) const {
  MOZ_RELEASE_ASSERT(mANativeWindow_toSurface);
  return mANativeWindow_toSurface(env, window);
}

AndroidImage::AndroidImage(AImage* aImage,
                           const RefPtr<AndroidImageReader>& aImageReader)
    : mImage(aImage), mImageReader(aImageReader) {}

AndroidImage::~AndroidImage() {
  const RefPtr<AndroidImageReader> reader(mImageReader);
  MOZ_ASSERT(reader, "Image should not outlive its ImageReader");
  if (reader) {
    reader->ReleaseImage(this);
  } else {
    layers::AndroidImageApi::Get()->AImage_delete(mImage);
  }
}

RefPtr<AndroidHardwareBuffer> AndroidImage::GetHardwareBuffer() {
  if (mHardwareBuffer) {
    return mHardwareBuffer;
  }

  const auto* api = layers::AndroidImageApi::Get();
  AHardwareBuffer* buffer;
  media_status_t res = api->AImage_getHardwareBuffer(mImage, &buffer);
  if (res != AMEDIA_OK) {
    gfxCriticalNote << "AImage_getHardwareBuffer failed:" << gfx::hexa(res);
    return nullptr;
  }

  // FIXME: don't hard code this format
  mHardwareBuffer =
      AndroidHardwareBuffer::FromNativeBuffer(buffer, gfx::SurfaceFormat::R8G8B8X8);
  return mHardwareBuffer;
}

int64_t AndroidImage::GetTimestamp() const {
  const auto* api = layers::AndroidImageApi::Get();

  int64_t timestamp;
  media_status_t res = api->AImage_getTimestamp(mImage, &timestamp);
  MOZ_RELEASE_ASSERT(res == AMEDIA_OK);

  return timestamp;
}

AndroidImageReader::AndroidImageReader(AImageReader* aImageReader,
                                       int aMaxImages)
    : mImageReader(aImageReader),
      mMonitor("AndroidImageReader"),
      mMaxAcquiredImages(aMaxImages) {
  const auto* api = AndroidImageApi::Get();
  mListener.context = this;
  mListener.onImageAvailable = [](void* context, AImageReader* reader) {
    AndroidImageReader* self = (AndroidImageReader*)context;
    MOZ_ASSERT(self->mImageReader == reader);
    self->OnImageAvailable();
  };
  media_status_t res =
      api->AImageReader_setImageListener(mImageReader, &mListener);
  MOZ_RELEASE_ASSERT(res == AMEDIA_OK);
}

AndroidImageReader::~AndroidImageReader() {
  const auto* api = AndroidImageApi::Get();
  api->AImageReader_setImageListener(mImageReader, nullptr);
  api->AImageReader_delete(mImageReader);
}

/* static */ RefPtr<AndroidImageReader> AndroidImageReader::Create(
    int aWidth, int aHeight, int aFormat, int aMaxImages, int64_t aUsage) {
  const auto* api = AndroidImageApi::Get();
  AImageReader* imageReader = nullptr;
  media_status_t res = api->AImageReader_newWithUsage(
      aWidth, aHeight, aFormat, aUsage, aMaxImages, &imageReader);
  if (res != AMEDIA_OK) {
    gfxCriticalNote << "AImageReader_newWithUsage failed: " << gfx::hexa(res);
    return nullptr;
  }

  return new AndroidImageReader(imageReader, aMaxImages);
}

java::sdk::Surface::LocalRef AndroidImageReader::GetSurface() {
  const auto* api = AndroidImageApi::Get();
  ANativeWindow* window = nullptr;
  api->AImageReader_getWindow(mImageReader, &window);
  jobject surface =
      api->ANativeWindow_toSurface(jni::GetEnvForThread(), window);
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

  const auto* api = layers::AndroidImageApi::Get();
  AImage* image;
  media_status_t res = api->AImageReader_acquireNextImage(mImageReader, &image);
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
  const auto* api = layers::AndroidImageApi::Get();

  MOZ_RELEASE_ASSERT(aImage->mHardwareBuffer->hasOneRef());
  ipc::FileDescriptor releaseFence =
      aImage->mHardwareBuffer->GetAndResetReleaseFence();
  aImage->mHardwareBuffer = nullptr;

  if (releaseFence.IsValid()) {
    api->AImage_deleteAsync(aImage->mImage,
                            releaseFence.TakePlatformHandle().release());
  } else {
    api->AImage_delete(aImage->mImage);
  }
  aImage->mImage = nullptr;
  if (--mAcquiredImages < mMaxAcquiredImages) {
    lock.NotifyAll();
  }
}

}  // namespace mozilla::layers
