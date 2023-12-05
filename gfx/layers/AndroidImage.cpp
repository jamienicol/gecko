/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidImage.h"

namespace mozilla::layers {

StaticAutoPtr<AndroidImageApi> AndroidImageApi::sInstance;

/* static */ void AndroidImageApi::Init() {
  sInstance = new AndroidImageApi();
  if (!sInstance->Load()) {
    sInstance = nullptr;
  }
}

/* static */ AndroidImageApi* AndroidImageApi::Get() { return sInstance; }

/* static */
void AndroidImageApi::Shutdown() { sInstance = nullptr; }

#define LOAD_FN(fun)                           \
  m##fun = (_##fun)dlsym(handle, #fun);        \
  if (!m##fun) {                               \
    gfxCriticalNote << "Failed to load " #fun; \
    return false;                              \
  }

bool AndroidImageApi::Load() {
  void* handle = dlopen("libandroid.so", RTLD_LAZY | RTLD_LOCAL);
  MOZ_ASSERT(handle);
  if (!handle) {
    gfxCriticalNote << "Failed to load libandroid.so";
    return false;
  }

  LOAD_FN(AImageReader_new);
  LOAD_FN(AImageReader_delete);
  LOAD_FN(AImageReader_getWindow);
  LOAD_FN(AImageReader_setImageListener);
  LOAD_FN(ANativeWindow_toSurface);

  return true;
}

media_status_t AndroidImageApi::AImageReader_new(
    int32_t width, int32_t height, int32_t format, int32_t maxImages,
    /*out*/ AImageReader** reader) const {
  return mAImageReader_new(width, height, format, maxImages, reader);
}

media_status_t AndroidImageApi::AImageReader_newWithUsage(
    int32_t width, int32_t height, int32_t format, uint64_t usage,
    int32_t maxImages,
    /*out*/ AImageReader** reader) const {
  return mAImageReader_newWithUsage(width, height, format, usage, maxImages,
                                    reader);
}

void AndroidImageApi::AImageReader_delete(AImageReader* reader) const {
  mAImageReader_delete(reader);
}

media_status_t AndroidImageApi::AImageReader_getWindow(
    AImageReader* reader, /*out*/ ANativeWindow** window) const {
  return mAImageReader_getWindow(reader, window);
}

media_status_t AndroidImageApi::AImageReader_setImageListener(
    AImageReader* reader, AImageReader_ImageListener* listener) const {
  return mAImageReader_setImageListener(reader, listener);
}

jobject AndroidImageApi::ANativeWindow_toSurface(JNIEnv* env,
                                                 ANativeWindow* window) const {
  return mANativeWindow_toSurface(env, window);
}

#undef LOAD_FN

}  // namespace mozilla::layers
