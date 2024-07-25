/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/NativeLayerAndroid.h"

#include "mozilla/webrender/RenderAndroidImageReaderTextureHost.h"

namespace mozilla::layers {

NativeLayerRootAndroid::NativeLayerRootAndroid()
    : mMonitor("NativeLayerRootAndroid") {}

already_AddRefed<NativeLayer> NativeLayerRootAndroid::CreateLayer(
    const gfx::IntSize& aSize, bool aIsOpaque,
    SurfacePoolHandle* aSurfacePoolHandle) {
  // FIXME: pool surface controls
  const auto* api = AndroidSurfaceControlApi::Get();
  UniquePtr<ASurfaceControl> surfaceControl = WrapUnique(
      api->ASurfaceControl_create(mSurfaceControl.get(), "NativeLayer"));
  if (!surfaceControl) {
    gfxCriticalError() << "Failed to create child SurfaceControl";
    return nullptr;
  }

  RefPtr<NativeLayerAndroid> layer =
      new NativeLayerAndroid(std::move(surfaceControl), aSize, aIsOpaque,
                             aSurfacePoolHandle->AsSurfacePoolHandleAndroid());

  return layer.forget();
}

already_AddRefed<NativeLayer>
NativeLayerRootAndroid::CreateLayerForExternalTexture(bool aIsOpaque) {
  const auto* api = AndroidSurfaceControlApi::Get();
  UniquePtr<ASurfaceControl> surfaceControl = WrapUnique(
      api->ASurfaceControl_create(mSurfaceControl.get(), "NativeLayer"));
  if (!surfaceControl) {
    gfxCriticalError() << "Failed to create child SurfaceControl";
    return nullptr;
  }

  RefPtr<NativeLayer> layer =
      new NativeLayerAndroid(std::move(surfaceControl), aIsOpaque);
  return layer.forget();
}

already_AddRefed<NativeLayer> NativeLayerRootAndroid::CreateLayerForColor(
    gfx::DeviceColor aColor) {
  return nullptr;
}

void NativeLayerRootAndroid::AppendLayer(NativeLayer* aLayer) {
  MonitorAutoLock lock(mMonitor);
  MOZ_RELEASE_ASSERT(false);
};

void NativeLayerRootAndroid::RemoveLayer(NativeLayer* aLayer) {
  MonitorAutoLock lock(mMonitor);
  MOZ_RELEASE_ASSERT(false);
};

void NativeLayerRootAndroid::SetLayers(
    const nsTArray<RefPtr<NativeLayer>>& aLayers) {
  MonitorAutoLock lock(mMonitor);
  nsTArray<RefPtr<NativeLayerAndroid>> newSublayers(aLayers.Length());
  for (const RefPtr<NativeLayer>& sublayer : aLayers) {
    RefPtr<NativeLayerAndroid> layer = sublayer->AsNativeLayerAndroid();
    newSublayers.AppendElement(layer);
  }

  if (newSublayers != mSublayers) {
    for (const RefPtr<NativeLayerAndroid>& layer : mSublayers) {
      if (!newSublayers.Contains(layer)) {
        mOldSublayers.AppendElement(layer);
      }
    }
    mSublayers = std::move(newSublayers);
    mMutatedLayers = true;
  }
};

void NativeLayerRootAndroid::PrepareForCommit() {
  MonitorAutoLock lock(mMonitor);
}

bool NativeLayerRootAndroid::CommitToScreen() {
  MonitorAutoLock lock(mMonitor);
  while (mPendingCommit) {
    if (lock.Wait(TimeDuration::FromMilliseconds(1000)) == CVStatus::Timeout) {
      printf_stderr("jamiedbg Timeout waiting for pending commit\n");
      return false;
    }
  }

  const auto* api = AndroidSurfaceControlApi::Get();
  ASurfaceTransaction* transaction = api->ASurfaceTransaction_create();

  // FIXME: setColor() for compositor's clear color?

  std::unordered_map<ASurfaceControl*, ReleasedBuffer> releasedBuffers;

  if (mMutatedLayers) {
    for (auto& layer : mOldSublayers) {
      Maybe<NativeLayerAndroidBufferSource> frontBuffer;
      layer->Remove(transaction, frontBuffer);
      if (frontBuffer) {
        // Using raw ASurfaceControl* as key is safe, as it is kept alive by the
        // mLayer member in the value.
        releasedBuffers.insert({layer->mSurfaceControl.get(),
                                ReleasedBuffer{
                                    .mLayer = layer,
                                    .mSurface = frontBuffer.extract(),
                                }});
      }
    }
    mOldSublayers.Clear();
    mMutatedLayers = false;
  }

  // FIXME: do a pass of newly added layers, so we only have to reparent those
  // ones here instead of in Update()

  int zOrder = 0;
  for (auto& layer : mSublayers) {
    Maybe<NativeLayerAndroidBufferSource> prevFrontBuffer;
    layer->Update(transaction, mSurfaceControl.get(), zOrder++,
                  mLayersRenderedFence, prevFrontBuffer);
    if (prevFrontBuffer) {
      // Using raw ASurfaceControl* as key is safe, as it is kept alive by the
      // mLayer member in the value.
      releasedBuffers.insert({layer->mSurfaceControl.get(),
                              ReleasedBuffer{
                                  .mLayer = layer,
                                  .mSurface = prevFrontBuffer.extract(),
                              }});
    }
  }

  mLayersRenderedFence.apply(::close).reset();

  mReleasedBuffers.push(std::move(releasedBuffers));

  AddRef();
  api->ASurfaceTransaction_setOnComplete(
      transaction, this, [](void* context, ASurfaceTransactionStats* stats) {
        NativeLayerRootAndroid* root = (NativeLayerRootAndroid*)context;
        root->OnTransactionComplete(stats);
        root->Release();
      });

  AddRef();
  api->ASurfaceTransaction_setOnCommit(
      transaction, this, [](void* context, ASurfaceTransactionStats* stats) {
        NativeLayerRootAndroid* root = (NativeLayerRootAndroid*)context;
        root->OnTransactionCommit(stats);
        root->Release();
      });

  api->ASurfaceTransaction_apply(transaction);
  api->ASurfaceTransaction_delete(transaction);

  mPendingCommit = true;

  return true;
}

UniquePtr<NativeLayerRootSnapshotter>
NativeLayerRootAndroid::CreateSnapshotter() {
  MonitorAutoLock lock(mMonitor);
  return nullptr;
}

void NativeLayerRootAndroid::SetLayersRenderedFence(int aFence) {
  MonitorAutoLock lock(mMonitor);
  // This must only be called once per frame, after the OpenGL commands to
  // render every layer have been submitted. CommitToScreen() will then take
  // this value, meaning it will be Nothing again next time this function is
  // called.
  mLayersRenderedFence.emplace(aFence);
}

void NativeLayerRootAndroid::OnTransactionCommit(
    ASurfaceTransactionStats* stats) {
  MonitorAutoLock lock(mMonitor);
  mPendingCommit = false;
  lock.NotifyAll();
}

void NativeLayerRootAndroid::OnTransactionComplete(
    ASurfaceTransactionStats* stats) {
  MonitorAutoLock lock(mMonitor);

  const auto* api = AndroidSurfaceControlApi::Get();

  std::unordered_map<ASurfaceControl*, ReleasedBuffer> releasedBuffers =
      std::move(mReleasedBuffers.front());
  mReleasedBuffers.pop();

  ASurfaceControl** surfaceControls;
  size_t numSurfaceControls;
  api->ASurfaceTransactionStats_getASurfaceControls(stats, &surfaceControls,
                                                    &numSurfaceControls);

  for (size_t i = 0; i < numSurfaceControls; i++) {
    ASurfaceControl* const sc = surfaceControls[i];

    const int releaseFence =
        api->ASurfaceTransactionStats_getPreviousReleaseFenceFd(stats, sc);

    const auto& releasedBuffer = releasedBuffers.find(sc);
    if (releasedBuffer != releasedBuffers.end()) {
      releasedBuffer->second.mSurface.match(
          [&](UniquePtr<HardwareBufferSurface>& surface) {
            surface->OnRelease(releaseFence);
            releasedBuffer->second.mLayer->mSurfacePoolHandle
                ->ReturnSurfaceToPool(std::move(surface));
          },
          [=](RefPtr<AndroidImage>& image) {
            // This should drop the final reference to the image, causing it to
            // be deleted

            if (releaseFence != -1) {
              ::close(releaseFence);
            }
          });
      releasedBuffers.erase(sc);
    } else {
      MOZ_ASSERT(releaseFence == -1,
                 "jamiedbg No ReleasedBuffer entry found for released buffer");
      if (releaseFence != -1) {
        ::close(releaseFence);
      }
    }
  }

  // Ensure that we handled all buffers that were released in this transaction.
  for (const auto& it : releasedBuffers) {
    it.second.mSurface.match(
        [&](const UniquePtr<HardwareBufferSurface>& surface) {
          printf_stderr(
              "jamiedbg Unhandled released buffer: nativeLayer: %p, sc: %p, "
              "surface: %p\n",
              it.second.mLayer.get(), it.first, surface.get());
        },
        [&](const RefPtr<AndroidImage>& image) {
          printf_stderr(
              "jamiedbg Unhandled released buffer: nativeLayer: %p, sc: %p, "
              "image: %p\n",
              it.second.mLayer.get(), it.first, image.get());
        });
  }
  MOZ_ASSERT(releasedBuffers.empty());

  api->ASurfaceTransactionStats_releaseASurfaceControls(surfaceControls);
}

bool NativeLayerRootAndroid::Attach(ANativeWindow* aNativeWindow) {
  MonitorAutoLock lock(mMonitor);
  auto api = AndroidSurfaceControlApi::Get();
  mSurfaceControl = WrapUnique(
      api->ASurfaceControl_createFromWindow(aNativeWindow, "NativeLayerRoot"));
  if (!mSurfaceControl) {
    gfxCriticalError() << "Failed to create SurfaceControl from NativeWindow";
    return false;
  }

  return true;
}

void NativeLayerRootAndroid::Detach() {
  MonitorAutoLock lock(mMonitor);
  mSurfaceControl = nullptr;
}

static bool BufferSourceHasBuffer(
    const NativeLayerAndroidBufferSource& aBufferSource) {
  return aBufferSource.match(
      [](const UniquePtr<HardwareBufferSurface>& surface) {
        return surface != nullptr;
      },
      [](const RefPtr<AndroidImage>& image) { return image != nullptr; });
}

std::ostream& operator<<(std::ostream& aStream,
                         const NativeLayerAndroidBufferSource& aBufferSource) {
  aBufferSource.match(
      [&](const UniquePtr<HardwareBufferSurface>& surface) {
        aStream << "HardwareBufferSurface [" << surface.get() << "]";
      },
      [&](const RefPtr<AndroidImage>& image) {
        aStream << "AndroidImage [" << image.get() << "]";
      });
  return aStream;
}

NativeLayerAndroid::NativeLayerAndroid(
    UniquePtr<ASurfaceControl>&& aSurfaceControl, const gfx::IntSize& aSize,
    bool aIsOpaque, SurfacePoolHandleAndroid* aSurfacePoolHandle)
    : mMutex("NativeLayerAndroid"),
      mSurfaceControl(std::move(aSurfaceControl)),
      mSize(aSize),
      mIsOpaque(aIsOpaque),
      mSurfacePoolHandle(aSurfacePoolHandle),
      mFrontBuffer(WrapUnique<HardwareBufferSurface>(nullptr)),
      mPrevFrontBuffer(WrapUnique<HardwareBufferSurface>(nullptr)) {}

NativeLayerAndroid::NativeLayerAndroid(
    UniquePtr<ASurfaceControl>&& aSurfaceControl, bool aIsOpaque)
    : mMutex("NativeLayerAndroid"),
      mSurfaceControl(std::move(aSurfaceControl)),
      mIsOpaque(aIsOpaque),
      mFrontBuffer(RefPtr<AndroidImage>(nullptr)),
      mPrevFrontBuffer(RefPtr<AndroidImage>(nullptr)) {}

NativeLayerAndroid::~NativeLayerAndroid() {
  MOZ_ASSERT(!BufferSourceHasBuffer(mFrontBuffer));
  MOZ_ASSERT(!BufferSourceHasBuffer(mPrevFrontBuffer));
  MOZ_ASSERT(!mInProgressBuffer);
}

gfx::IntSize NativeLayerAndroid::GetSize() {
  MutexAutoLock lock(mMutex);
  return mSize;
}

bool NativeLayerAndroid::IsOpaque() {
  MutexAutoLock lock(mMutex);
  return mIsOpaque;
}

void NativeLayerAndroid::SetPosition(const gfx::IntPoint& aPosition) {
  MutexAutoLock lock(mMutex);
  mPosition = aPosition;
}

gfx::IntPoint NativeLayerAndroid::GetPosition() {
  MutexAutoLock lock(mMutex);
  return mPosition;
}

void NativeLayerAndroid::SetTransform(const gfx::Matrix4x4& aTransform) {
  MutexAutoLock lock(mMutex);
  mTransform = aTransform;
}

gfx::Matrix4x4 NativeLayerAndroid::GetTransform() {
  MutexAutoLock lock(mMutex);
  return mTransform;
}

gfx::IntRect NativeLayerAndroid::GetRect() {
  MutexAutoLock lock(mMutex);
  return gfx::IntRect(mPosition, mSize);
}

void NativeLayerAndroid::SetClipRect(const Maybe<gfx::IntRect>& aClipRect) {
  MutexAutoLock lock(mMutex);
  mClipRect = aClipRect;
}

Maybe<gfx::IntRect> NativeLayerAndroid::ClipRect() {
  MutexAutoLock lock(mMutex);
  return mClipRect;
}

gfx::IntRect NativeLayerAndroid::CurrentSurfaceDisplayRect() {
  MutexAutoLock lock(mMutex);
  return mDisplayRect;
}

void NativeLayerAndroid::SetSurfaceIsFlipped(bool aIsFlipped) {
  MutexAutoLock lock(mMutex);
  mSurfaceIsFlipped = aIsFlipped;
}

bool NativeLayerAndroid::SurfaceIsFlipped() {
  MutexAutoLock lock(mMutex);
  return mSurfaceIsFlipped;
}

void NativeLayerAndroid::SetSamplingFilter(
    gfx::SamplingFilter aSamplingFilter) {
  MutexAutoLock lock(mMutex);
  mSamplingFilter = aSamplingFilter;
}

RefPtr<gfx::DrawTarget> NativeLayerAndroid::NextSurfaceAsDrawTarget(
    const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
    gfx::BackendType aBackendType) {
  MutexAutoLock lock(mMutex);
  mDisplayRect = aDisplayRect;
  mDirtyRegion = aUpdateRegion;

  MOZ_ASSERT(!mInProgressBuffer);
  auto& frontBuffer = mFrontBuffer.as<UniquePtr<HardwareBufferSurface>>();
  if (frontBuffer && !frontBuffer->IsAttached()) {
    printf_stderr("jamiedbg Reusing non-attached frontbuffer");
    // FIXME: can this ever happen? Shouldn't we always be attached?
    // The layer's front buffer has not been attached, so we can re-use it
    mInProgressBuffer = std::move(frontBuffer);
  } else {
    mInProgressBuffer = mSurfacePoolHandle->ObtainSurfaceFromPool(mSize);
  }

  if (!mInProgressBuffer) {
    gfxCriticalError() << "Failed to obtain buffer";
    wr::RenderThread::Get()->HandleWebRenderError(
        wr::WebRenderError::NEW_SURFACE);
    return nullptr;
  }

  if (frontBuffer) {
    HandlePartialUpdate(lock);
  }

  return mInProgressBuffer->WriteLock();
}

Maybe<GLuint> NativeLayerAndroid::NextSurfaceAsFramebuffer(
    const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
    bool aNeedsDepth) {
  MutexAutoLock lock(mMutex);
  mDisplayRect = aDisplayRect;
  mDirtyRegion = aUpdateRegion;

  MOZ_ASSERT(!mInProgressBuffer);
  auto& frontBuffer = mFrontBuffer.as<UniquePtr<HardwareBufferSurface>>();
  if (frontBuffer && !frontBuffer->IsAttached()) {
    // FIXME: can this ever happen? Shouldn't we always be attached?
    // The layer's front buffer has not been attached, so we can re-use it
    mInProgressBuffer = std::move(frontBuffer);
  } else {
    mInProgressBuffer = mSurfacePoolHandle->ObtainSurfaceFromPool(mSize);
  }

  if (!mInProgressBuffer) {
    gfxCriticalError() << "Failed to obtain buffer";
    wr::RenderThread::Get()->HandleWebRenderError(
        wr::WebRenderError::NEW_SURFACE);
    return Nothing();
  }

  // get the framebuffer before handling partial damage so we don't accidently
  // create one without depth buffer
  Maybe<GLuint> fbo = mSurfacePoolHandle->GetFramebufferForSurface(
      mInProgressBuffer.get(), aNeedsDepth);
  MOZ_RELEASE_ASSERT(fbo, "GetFramebufferForSurface failed.");

  if (frontBuffer) {
    HandlePartialUpdate(lock);
  }

  return fbo;
}

void NativeLayerAndroid::HandlePartialUpdate(
    const MutexAutoLock& aProofOfLock) {
  gfx::IntRegion copyRegion = gfx::IntRegion(mDisplayRect);
  copyRegion.SubOut(mDirtyRegion);

  if (!copyRegion.IsEmpty()) {
    auto& gl = mSurfacePoolHandle->gl();
    auto& frontBuffer = mFrontBuffer.as<UniquePtr<HardwareBufferSurface>>();
    if (gl) {
      gl->MakeCurrent();
      Maybe<GLuint> sourceFB = mSurfacePoolHandle->GetFramebufferForSurface(
          frontBuffer.get(), false);
      MOZ_RELEASE_ASSERT(sourceFB);
      Maybe<GLuint> destFB = mSurfacePoolHandle->GetFramebufferForSurface(
          mInProgressBuffer.get(), false);
      MOZ_RELEASE_ASSERT(destFB);
      for (auto iter = copyRegion.RectIter(); !iter.Done(); iter.Next()) {
        gfx::IntRect r = iter.Get();
        gl->BlitHelper()->BlitFramebufferToFramebuffer(*sourceFB, *destFB, r, r,
                                                       LOCAL_GL_NEAREST);
      }
    } else {
      RefPtr<gfx::DataSourceSurface> dataSourceSurface =
          frontBuffer->ReadLock();
      RefPtr<gfx::DrawTarget> dt = mInProgressBuffer->WriteLock();

      for (auto iter = copyRegion.RectIter(); !iter.Done(); iter.Next()) {
        gfx::IntRect r = iter.Get();
        dt->CopySurface(dataSourceSurface, r, r.TopLeft());
      }

      dataSourceSurface = nullptr;
      dt = nullptr;
      frontBuffer->Unlock();
      mInProgressBuffer->Unlock();
    }
  }
}

void NativeLayerAndroid::Update(
    ASurfaceTransaction* aTransaction, ASurfaceControl* aParent, int aZOrder,
    const Maybe<int>& aFence,
    Maybe<NativeLayerAndroidBufferSource>& aOutPrevFrontBuffer) {
  MutexAutoLock lock(mMutex);
  const auto* api = AndroidSurfaceControlApi::Get();

  // FIXME: reparent only newly added buffers in CommitToScreen instead of all
  // of them here.
  api->ASurfaceTransaction_reparent(aTransaction, mSurfaceControl.get(),
                                    aParent);

  // FIXME: instead of increasing this for each tile, can we set it based off
  // the surface zOrder?
  api->ASurfaceTransaction_setZOrder(aTransaction, mSurfaceControl.get(),
                                     aZOrder);

  if (mFrontBufferUpdated) {
    mFrontBufferUpdated = false;
    mFrontBuffer.match(
        [&](UniquePtr<HardwareBufferSurface>& surface) {
          int fence = -1;
          if (aFence.isSome()) {
            fence = ::dup(*aFence);
          }

          AHardwareBuffer* const hardwareBuffer =
              surface ? surface->GetBuffer() : nullptr;
          api->ASurfaceTransaction_setBuffer(
              aTransaction, mSurfaceControl.get(), hardwareBuffer, fence);

          surface->SetAttached();
        },
        [&](RefPtr<AndroidImage>& image) {
          // FIXME: do we need to get a fence from the image for the buffer?
          AHardwareBuffer* const hardwareBuffer =
              image ? image->GetHardwareBuffer() : nullptr;
          api->ASurfaceTransaction_setBuffer(
              aTransaction, mSurfaceControl.get(), hardwareBuffer, -1);
        });

    if (BufferSourceHasBuffer(mPrevFrontBuffer)) {
      aOutPrevFrontBuffer.emplace(std::move(mPrevFrontBuffer));
    }
  }

  // FIXME: handle all properties, then see if only setting mutated ones is
  // more efficient
  MOZ_RELEASE_ASSERT(mTransform.Is2D());
  auto transform2D = mTransform.As2D();

  gfx::Rect surfaceRectClipped = gfx::Rect(gfx::Point(), gfx::Size(mSize));
  surfaceRectClipped = surfaceRectClipped.Intersect(gfx::Rect(mDisplayRect));

  transform2D.PreTranslate(gfx::Point(mPosition));
  surfaceRectClipped = transform2D.TransformBounds(surfaceRectClipped);

  if (mClipRect) {
    surfaceRectClipped = surfaceRectClipped.Intersect(gfx::Rect(*mClipRect));
  }

  // FIXME: handle flip/rotate transforms

  // FIXME: set damage region

  auto transform2DInversed = transform2D.Inverse();
  gfx::Rect bufferClip =
      transform2DInversed.TransformBounds(surfaceRectClipped);

  if (surfaceRectClipped.IsEmpty() || bufferClip.IsEmpty()) {
    // We must explicitly hide the surface, as ASurfaceControl_setGeometry
    // does not support empty rects.
    api->ASurfaceTransaction_setVisibility(
        aTransaction, mSurfaceControl.get(),
        ASURFACE_TRANSACTION_VISIBILITY_HIDE);
  } else {
    api->ASurfaceTransaction_setVisibility(
        aTransaction, mSurfaceControl.get(),
        ASURFACE_TRANSACTION_VISIBILITY_SHOW);

    ARect src = {
        .left = (int32_t)bufferClip.x,
        .top = (int32_t)bufferClip.y,
        .right = (int32_t)bufferClip.XMost(),
        .bottom = (int32_t)bufferClip.YMost(),
    };
    ARect dest = {
        .left = (int32_t)surfaceRectClipped.x,
        .top = (int32_t)surfaceRectClipped.y,
        .right = (int32_t)surfaceRectClipped.XMost(),
        .bottom = (int32_t)surfaceRectClipped.YMost(),
    };

    api->ASurfaceTransaction_setGeometry(aTransaction, mSurfaceControl.get(),
                                         src, dest,
                                         ANATIVEWINDOW_TRANSFORM_IDENTITY);
  }
}

void NativeLayerAndroid::Remove(
    ASurfaceTransaction* aTransaction,
    Maybe<NativeLayerAndroidBufferSource>& aOutFrontBuffer) {
  MutexAutoLock lock(mMutex);
  if (BufferSourceHasBuffer(mFrontBuffer)) {
    aOutFrontBuffer.emplace(std::move(mFrontBuffer));
  }
  const auto* api = AndroidSurfaceControlApi::Get();
  api->ASurfaceTransaction_reparent(aTransaction, mSurfaceControl.get(),
                                    nullptr);
}

void NativeLayerAndroid::NotifySurfaceReady() {
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mInProgressBuffer);
  if (!mSurfacePoolHandle->gl()) {
    mInProgressBuffer->Unlock();
  }
  mPrevFrontBuffer = std::move(mFrontBuffer);
  mFrontBuffer = AsVariant(std::move(mInProgressBuffer));
  mFrontBufferUpdated = true;
}

void NativeLayerAndroid::DiscardBackbuffers() { MutexAutoLock lock(mMutex); }

void NativeLayerAndroid::AttachExternalImage(
    wr::RenderTextureHost* aExternalImage) {
  MutexAutoLock lock(mMutex);
  if (wr::RenderAndroidImageReaderTextureHost* host =
          aExternalImage->AsRenderAndroidImageReaderTextureHost()) {
    NativeLayerAndroidBufferSource newFrontBuffer = AsVariant(host->GetImage());
    if (newFrontBuffer != mFrontBuffer) {
      mPrevFrontBuffer = std::move(mFrontBuffer);
      mFrontBuffer = std::move(newFrontBuffer);
      mFrontBufferUpdated = true;
    }
    mSize = host->GetSize();
    mDisplayRect = gfx::IntRect(gfx::IntPoint(), mSize);
  }
}

}  // namespace mozilla::layers
