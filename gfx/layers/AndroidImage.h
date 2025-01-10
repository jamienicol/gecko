/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef LAYERS_ANDROID_IMAGE_H_
#define LAYERS_ANDROID_IMAGE_H_

#include "SurfaceTexture.h"
#include "media/NdkImage.h"
#include "media/NdkImageReader.h"
#include "media/NdkMediaError.h"
#include "mozilla/layers/AndroidHardwareBuffer.h"
#include "mozilla/Monitor.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/ThreadSafeWeakPtr.h"

namespace mozilla::layers {

class AndroidImageReader;

// An Image acquired from the ImageReader. This allows access to the Image's
// HardwareBuffer.
class AndroidImage {
 public:
  friend class AndroidImageReader;
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AndroidImage);

  explicit AndroidImage(AImage* aImage,
                        const RefPtr<AndroidImageReader>& aImageReader);

  // Retrieves the Image's hardware buffer. All references to this buffer *must*
  // be dropped before the Image is destroyed.
  RefPtr<AndroidHardwareBuffer> GetHardwareBuffer();

  gfx::IntRect GetCropRect() const;

  // Gets the timestamp in nanoseconds of the image.
  int64_t GetTimestamp() const;

 private:
  ~AndroidImage();

  AImage* mImage;
  RefPtr<AndroidHardwareBuffer> mHardwareBuffer;
  ThreadSafeWeakPtr<AndroidImageReader> mImageReader;
};

class AndroidImageReader
    : public SupportsThreadSafeWeakPtr<AndroidImageReader> {
 public:
  friend AndroidImage::~AndroidImage();
  MOZ_DECLARE_REFCOUNTED_TYPENAME(AndroidImageReader)

  static RefPtr<AndroidImageReader> Create(int aWidth, int aHeight, int aFormat,
                                           int aMaxImages, int64_t aUsage);
  ~AndroidImageReader();

  // Retrieves the Java Surface object which can be used to produce frames for
  // this ImageReader, for example by configuring a MediaCodec with it as the
  // output Surface.
  java::sdk::Surface::LocalRef GetSurface();

  // Acquires the next available image, blocking if no image is yet available or
  // we have already acquired the maximum number of images.
  RefPtr<AndroidImage> AcquireNextImage();

 private:
  explicit AndroidImageReader(AImageReader* aImageReader, int aMaxImages);

  void OnImageAvailable();

  void ReleaseImage(AndroidImage* aImage);

  AImageReader* const mImageReader;
  AImageReader_ImageListener mListener;

  Monitor mMonitor;
  // The number of images that are available to be acquired. This is is
  // incremented by the OnImageAvailable() callback, called from the image
  // reader's internal thread. It is decremented by AcquireNextImage() after
  // acquiring an image, which can be called from any thread. When this is
  // incremented to 1 any thread waiting on the monitor will be notified.
  int mPendingImages MOZ_GUARDED_BY(mMonitor) = 0;
  // The number of images that are currently acquired. This is is
  // incremented by AcquireNextImage() after acquiring an image, which can be
  // called from any thread. It is decremented by ReleaseImage(), called when
  // the final reference to an acquired image is dropped, which can occur on any
  // thread. When this is decremented to less than mMaxAcquiredImages, any
  // thread waiting on the monitor will be notified.
  int mAcquiredImages MOZ_GUARDED_BY(mMonitor) = 0;
  const int mMaxAcquiredImages;
};

}  // namespace mozilla::layers

#endif  // LAYERS_ANDROID_IMAGE_H_
