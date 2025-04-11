/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ExternalTexture_H_
#define ExternalTexture_H_

#include "mozilla/AlreadyAddRefed.h"
#include "ObjectModel.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/webgpu/ffi/wgpu.h"
#include "mozilla/webgpu/WebGPUTypes.h"

namespace mozilla {
namespace webgpu {
class Buffer;
class Device;
class Texture;
class TextureView;
}  // namespace webgpu
namespace dom {
class HTMLVideoElement;
class VideoFrame;
}  // namespace dom
namespace layers {
class Image;
}
namespace ipc {
class Shmem;
}

namespace webgpu {

// NOTE: Incomplete, and needs to be reconciled with the existing
// `ExternalTexture`, which is used by and for internals that handle compositor
// textures.
//
// Follow-up to complete implementation is at
// <https://bugzilla.mozilla.org/show_bug.cgi?id=1827116>.
class ExtTex : public ObjectBase, public ChildOf<Device> {
 public:
  GPU_DECL_CYCLE_COLLECTION(ExtTex)
  GPU_DECL_JS_WRAP(ExtTex)

  explicit ExtTex(Device* const aParent, RawId aId);
  Device* GetDevice() { return mParent; }
  const RawId mId;

  static already_AddRefed<ExtTex> CreateFromVideoFrame(
      Device* const aParent, dom::VideoFrame& aVideoFrame);
  static already_AddRefed<ExtTex> CreateFromHTMLVideoElement(
      Device* const aParent, dom::HTMLVideoElement& aVideoElement);

  RawId mPlane0Id;
  RawId mPlane1Id;
  RawId mPlane2Id;
  // RefPtr<Texture> mPlane0;
  // RefPtr<TextureView> mPlane0View;
  // RefPtr<Texture> mPlane1;
  // RefPtr<TextureView> mPlane1View;
  // RefPtr<Texture> mPlane2;
  // RefPtr<TextureView> mPlane2View;
  // RefPtr<Buffer> mParamsBuffer;

 private:
  ~ExtTex();
  void Cleanup() {}
  void Init(layers::Image* aImage);
};

class ExternalTextureD3D11;
class ExternalTextureDMABuf;
class ExternalTextureMacIOSurface;
class WebGPUParent;

// A texture that can be used by the WebGPU implementation but is created and
// owned by Gecko
class ExternalTexture {
 public:
  static UniquePtr<ExternalTexture> Create(
      WebGPUParent* aParent, const ffi::WGPUDeviceId aDeviceId,
      const uint32_t aWidth, const uint32_t aHeight,
      const struct ffi::WGPUTextureFormat aFormat,
      const ffi::WGPUTextureUsages aUsage);

  ExternalTexture(const uint32_t aWidth, const uint32_t aHeight,
                  const struct ffi::WGPUTextureFormat aFormat,
                  const ffi::WGPUTextureUsages aUsage);
  virtual ~ExternalTexture();

  virtual Maybe<layers::SurfaceDescriptor> ToSurfaceDescriptor() = 0;

  virtual void GetSnapshot(const ipc::Shmem& aDestShmem,
                           const gfx::IntSize& aSize) {}

  virtual ExternalTextureDMABuf* AsExternalTextureDMABuf() { return nullptr; }

  virtual ExternalTextureMacIOSurface* AsExternalTextureMacIOSurface() {
    return nullptr;
  }

  virtual ExternalTextureD3D11* AsExternalTextureD3D11() { return nullptr; }

  gfx::IntSize GetSize() { return gfx::IntSize(mWidth, mHeight); }

  void SetSubmissionIndex(uint64_t aSubmissionIndex);
  uint64_t GetSubmissionIndex() const { return mSubmissionIndex; }

  void SetOwnerId(const layers::RemoteTextureOwnerId aOwnerId) {
    mOwnerId = aOwnerId;
  }
  layers::RemoteTextureOwnerId GetOwnerId() const {
    MOZ_ASSERT(mOwnerId.IsValid());
    return mOwnerId;
  }

  virtual void onBeforeQueueSubmit(RawId aQueueId) {}

  const uint32_t mWidth;
  const uint32_t mHeight;
  const struct ffi::WGPUTextureFormat mFormat;
  const ffi::WGPUTextureUsages mUsage;

 protected:
  uint64_t mSubmissionIndex = 0;
  layers::RemoteTextureOwnerId mOwnerId;
};

// Dummy class
class ExternalTextureReadBackPresent final : public ExternalTexture {
 public:
  static UniquePtr<ExternalTextureReadBackPresent> Create(
      const uint32_t aWidth, const uint32_t aHeight,
      const struct ffi::WGPUTextureFormat aFormat,
      const ffi::WGPUTextureUsages aUsage);

  ExternalTextureReadBackPresent(const uint32_t aWidth, const uint32_t aHeight,
                                 const struct ffi::WGPUTextureFormat aFormat,
                                 const ffi::WGPUTextureUsages aUsage);
  virtual ~ExternalTextureReadBackPresent();

  Maybe<layers::SurfaceDescriptor> ToSurfaceDescriptor() override {
    return Nothing();
  }
};

}  // namespace webgpu
}  // namespace mozilla

#endif  // GPU_ExternalTexture_H_
