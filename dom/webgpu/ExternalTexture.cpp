/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ExternalTexture.h"

#include "Queue.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/VideoFrame.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/webgpu/WebGPUParent.h"
#include "mozilla/webgpu/WebGPUChild.h"
#include "mozilla/webgpu/Buffer.h"
#include "mozilla/webgpu/Texture.h"
#include "mozilla/webgpu/ffi/wgpu.h"

#ifdef XP_WIN
#  include "mozilla/webgpu/ExternalTextureD3D11.h"
#endif

#if defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID)
#  include "mozilla/webgpu/ExternalTextureDMABuf.h"
#endif

#ifdef XP_MACOSX
#  include "mozilla/webgpu/ExternalTextureMacIOSurface.h"
#endif

namespace mozilla::webgpu {

GPU_IMPL_CYCLE_COLLECTION(ExtTex, mParent)

/* static */ already_AddRefed<ExtTex> ExtTex::CreateFromVideoFrame(
    Device* const aParent, dom::VideoFrame& aVideoFrame) {
  RawId id = ffi::wgpu_client_make_external_texture_id(
      aParent->GetBridge()->GetClient());
  RefPtr<ExtTex> extTex = new ExtTex(aParent, id);

  RefPtr<layers::Image> image = aVideoFrame.GetImage();
  extTex->Init(image);
  return extTex.forget();
}

/* static */ already_AddRefed<ExtTex> ExtTex::CreateFromHTMLVideoElement(
    Device* const aParent, dom::HTMLVideoElement& aVideoElement) {
  RawId id = ffi::wgpu_client_make_external_texture_id(
      aParent->GetBridge()->GetClient());
  RefPtr<ExtTex> extTex = new ExtTex(aParent, id);

  RefPtr<layers::Image> image = aVideoElement.GetCurrentImage();
  extTex->Init(image);
  return extTex.forget();
}

void ExtTex::Init(layers::Image* aImage) {
  if (!aImage) {
    printf_stderr("jamiedbg Image is null\n");
    return;
  }

  Maybe<layers::SurfaceDescriptor> desc = aImage->GetDesc();
  if (!desc) {
    printf_stderr("jamiedbg Failed to get SurfaceDescriptor from Image\n");
    return;
  }
  switch (desc->type()) {
    case layers::SurfaceDescriptor::TSurfaceDescriptorGPUVideo: {
      printf_stderr("jamiedbg TSurfaceDescriptorGPUVideo\n");
      layers::SurfaceDescriptorGPUVideo gpuVideoDesc =
          desc->get_SurfaceDescriptorGPUVideo();
      if (gpuVideoDesc.type() !=
          layers::SurfaceDescriptorGPUVideo::TSurfaceDescriptorRemoteDecoder) {
        printf_stderr(
            "jamiedbg gpuVideoDesc type %d is not "
            "TSurfaceDescriptorRemoteDecoder\n",
            gpuVideoDesc.type());
        return;
      }
      layers::SurfaceDescriptorRemoteDecoder remoteDecoderDesc =
          gpuVideoDesc.get_SurfaceDescriptorRemoteDecoder();
      layers::RemoteDecoderVideoSubDescriptor subDesc =
          remoteDecoderDesc.subdesc();
      switch (subDesc.type()) {
        case layers::RemoteDecoderVideoSubDescriptor::Tnull_t: {
          printf_stderr("jamiedbg subDesc type is Tnull\n");
          mParent->GetBridge()->SendDeviceCreateExternalTexture(
              mParent->mId, mParent->GetQueue()->mId, mId, *desc, mPlane0Id,
              mPlane1Id, mPlane2Id);
          break;
        }
        default:
          printf_stderr(
              "jamiedbg Unsupported RemoteDecoderVideoSubDescriptor type %d\n",
              subDesc.type());
          return;
      }
      break;
    }
    default:
      printf_stderr("jamiedbg Unsupported SurfaceDescriptor type %d\n",
                    desc->type());
      break;
  }
}

ExtTex::ExtTex(Device* const aParent, RawId aId) : ChildOf(aParent), mId(aId) {
  mPlane0Id =
      ffi::wgpu_client_make_texture_id(aParent->GetBridge()->GetClient());
  mPlane1Id =
      ffi::wgpu_client_make_texture_id(aParent->GetBridge()->GetClient());
  mPlane2Id =
      ffi::wgpu_client_make_texture_id(aParent->GetBridge()->GetClient());
}

// FIXME: cleanup
ExtTex::~ExtTex() = default;

JSObject* ExtTex::WrapObject(JSContext* cx, JS ::Handle<JSObject*> givenProto) {
  return dom::GPUExternalTexture_Binding::Wrap(cx, this, givenProto);
}

// static
UniquePtr<ExternalTexture> ExternalTexture::Create(
    WebGPUParent* aParent, const ffi::WGPUDeviceId aDeviceId,
    const uint32_t aWidth, const uint32_t aHeight,
    const struct ffi::WGPUTextureFormat aFormat,
    const ffi::WGPUTextureUsages aUsage) {
  MOZ_ASSERT(aParent);

  UniquePtr<ExternalTexture> texture;
#ifdef XP_WIN
  texture = ExternalTextureD3D11::Create(aParent, aDeviceId, aWidth, aHeight,
                                         aFormat, aUsage);
#elif defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID)
  texture = ExternalTextureDMABuf::Create(aParent, aDeviceId, aWidth, aHeight,
                                          aFormat, aUsage);
#elif defined(XP_MACOSX)
  texture = ExternalTextureMacIOSurface::Create(aParent, aDeviceId, aWidth,
                                                aHeight, aFormat, aUsage);
#endif
  return texture;
}

ExternalTexture::ExternalTexture(const uint32_t aWidth, const uint32_t aHeight,
                                 const struct ffi::WGPUTextureFormat aFormat,
                                 const ffi::WGPUTextureUsages aUsage)
    : mWidth(aWidth), mHeight(aHeight), mFormat(aFormat), mUsage(aUsage) {}

ExternalTexture::~ExternalTexture() {}

void ExternalTexture::SetSubmissionIndex(uint64_t aSubmissionIndex) {
  MOZ_ASSERT(aSubmissionIndex != 0);

  mSubmissionIndex = aSubmissionIndex;
}

UniquePtr<ExternalTextureReadBackPresent>
ExternalTextureReadBackPresent::Create(
    const uint32_t aWidth, const uint32_t aHeight,
    const struct ffi::WGPUTextureFormat aFormat,
    const ffi::WGPUTextureUsages aUsage) {
  return MakeUnique<ExternalTextureReadBackPresent>(aWidth, aHeight, aFormat,
                                                    aUsage);
}

ExternalTextureReadBackPresent::ExternalTextureReadBackPresent(
    const uint32_t aWidth, const uint32_t aHeight,
    const struct ffi::WGPUTextureFormat aFormat,
    const ffi::WGPUTextureUsages aUsage)
    : ExternalTexture(aWidth, aHeight, aFormat, aUsage) {}

ExternalTextureReadBackPresent::~ExternalTextureReadBackPresent() {}

}  // namespace mozilla::webgpu
