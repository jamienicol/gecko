/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_ANDROID_IMAGE
#define MOZILLA_LAYERS_ANDROID_IMAGE

#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

#include "mozilla/StaticPtr.h"

namespace mozilla::layers {

class AndroidImageApi {
 public:
  static void Init();
  static void Shutdown();

  static AndroidImageApi* Get();

  media_status_t AImageReader_new(int32_t width, int32_t height, int32_t format,
                                  int32_t maxImages,
                                  /*out*/ AImageReader** reader) const;
  media_status_t AImageReader_newWithUsage(int32_t width, int32_t height,
                                           int32_t format, uint64_t usage,
                                           int32_t maxImages,
                                           /*out*/ AImageReader** reader) const;
  void AImageReader_delete(AImageReader* reader) const;
  media_status_t AImageReader_getWindow(AImageReader* reader,
                                        /*out*/ ANativeWindow** window) const;
  media_status_t AImageReader_setImageListener(
      AImageReader* reader, AImageReader_ImageListener* listener) const;
  jobject ANativeWindow_toSurface(JNIEnv* env, ANativeWindow* window) const;

 private:
  AndroidImageApi() = default;
  bool Load();

  using _AImageReader_new = media_status_t (*)(int32_t width, int32_t height,
                                               int32_t format,
                                               int32_t maxImages,
                                               /*out*/ AImageReader** reader);
  using _AImageReader_newWithUsage =
      media_status_t (*)(int32_t width, int32_t height, int32_t format,
                         uint64_t usage, int32_t maxImages,
                         /*out*/ AImageReader** reader);
  using _AImageReader_delete = void (*)(AImageReader* reader);
  using _AImageReader_getWindow =
      media_status_t (*)(AImageReader* reader, /*out*/ ANativeWindow** window);
  using _AImageReader_setImageListener = media_status_t (*)(
      AImageReader* reader, AImageReader_ImageListener* listener);
  using _ANativeWindow_toSurface = jobject (*)(JNIEnv* env,
                                               ANativeWindow* window);

  _AImageReader_new mAImageReader_new = nullptr;
  _AImageReader_newWithUsage mAImageReader_newWithUsage = nullptr;
  _AImageReader_delete mAImageReader_delete = nullptr;
  _AImageReader_getWindow mAImageReader_getWindow = nullptr;
  _AImageReader_setImageListener mAImageReader_setImageListener = nullptr;
  _ANativeWindow_toSurface mANativeWindow_toSurface = nullptr;

  static StaticAutoPtr<AndroidImageApi> sInstance;
};

}  // namespace mozilla::layers

#endif  // MOZILLA_LAYERS_ANDROID_IMAGE
