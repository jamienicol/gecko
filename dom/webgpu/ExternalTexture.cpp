/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ExternalTexture.h"
#include <cstring>

#include "Queue.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/dom/VideoFrame.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/webgpu/WebGPUParent.h"
#include "mozilla/webgpu/Texture.h"

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
  printf_stderr(
      "jamiedbg ExtTex::CreateFromVideoFrame() coded: %dx%d, display: %dx%d\n",
      aVideoFrame.CodedWidth(), aVideoFrame.CodedHeight(),
      aVideoFrame.DisplayWidth(), aVideoFrame.DisplayHeight());

  RefPtr<layers::Image> image = aVideoFrame.GetImage();
  RefPtr<gfx::SourceSurface> surface = image->GetAsSourceSurface();

  dom::GPUTextureDescriptor texDesc;
  texDesc.mLabel = u"ext-tex"_ns;
  dom::OwningRangeEnforcedUnsignedLongSequenceOrGPUExtent3DDict size;
  (void)size.SetAsGPUExtent3DDict();
  size.GetAsGPUExtent3DDict().mWidth = aVideoFrame.CodedWidth();
  size.GetAsGPUExtent3DDict().mHeight = aVideoFrame.CodedHeight();
  size.GetAsGPUExtent3DDict().mDepthOrArrayLayers = 1;
  texDesc.mSize = size;
  texDesc.mMipLevelCount = 1;
  texDesc.mSampleCount = 1;
  texDesc.mFormat = dom::GPUTextureFormat::Bgra8unorm;
  texDesc.mUsage = WGPUTextureUsages_TEXTURE_BINDING | WGPUTextureUsages_COPY_DST;
  // texDesc.mViewFormats = Sequence();
  RefPtr<Texture> tex = aParent->CreateTexture(texDesc);

  if (surface) {
    printf_stderr("jamiedbg got source surface. isdata: %d\n",
                  surface->IsDataSourceSurface());
    RefPtr<gfx::DataSourceSurface> dataSurface = surface->GetDataSurface();
    gfx::DataSourceSurface::ScopedMap surface_map(dataSurface.get(),
                                                  gfx::DataSourceSurface::READ);
    // for (int y = 0; y < 8; y++) {
    //   for (int x = 0; x < 8; x++) {
    //     printf_stderr(
    //         "jamiedbg pixel %d,%d: %f,%f,%f,%f\n", x, y,
    //         static_cast<float>(surface_map.GetData()[y *
    //         surface_map.GetStride() + x * 4 + 0]) /
    //             255.0,
    //         static_cast<float>(surface_map.GetData()[y *
    //         surface_map.GetStride() + x * 4 + 1]) /
    //             255.0,
    //         static_cast<float>(surface_map.GetData()[y *
    //         surface_map.GetStride() + x * 4 + 2]) /
    //             255.0,
    //         static_cast<float>(surface_map.GetData()[y *
    //         surface_map.GetStride() + x * 4 + 3]) /
    //             255.0);
    //   }
    // }

    auto shmem_handle = mozilla::ipc::shared_memory::Create(
        surface_map.GetStride() * surface->GetSize().height);
    auto shmem_map = shmem_handle.Map();
    std::memcpy(shmem_map.DataAs<uint8_t>(), surface_map.GetData(),
                shmem_handle.Size());

    ipc::ByteBuf bb;
    ffi::WGPUTexelCopyTextureInfo info = {
        .texture = tex->mId,
        .mip_level = 0,
        .origin =
            {
                .x = 0,
                .y = 0,
                .z = 0,
            },
        .aspect = ffi::WGPUTextureAspect::WGPUTextureAspect_All,
    };
    uint32_t stride = surface_map.GetStride();
    uint32_t height = surface->GetSize().height;
    ffi::WGPUTexelCopyBufferLayout layout = {
        .offset = 0,
        .bytes_per_row = &stride,
        .rows_per_image = &height,
    };
    ffi::WGPUExtent3d size = {
        .width = tex->Width(),
        .height = tex->Height(),
        .depth_or_array_layers = 1,
    };
    ffi::wgpu_queue_write_texture(info, layout, size, ToFFI(&bb));
    aParent->GetBridge()->SendQueueWriteAction(aParent->GetQueue()->mId,
                                               aParent->mId, std::move(bb),
                                               std::move(shmem_handle));
  } else {
    printf_stderr("jamiedbg failed to get source surface\n");
  }

  RefPtr<ExtTex> ext = new ExtTex(aParent, tex);
  return ext.forget();
}

ExtTex::ExtTex(Device* const aParent, RefPtr<Texture> aPlane0)
    : ChildOf(aParent), mPlane0(aPlane0) {
  if (mPlane0) {
    dom::GPUTextureViewDescriptor desc = {};
    // desc.mArrayLayerCount =
    desc.mAspect = dom::GPUTextureAspect::All;
    desc.mBaseArrayLayer = 0;
    desc.mBaseMipLevel = 0;
    mPlane0View = mPlane0->CreateView(desc);
  }
}

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
