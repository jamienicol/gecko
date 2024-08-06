/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef LAYERS_ANDROID_IMAGE_H_
#define LAYERS_ANDROID_IMAGE_H_

#include "media/NdkImage.h"
#include "media/NdkImageReader.h"
#include "media/NdkMediaError.h"
#include "mozilla/StaticPtr.h"

namespace mozilla::layers {

// Dynamic function loader for the Android NDK's AImage, AImageReader, and
// related APIs. It is the caller's responsibility to ensure any used functions
// are supported on the running Android version.
class AndroidImageApi {
 public:
  // Initializes the singleton object and attempts to load the functions.
  static void Init();

  // Obtains the singleton instance. Init() must have been called prior to this.
  static const AndroidImageApi* Get();

  media_status_t AImageReader_new(int32_t width, int32_t height, int32_t format,
                                  int32_t maxImages,
                                  AImageReader** reader) const;
  void AImageReader_delete(AImageReader* reader) const;
  media_status_t AImageReader_getWindow(AImageReader* reader,
                                        ANativeWindow** window) const;
  media_status_t AImageReader_getWidth(const AImageReader* reader,
                                       int32_t* width) const;
  media_status_t AImageReader_getHeight(const AImageReader* reader,
                                        int32_t* height) const;
  media_status_t AImageReader_getFormat(const AImageReader* reader,
                                        int32_t* format) const;
  media_status_t AImageReader_getMaxImages(const AImageReader* reader,
                                           int32_t* maxImages) const;
  media_status_t AImageReader_acquireNextImage(AImageReader* reader,
                                               AImage** image) const;
  media_status_t AImageReader_acquireLatestImage(AImageReader* reader,
                                                 AImage** image) const;
  media_status_t AImageReader_setImageListener(
      AImageReader* reader, AImageReader_ImageListener* listener) const;
  media_status_t AImageReader_newWithUsage(int32_t width, int32_t height,
                                           int32_t format, uint64_t usage,
                                           int32_t maxImages,
                                           AImageReader** reader) const;
  media_status_t AImageReader_acquireNextImageAsync(AImageReader* reader,
                                                    AImage** image,
                                                    int* acquireFenceFd) const;
  media_status_t AImageReader_acquireLatestImageAsync(
      AImageReader* reader, AImage** image, int* acquireFenceFd) const;
  media_status_t AImageReader_setBufferRemovedListener(
      AImageReader* reader, AImageReader_BufferRemovedListener* listener) const;

  void AImage_delete(AImage* image) const;
  media_status_t AImage_getWidth(const AImage* image, int32_t* width) const;
  media_status_t AImage_getHeight(const AImage* image, int32_t* height) const;
  media_status_t AImage_getFormat(const AImage* image, int32_t* format) const;
  media_status_t AImage_getCropRect(const AImage* image,
                                    AImageCropRect* rect) const;
  media_status_t AImage_getTimestamp(const AImage* image,
                                     int64_t* timestampNs) const;
  media_status_t AImage_getNumberOfPlanes(const AImage* image,
                                          int32_t* numPlanes) const;
  media_status_t AImage_getPlanePixelStride(const AImage* image, int planeIdx,
                                            int32_t* pixelStride) const;
  media_status_t AImage_getPlaneRowStride(const AImage* image, int planeIdx,
                                          int32_t* rowStride) const;
  media_status_t AImage_getPlaneData(const AImage* image, int planeIdx,
                                     uint8_t** data, int* dataLength);
  void AImage_deleteAsync(AImage* image, int releaseFenceFd) const;
  media_status_t AImage_getHardwareBuffer(const AImage* image,
                                          AHardwareBuffer** buffer) const;
  jobject ANativeWindow_toSurface(JNIEnv* env, ANativeWindow* window) const;

 private:
  AndroidImageApi() = default;
  bool Load();

  using _AImageReader_new = media_status_t (*)(int32_t width, int32_t height,
                                               int32_t format,
                                               int32_t maxImages,
                                               AImageReader** reader);
  using _AImageReader_delete = void (*)(AImageReader* reader);
  using _AImageReader_getWindow = media_status_t (*)(AImageReader* reader,
                                                     ANativeWindow** window);
  using _AImageReader_getWidth = media_status_t (*)(const AImageReader* reader,
                                                    int32_t* width);
  using _AImageReader_getHeight = media_status_t (*)(const AImageReader* reader,
                                                     int32_t* height);
  using _AImageReader_getFormat = media_status_t (*)(const AImageReader* reader,
                                                     int32_t* format);
  using _AImageReader_getMaxImages =
      media_status_t (*)(const AImageReader* reader, int32_t* maxImages);
  using _AImageReader_acquireNextImage =
      media_status_t (*)(AImageReader* reader, AImage** image);
  using _AImageReader_acquireLatestImage =
      media_status_t (*)(AImageReader* reader, AImage** image);
  using _AImageReader_setImageListener = media_status_t (*)(
      AImageReader* reader, AImageReader_ImageListener* listener);
  using _AImageReader_newWithUsage = media_status_t (*)(
      int32_t width, int32_t height, int32_t format, uint64_t usage,
      int32_t maxImages, AImageReader** reader);
  using _AImageReader_acquireNextImageAsync = media_status_t (*)(
      AImageReader* reader, AImage** image, int* acquireFenceFd);
  using _AImageReader_acquireLatestImageAsync = media_status_t (*)(
      AImageReader* reader, AImage** image, int* acquireFenceFd);
  using _AImageReader_setBufferRemovedListener = media_status_t (*)(
      AImageReader* reader, AImageReader_BufferRemovedListener* listener);

  using _AImage_delete = void (*)(AImage* image);
  using _AImage_getWidth = media_status_t (*)(const AImage* image,
                                              int32_t* width);
  using _AImage_getHeight = media_status_t (*)(const AImage* image,
                                               int32_t* height);
  using _AImage_getFormat = media_status_t (*)(const AImage* image,
                                               int32_t* format);
  using _AImage_getCropRect = media_status_t (*)(const AImage* image,
                                                 AImageCropRect* rect);
  using _AImage_getTimestamp = media_status_t (*)(const AImage* image,
                                                  int64_t* timestampNs);
  using _AImage_getNumberOfPlanes = media_status_t (*)(const AImage* image,
                                                       int32_t* numPlanes);
  using _AImage_getPlanePixelStride = media_status_t (*)(const AImage* image,
                                                         int planeIdx,
                                                         int32_t* pixelStride);
  using _AImage_getPlaneRowStride = media_status_t (*)(const AImage* image,
                                                       int planeIdx,
                                                       int32_t* rowStride);
  using _AImage_getPlaneData = media_status_t (*)(const AImage* image,
                                                  int planeIdx, uint8_t** data,
                                                  int* dataLength);
  using _AImage_deleteAsync = void (*)(AImage* image, int releaseFenceFd);
  using _AImage_getHardwareBuffer =
      media_status_t (*)(const AImage* image, AHardwareBuffer** buffer);
  using _ANativeWindow_toSurface = jobject (*)(JNIEnv* env,
                                               ANativeWindow* window);

  _AImageReader_new mAImageReader_new = nullptr;
  _AImageReader_delete mAImageReader_delete = nullptr;
  _AImageReader_getWindow mAImageReader_getWindow = nullptr;
  _AImageReader_getWidth mAImageReader_getWidth = nullptr;
  _AImageReader_getHeight mAImageReader_getHeight = nullptr;
  _AImageReader_getFormat mAImageReader_getFormat = nullptr;
  _AImageReader_getMaxImages mAImageReader_getMaxImages = nullptr;
  _AImageReader_acquireNextImage mAImageReader_acquireNextImage = nullptr;
  _AImageReader_acquireLatestImage mAImageReader_acquireLatestImage = nullptr;
  _AImageReader_setImageListener mAImageReader_setImageListener = nullptr;
  _AImageReader_newWithUsage mAImageReader_newWithUsage = nullptr;
  _AImageReader_acquireNextImageAsync mAImageReader_acquireNextImageAsync =
      nullptr;
  _AImageReader_acquireLatestImageAsync mAImageReader_acquireLatestImageAsync =
      nullptr;
  _AImageReader_setBufferRemovedListener
      mAImageReader_setBufferRemovedListener = nullptr;

  _AImage_delete mAImage_delete = nullptr;
  _AImage_getWidth mAImage_getWidth = nullptr;
  _AImage_getHeight mAImage_getHeight = nullptr;
  _AImage_getFormat mAImage_getFormat = nullptr;
  _AImage_getCropRect mAImage_getCropRect = nullptr;
  _AImage_getTimestamp mAImage_getTimestamp = nullptr;
  _AImage_getNumberOfPlanes mAImage_getNumberOfPlanes = nullptr;
  _AImage_getPlanePixelStride mAImage_getPlanePixelStride = nullptr;
  _AImage_getPlaneRowStride mAImage_getPlaneRowStride = nullptr;
  _AImage_getPlaneData mAImage_getPlaneData = nullptr;
  _AImage_deleteAsync mAImage_deleteAsync = nullptr;
  _AImage_getHardwareBuffer mAImage_getHardwareBuffer = nullptr;
  _ANativeWindow_toSurface mANativeWindow_toSurface = nullptr;

  static StaticAutoPtr<AndroidImageApi> sInstance;
};

}  // namespace mozilla::layers

#endif  // LAYERS_ANDROID_IMAGE_H_
