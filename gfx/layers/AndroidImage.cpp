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
  return mAImageReader_new(width, height, format, maxImages, reader);
}

void AndroidImageApi::AImageReader_delete(AImageReader* reader) const {
  mAImageReader_delete(reader);
}

media_status_t AndroidImageApi::AImageReader_getWindow(
    AImageReader* reader, ANativeWindow** window) const {
  return mAImageReader_getWindow(reader, window);
}

media_status_t AndroidImageApi::AImageReader_getWidth(const AImageReader* reader,
                                     int32_t* width) const {
  return mAImageReader_getWidth(reader, width);
}

media_status_t AndroidImageApi::AImageReader_getHeight(const AImageReader* reader,
                                      int32_t* height) const {
  return mAImageReader_getHeight(reader, height);
}

media_status_t AndroidImageApi::AImageReader_getFormat(const AImageReader* reader,
                                      int32_t* format) const {
  return mAImageReader_getFormat(reader, format);
}

media_status_t AndroidImageApi::AImageReader_getMaxImages(const AImageReader* reader,
                                         int32_t* maxImages) const {
  return mAImageReader_getMaxImages(reader, maxImages);
}

media_status_t AndroidImageApi::AImageReader_acquireNextImage(
    AImageReader* reader, AImage** image) const {
  return mAImageReader_acquireNextImage(reader, image);
}

media_status_t AndroidImageApi::AImageReader_acquireLatestImage(
    AImageReader* reader, AImage** image) const {
  return mAImageReader_acquireLatestImage(reader, image);
}

media_status_t AndroidImageApi::AImageReader_setImageListener(
    AImageReader* reader, AImageReader_ImageListener* listener) const {
  return mAImageReader_setImageListener(reader, listener);
}

media_status_t AndroidImageApi::AImageReader_newWithUsage(
    int32_t width, int32_t height, int32_t format, uint64_t usage,
    int32_t maxImages, AImageReader** reader) const {
  return mAImageReader_newWithUsage(width, height, format, usage, maxImages,
                                    reader);
}

media_status_t AndroidImageApi::AImageReader_acquireNextImageAsync(
    AImageReader* reader, AImage** image, int* acquireFenceFd) const {
  return mAImageReader_acquireNextImageAsync(reader, image, acquireFenceFd);
}

media_status_t AndroidImageApi::AImageReader_acquireLatestImageAsync(
    AImageReader* reader, AImage** image, int* acquireFenceFd) const {
  return mAImageReader_acquireLatestImageAsync(reader, image, acquireFenceFd);
}

media_status_t AndroidImageApi::AImageReader_setBufferRemovedListener(
    AImageReader* reader, AImageReader_BufferRemovedListener* listener) const {
  return mAImageReader_setBufferRemovedListener(reader, listener);
}

void AndroidImageApi::AImage_delete(AImage* image) const {
  return mAImage_delete(image);
}

media_status_t AndroidImageApi::AImage_getWidth(const AImage* image,
                                                int32_t* width) const {
  return mAImage_getWidth(image, width);
}

media_status_t AndroidImageApi::AImage_getHeight(const AImage* image,
                                                 int32_t* height) const {
  return mAImage_getHeight(image, height);
}

media_status_t AndroidImageApi::AImage_getFormat(const AImage* image,
                                                 int32_t* format) const {
  return mAImage_getFormat(image, format);
}

media_status_t AndroidImageApi::AImage_getCropRect(const AImage* image,
                                                   AImageCropRect* rect) const {
  return mAImage_getCropRect(image, rect);
}

media_status_t AndroidImageApi::AImage_getTimestamp(
    const AImage* image, int64_t* timestampNs) const {
  return mAImage_getTimestamp(image, timestampNs);
}

media_status_t AndroidImageApi::AImage_getNumberOfPlanes(
    const AImage* image, int32_t* numPlanes) const {
  return mAImage_getNumberOfPlanes(image, numPlanes);
}

media_status_t AndroidImageApi::AImage_getPlanePixelStride(
    const AImage* image, int planeIdx, int32_t* pixelStride) const {
  return mAImage_getPlanePixelStride(image, planeIdx, pixelStride);
}

media_status_t AndroidImageApi::AImage_getPlaneRowStride(
    const AImage* image, int planeIdx, int32_t* rowStride) const {
  return mAImage_getPlaneRowStride(image, planeIdx, rowStride);
}

media_status_t AndroidImageApi::AImage_getPlaneData(const AImage* image,
                                                    int planeIdx,
                                                    uint8_t** data,
                                                    int* dataLength) {
  return mAImage_getPlaneData(image, planeIdx, data, dataLength);
}

void AndroidImageApi::AImage_deleteAsync(AImage* image,
                                         int releaseFenceFd) const {
  return mAImage_deleteAsync(image, releaseFenceFd);
}

media_status_t AndroidImageApi::AImage_getHardwareBuffer(
    const AImage* image, AHardwareBuffer** buffer) const {
  return mAImage_getHardwareBuffer(image, buffer);
}

jobject AndroidImageApi::ANativeWindow_toSurface(JNIEnv* env,
                                                 ANativeWindow* window) const {
  return mANativeWindow_toSurface(env, window);
}

AndroidImageReader::AndroidImageReader(AImageReader* aImageReader)
    : mImageReader(aImageReader), mMonitor("AndroidImageReader") {
  const auto* api = AndroidImageApi::Get();
  mListener.context = this;
  mListener.onImageAvailable = [](void* context, AImageReader* reader) {
    AndroidImageReader* self = (AndroidImageReader*)context;
    MOZ_ASSERT(self->mImageReader == reader);
    self->OnImageAvailable();
  };
  media_status_t res =
      api->AImageReader_setImageListener(mImageReader, &mListener);
  if (res != AMEDIA_OK) {
    printf_stderr("jamiedbg AImageReader_setImageListener failed: %d\n", res);
  }
}

AndroidImageReader::~AndroidImageReader() {
  const auto* api = AndroidImageApi::Get();
  api->AImageReader_setImageListener(mImageReader, nullptr);
  api->AImageReader_delete(mImageReader);
}

/* static */ java::GeckoImageReader::LocalRef AndroidImageReader::NativeCreate(
    int aWidth, int aHeight, int aFormat, int aMaxImages, int64_t aUsage) {
  const auto* api = AndroidImageApi::Get();
  AImageReader* imageReader = nullptr;
  // FIXME: fallback to without usage on older SDK levels.
  // And return null if API not supported at all?
  media_status_t res = api->AImageReader_newWithUsage(
      aWidth, aHeight, aFormat, aUsage, aMaxImages, &imageReader);
  if (res != AMEDIA_OK) {
    printf_stderr("jamiedbg AImageReader_newWithUsage failed: %d\n", res);
    return nullptr;
  }

  RefPtr<AndroidImageReader> native = new AndroidImageReader(imageReader);
  java::GeckoImageReader::LocalRef java = java::GeckoImageReader::New();
  AndroidImageReader::AttachNative(java, native);

  return java;
}

mozilla::jni::Object::LocalRef AndroidImageReader::GetSurface() {
  const auto* api = AndroidImageApi::Get();
  ANativeWindow* window = nullptr;
  api->AImageReader_getWindow(mImageReader, &window);
  jobject surface =
      api->ANativeWindow_toSurface(jni::GetEnvForThread(), window);
  return mozilla::jni::Object::Ref::From(surface);
}

/* static */
RefPtr<AndroidImageReader> AndroidImageReader::Lookup(uint64_t aHandle) {
  auto imageReader = java::GeckoImageReader::Lookup(aHandle);
  if (!imageReader) {
    printf_stderr("jamiedbg Failed to get image reader instance\n");
    return nullptr;
  }

  return AndroidImageReader::GetNative(imageReader);
}

RefPtr<AndroidImage> AndroidImageReader::AcquireNextImage() {
  MonitorAutoLock lock(mMonitor);

  while (mPendingImages == 0) {
    if (lock.Wait(TimeDuration::FromMilliseconds(1000)) == CVStatus::Timeout) {
      printf_stderr("jamiedbg Timeout waiting for image available callback\n");
      return mCurrentImage;
    }
  }

  const auto* api = layers::AndroidImageApi::Get();
  AImage* image;
  media_status_t res = api->AImageReader_acquireNextImage(mImageReader, &image);
  if (res != AMEDIA_OK) {
    printf_stderr("jamiedbg AImageReader_acquireNextImage failed: %d\n", res);
    return nullptr;
  }
  mPendingImages--;

  mCurrentImage = new AndroidImage(image);
  return mCurrentImage;
}

RefPtr<AndroidImage> AndroidImageReader::GetCurrentImage() {
  MonitorAutoLock lock(mMonitor);
  return mCurrentImage;
}

void AndroidImageReader::OnImageAvailable() {
  MonitorAutoLock lock(mMonitor);

  if (++mPendingImages == 1) {
    lock.NotifyAll();
  }
}

AndroidImage::AndroidImage(AImage* aImage) : mImage(aImage) {}

AndroidImage::~AndroidImage() {
  const auto* api = layers::AndroidImageApi::Get();
  api->AImage_delete(mImage);
}

AHardwareBuffer* AndroidImage::GetHardwareBuffer() const {
  const auto* api = layers::AndroidImageApi::Get();

  AHardwareBuffer* buffer;
  media_status_t res = api->AImage_getHardwareBuffer(mImage, &buffer);
  if (res != AMEDIA_OK) {
    printf_stderr("jamiedbg AImage_getHardwareBuffer failed: %d\n", res);
    return nullptr;
  }

  return buffer;
}

int64_t AndroidImage::GetTimestamp() const {
  const auto* api = layers::AndroidImageApi::Get();

  int64_t timestamp;
  media_status_t res = api->AImage_getTimestamp(mImage, &timestamp);
  MOZ_RELEASE_ASSERT(res == AMEDIA_OK);

  return timestamp;
}

}  // namespace mozilla::layers
