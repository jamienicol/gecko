/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ExternalTexture.h"

#include "GPUVideoImage.h"
#include "mozilla/layers/GPUVideoTextureClient.h"
#include "Queue.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/VideoFrame.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/VideoBridgeUtils.h"
#include "mozilla/webgpu/WebGPUParent.h"
#include "mozilla/webgpu/Buffer.h"
#include "mozilla/webgpu/Texture.h"
#include "mozilla/webgpu/ffi/wgpu.h"
#include "mozilla/ToString.h"

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

/* static */ already_AddRefed<ExtTex> CreateFromImage(Device* const aParent,
                                                      layers::Image& aImage) {
  dom::GPUTextureDescriptor texDesc;
  texDesc.mLabel = u"ext-tex"_ns;
  dom::OwningRangeEnforcedUnsignedLongSequenceOrGPUExtent3DDict size;
  (void)size.SetAsGPUExtent3DDict();
  size.GetAsGPUExtent3DDict().mWidth = aImage.GetSize().width;
  size.GetAsGPUExtent3DDict().mHeight = aImage.GetSize().height;
  size.GetAsGPUExtent3DDict().mDepthOrArrayLayers = 1;
  texDesc.mSize = size;
  texDesc.mMipLevelCount = 1;
  texDesc.mSampleCount = 1;
  texDesc.mFormat = dom::GPUTextureFormat::Bgra8unorm;
  texDesc.mUsage =
      WGPUTextureUsages_TEXTURE_BINDING | WGPUTextureUsages_COPY_DST;
  // texDesc.mViewFormats = Sequence();
  RefPtr<Texture> tex = aParent->CreateTexture(texDesc);

  if (layers::GPUVideoImage* videoImage = aImage.AsGPUVideoImage()) {
    // On my linux desktop the internal image type is PlanarYCbCrImage.
    // On the webgpu samples videoUpload video at least.

    printf_stderr("CreateFromImage() GPUVideoImage\n");
    Maybe<layers::SurfaceDescriptor> desc = videoImage->GetDesc();
    if (!desc) {
      printf_stderr("jamiedbg failed to get desc\n");
    }
    if (desc->type() != layers::SurfaceDescriptor::TSurfaceDescriptorGPUVideo) {
      printf_stderr("jamiedbg unexpected descriptor type\n");
    }
    layers::SurfaceDescriptorGPUVideo& gpuDesc =
        desc->get_SurfaceDescriptorGPUVideo();
    layers::SurfaceDescriptorRemoteDecoder& remoteDesc =
        gpuDesc.get_SurfaceDescriptorRemoteDecoder();
    uint64_t handle = remoteDesc.handle();
    Maybe<layers::VideoBridgeSource> source = remoteDesc.source();
    // FIXME: there's an id field too. is it useful?
    aParent->GetBridge()->SendCreateExternalTexture(aParent->mId, handle,
                                                    source);
    printf_stderr("jamiedbg Remote Image handle: %" PRIu64 "\n", handle);
  } else if (aImage.AsDMABUFSurfaceImage()) {
    printf_stderr("CreateFromImage() DMABUFSurfaceImage\n");
  } else {
    printf_stderr("CreateFromImage() other image type\n");
  }
  RefPtr<gfx::SourceSurface> surface = aImage.GetAsSourceSurface();
  RefPtr<gfx::DataSourceSurface> dataSurface =
      surface ? surface->GetDataSurface() : nullptr;
  if (dataSurface) {
    printf_stderr("jamiedbg got source surface. isdata: %d\n",
                  surface->IsDataSourceSurface());
    gfx::DataSourceSurface::ScopedMap surface_map(dataSurface.get(),
                                                  gfx::DataSourceSurface::READ);

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
    printf_stderr("jamiedbg failed to get data source surface\n");
  }

  RefPtr<ExtTex> ext = new ExtTex(aParent, tex);
  return ext.forget();
}

/* static */ already_AddRefed<ExtTex> ExtTex::CreateFromVideoFrame(
    Device* const aParent, dom::VideoFrame& aVideoFrame) {
  printf_stderr("jamiedbg ExtTex::CreateFromVideoFrame()\n");

  RefPtr<layers::Image> image = aVideoFrame.GetImage();
  return CreateFromImage(aParent, *image);
}

/* static */ already_AddRefed<ExtTex> ExtTex::CreateFromHTMLVideoElement(
    Device* const aParent, dom::HTMLVideoElement& aVideoElement) {
  printf_stderr("jamiedbg ExtTex::CreateFromHTMLVideoElement()\n");

  RefPtr<layers::Image> image = aVideoElement.GetCurrentImage();
  return CreateFromImage(aParent, *image);
}

ExtTex::ExtTex(Device* const aParent, RefPtr<Texture> aPlane0)
    : ChildOf(aParent), mPlane0(aPlane0) {
  dom::GPUTextureViewDescriptor viewDesc;
  viewDesc.mAspect = dom::GPUTextureAspect::All;
  viewDesc.mBaseArrayLayer = 0;
  viewDesc.mBaseMipLevel = 0;

  if (mPlane0) {
    mPlane0View = mPlane0->CreateView(viewDesc);
  }
  if (mPlane1) {
    mPlane1View = mPlane1->CreateView(viewDesc);
  }
  if (mPlane2) {
    mPlane2View = mPlane2->CreateView(viewDesc);
  }

  dom::GPUBufferDescriptor bufferDesc;
  bufferDesc.mUsage = WGPUBufferUsages_UNIFORM | WGPUBufferUsages_COPY_DST;
  bufferDesc.mMappedAtCreation = false;
  bufferDesc.mSize = 4;
  ErrorResult res;
  mParamsBuffer = aParent->CreateBuffer(bufferDesc, res);
  // aParent->GetQueue()->WriteBuffer(*mParamsBuffer.get(), 0, const
  // dom::ArrayBufferViewOrArrayBuffer &aData, uint64_t aDataOffset, const
  // dom::Optional<uint64_t> &aSize, ErrorResult &aRv)
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
