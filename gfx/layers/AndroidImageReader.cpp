/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidImageReader.h"

#include <dlfcn.h>

#include "mozilla/gfx/Logging.h"

namespace mozilla::layers {

StaticAutoPtr<AndroidImageReaderApi> AndroidImageReaderApi::sInstance;

/* static */
void AndroidImageReaderApi::Init() {
  sInstance = new AndroidImageReaderApi();
  if (!sInstance->Load()) {
    sInstance = nullptr;
  }
}

/* static */
void AndroidImageReaderApi::Shutdown() { sInstance = nullptr; }

bool AndroidImageReaderApi::Load() {
  void* handle = dlopen("libmediandk.so", RTLD_LAZY | RTLD_LOCAL);
  MOZ_ASSERT(handle);
  if (!handle) {
    gfxCriticalNote << "Failed to load libmediandk.so";
    return false;
  }

  mAImage_delete = (_AImage_delete)dlsym(handle, "AImage_delete");
  mAImage_deleteAsync =
      (_AImage_deleteAsync)dlsym(handle, "AImage_deleteAsync");
  mAImage_getHardwareBuffer =
      (_AImage_getHardwareBuffer)dlsym(handle, "AImage_getHardwareBuffer");
  mAImage_getWidth = (_AImage_getWidth)dlsym(handle, "AImage_getWidth");
  mAImage_getHeight = (_AImage_getHeight)dlsym(handle, "AImage_getHeight");
  mAImage_getCropRect =
      (_AImage_getCropRect)dlsym(handle, "AImage_getCropRect");

  mAImageReader_newWithUsage =
      (_AImageReader_newWithUsage)dlsym(handle, "AImageReader_newWithUsage");
  mAImageReader_setImageListener = (_AImageReader_setImageListener)dlsym(
      handle, "AImageReader_setImageListener");
  mAImageReader_setBufferRemovedListener =
      (_AImageReader_setBufferRemovedListener)dlsym(
          handle, "AImageReader_setBufferRemovedListener");

  mAImageReader_delete =
      (_AImageReader_delete)dlsym(handle, "AImageReader_delete");
  mAImageReader_getWindow =
      (_AImageReader_getWindow)dlsym(handle, "AImageReader_getWindow");
  mAImageReader_getFormat =
      (_AImageReader_getFormat)dlsym(handle, "AImageReader_getFormat");
  mAImageReader_acquireNextImage = (_AImageReader_acquireNextImage)dlsym(
      handle, "AImageReader_acquireNextImage");
  mAImageReader_acquireLatestImage = (_AImageReader_acquireLatestImage)dlsym(
      handle, "AImageReader_acquireLatestImage");
  mAImageReader_acquireNextImageAsync =
      (_AImageReader_acquireNextImageAsync)dlsym(
          handle, "AImageReader_acquireNextImageAsync");
  mAImageReader_acquireLatestImageAsync =
      (_AImageReader_acquireLatestImageAsync)dlsym(
          handle, "AImageReader_acquireLatestImageAsync");

  if (!mAImage_delete || !mAImage_deleteAsync || !mAImage_getHardwareBuffer ||
      !mAImage_getWidth || !mAImage_getHeight || !mAImage_getCropRect ||
      !mAImageReader_newWithUsage || !mAImageReader_delete ||
      !mAImageReader_setImageListener ||
      !mAImageReader_setBufferRemovedListener || !mAImageReader_getWindow ||
      !mAImageReader_getFormat || !mAImageReader_acquireNextImage ||
      !mAImageReader_acquireLatestImage ||
      !mAImageReader_acquireNextImageAsync ||
      !mAImageReader_acquireLatestImageAsync) {
    gfxCriticalNote << "Failed to load AImageReader";
    return false;
  }

  handle = dlopen("libandroid.so", RTLD_LAZY | RTLD_LOCAL);
  MOZ_ASSERT(handle);
  if (!handle) {
    gfxCriticalNote << "Failed to load libandroid.so";
    return false;
  }

  mANativeWindow_toSurface =
      (_ANativeWindow_toSurface)dlsym(handle, "ANativeWindow_toSurface");
  if (!mANativeWindow_toSurface) {
    gfxCriticalNote << "Failed to load ANativeWindow_toSurface";
    return false;
  }

  return true;
}

void AndroidImageReaderApi::AImage_delete(AImage* aImage) const {
  mAImage_delete(aImage);
}

void AndroidImageReaderApi::AImage_deleteAsync(AImage* aImage,
                                               int aReleaseFenceFd) const {
  mAImage_deleteAsync(aImage, aReleaseFenceFd);
}

media_status_t AndroidImageReaderApi::AImage_getHardwareBuffer(
    const AImage* aImage,
    /*out*/ AHardwareBuffer** aBuffer) const {
  return mAImage_getHardwareBuffer(aImage, aBuffer);
}

media_status_t AndroidImageReaderApi::AImageReader_newWithUsage(
    int32_t aWidth, int32_t aHeight, int32_t aFormat, uint64_t aUsage,
    int32_t aMaxImages,
    /*out*/ AImageReader** aReader) const {
  return mAImageReader_newWithUsage(aWidth, aHeight, aFormat, aUsage,
                                    aMaxImages, aReader);
}

media_status_t AndroidImageReaderApi::AImageReader_setImageListener(
    AImageReader* aReader, AImageReader_ImageListener* aListener) const {
  return mAImageReader_setImageListener(aReader, aListener);
}

media_status_t AndroidImageReaderApi::AImageReader_setBufferRemovedListener(
    AImageReader* aReader,
    AImageReader_BufferRemovedListener* aListener) const {
  return mAImageReader_setBufferRemovedListener(aReader, aListener);
}

void AndroidImageReaderApi::AImageReader_delete(AImageReader* aReader) const {
  mAImageReader_delete(aReader);
}

media_status_t AndroidImageReaderApi::AImageReader_getWindow(
    AImageReader* aReader,
    /*out*/ ANativeWindow** aWindow) const {
  return mAImageReader_getWindow(aReader, aWindow);
}

media_status_t AndroidImageReaderApi::AImageReader_acquireNextImageAsync(
    AImageReader* aReader, /*out*/ AImage** aImage,
    /*out*/ int* aAcquireFenceFd) const {
  return mAImageReader_acquireNextImageAsync(aReader, aImage, aAcquireFenceFd);
}

jobject AndroidImageReaderApi::ANativeWindow_toSurface(
    JNIEnv* aEnv, ANativeWindow* aWindow) const {
  return mANativeWindow_toSurface(aEnv, aWindow);
}

}  // namespace mozilla::layers
