/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_ANDROIDIMAGEREADER
#define MOZILLA_LAYERS_ANDROIDIMAGEREADER

#include "mozilla/StaticPtr.h"

#include <jni.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <media/NdkMediaError.h>

typedef struct AImage AImage;
typedef struct AImageCropRect {
  int32_t left;
  int32_t top;
  int32_t right;
  int32_t bottom;
} AImageCropRect;

typedef struct AImageReader AImageReader;
typedef void (*AImageReader_ImageCallback)(void* context, AImageReader* reader);

typedef struct AImageReader_ImageListener {
  void* context;
  AImageReader_ImageCallback onImageAvailable;
} AImageReader_ImageListener;

typedef void (*AImageReader_BufferRemovedCallback)(void* context,
                                                   AImageReader* reader,
                                                   AHardwareBuffer* buffer);

typedef struct AImageReader_BufferRemovedListener {
  void* context;
  AImageReader_BufferRemovedCallback onBufferRemoved;
} AImageReader_BufferRemovedListener;

enum AIMAGE_FORMATS {
  AIMAGE_FORMAT_YUV_420_888 = 0x23,
  AIMAGE_FORMAT_PRIVATE = 0x22
};

namespace mozilla {

namespace layers {

class AndroidImageReaderApi final {
 public:
  static void Init();
  static void Shutdown();

  static AndroidImageReaderApi* Get() { return sInstance; };

  void AImage_delete(AImage* aImage) const;
  void AImage_deleteAsync(AImage* aImage, int aReleaseFenceFd) const;
  media_status_t AImage_getHardwareBuffer(
      const AImage* image,
      /*out*/ AHardwareBuffer** buffer) const;

  media_status_t AImageReader_newWithUsage(
      int32_t aWidth, int32_t aHeight, int32_t aFormat, uint64_t aUsage,
      int32_t aMaxImages,
      /*out*/ AImageReader** aReader) const;
  void AImageReader_delete(AImageReader* aReader) const;
  media_status_t AImageReader_setImageListener(
      AImageReader* aReader, AImageReader_ImageListener* aListener) const;
  media_status_t AImageReader_setBufferRemovedListener(
      AImageReader* aReader,
      AImageReader_BufferRemovedListener* aListener) const;
  media_status_t AImageReader_getWindow(AImageReader* aReader,
                                        /*out*/ ANativeWindow** aWindow) const;
  media_status_t AImageReader_acquireNextImage(AImageReader* aReader,
                                               /*out*/ AImage** aImage) const;
  media_status_t AImageReader_acquireLatestImage(AImageReader* aReader,
                                                 /*out*/ AImage** aImage) const;
  media_status_t AImageReader_acquireNextImageAsync(
      AImageReader* aReader, /*out*/ AImage** aImage,
      /*out*/ int* aAcquireFenceFd) const;
  media_status_t AImageReader_acquireLatestImageAsync(
      AImageReader* aReader, /*out*/ AImage** aImage,
      /*out*/ int* aAcquireFenceFd) const;

  jobject ANativeWindow_toSurface(JNIEnv* env, ANativeWindow* window) const;

 private:
  bool Load();

  static StaticAutoPtr<AndroidImageReaderApi> sInstance;

  using _AImage_delete = void (*)(AImage* image);
  using _AImage_deleteAsync = void (*)(AImage* image, int releaseFenceFd);
  using _AImage_getHardwareBuffer =
      media_status_t (*)(const AImage* image, AHardwareBuffer** buffer);
  using _AImage_getWidth = media_status_t (*)(const AImage* image,
                                              int32_t* width);
  using _AImage_getHeight = media_status_t (*)(const AImage* image,
                                               int32_t* height);
  using _AImage_getCropRect = media_status_t (*)(const AImage* image,
                                                 AImageCropRect* rect);

  using _AImageReader_newWithUsage = media_status_t (*)(
      int32_t width, int32_t height, int32_t format, uint64_t usage,
      int32_t maxImages, /*out*/ AImageReader** reader);
  using _AImageReader_delete = void (*)(AImageReader* reader);
  using _AImageReader_setImageListener = media_status_t (*)(
      AImageReader* reader, AImageReader_ImageListener* listener);
  using _AImageReader_setBufferRemovedListener = media_status_t (*)(
      AImageReader* reader, AImageReader_BufferRemovedListener* listener);
  using _AImageReader_getWindow =
      media_status_t (*)(AImageReader* reader, /*out*/ ANativeWindow** window);
  using _AImageReader_getFormat = media_status_t (*)(const AImageReader* reader,
                                                     /*out*/ int32_t* format);
  using _AImageReader_acquireNextImage =
      media_status_t (*)(AImageReader* reader, /*out*/ AImage** image);
  using _AImageReader_acquireLatestImage =
      media_status_t (*)(AImageReader* reader, /*out*/ AImage** image);
  using _AImageReader_acquireNextImageAsync =
      media_status_t (*)(AImageReader* reader, /*out*/ AImage** image,
                         /*out*/ int* acquireFenceFd);
  using _AImageReader_acquireLatestImageAsync =
      media_status_t (*)(AImageReader* reader, /*out*/ AImage** image,
                         /*out*/ int* acquireFenceFd);
  using _ANativeWindow_toSurface = jobject (*)(JNIEnv* env,
                                               ANativeWindow* window);

  _AImage_delete mAImage_delete = nullptr;
  _AImage_deleteAsync mAImage_deleteAsync = nullptr;
  _AImage_getHardwareBuffer mAImage_getHardwareBuffer = nullptr;
  _AImage_getWidth mAImage_getWidth = nullptr;
  _AImage_getHeight mAImage_getHeight = nullptr;
  _AImage_getCropRect mAImage_getCropRect = nullptr;

  _AImageReader_newWithUsage mAImageReader_newWithUsage = nullptr;
  _AImageReader_delete mAImageReader_delete = nullptr;
  _AImageReader_setImageListener mAImageReader_setImageListener = nullptr;
  _AImageReader_setBufferRemovedListener
      mAImageReader_setBufferRemovedListener = nullptr;
  _AImageReader_getWindow mAImageReader_getWindow = nullptr;
  _AImageReader_getFormat mAImageReader_getFormat = nullptr;
  _AImageReader_acquireNextImage mAImageReader_acquireNextImage = nullptr;
  _AImageReader_acquireLatestImage mAImageReader_acquireLatestImage = nullptr;
  _AImageReader_acquireNextImageAsync mAImageReader_acquireNextImageAsync =
      nullptr;
  _AImageReader_acquireLatestImageAsync mAImageReader_acquireLatestImageAsync =
      nullptr;
  _ANativeWindow_toSurface mANativeWindow_toSurface = nullptr;
};

}  // namespace mozilla::layers

template <>
class DefaultDelete<AImageReader> {
 public:
  void operator()(AImageReader* aPtr) const {
    printf_stderr("jamiedbg AImageReader_delete()\n");
    layers::AndroidImageReaderApi::Get()->AImageReader_delete(aPtr);
  }
};

}  // namespace mozilla

#endif // MOZILLA_LAYERS_ANDROIDIMAGEREADER
