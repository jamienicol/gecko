/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/NativeLayerAndroid.h"

#include "GLBlitHelper.h"
#include "android/native_window.h"
#include "mozilla/Assertions.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/Variant.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/AndroidHardwareBuffer.h"
#include "mozilla/layers/SurfacePoolAndroid.h"
#include "mozilla/webrender/RenderAndroidHardwareBufferTextureHost.h"
#include "mozilla/webrender/RenderThread.h"

namespace mozilla::layers {

NativeLayerAndroidBufferSource::NativeLayerAndroidBufferSource(
    RefPtr<AndroidHardwareBuffer> aBuffer)
    : mBuffer(aBuffer) {}
NativeLayerAndroidBufferSource::NativeLayerAndroidBufferSource(
    RefPtr<wr::RenderAndroidHardwareBufferTextureHost> aTextureHost)
    : mBuffer(aTextureHost) {}

auto NativeLayerAndroidBufferSource::operator=(
    RefPtr<AndroidHardwareBuffer> aBuffer) {
  MOZ_ASSERT(mBuffer.is<decltype(aBuffer)>(),
             "NativeLayer should not change type.");
  mBuffer = AsVariant(aBuffer);
}
auto NativeLayerAndroidBufferSource::operator=(
    RefPtr<wr::RenderAndroidHardwareBufferTextureHost> aTextureHost) {
  MOZ_ASSERT(mBuffer.is<decltype(aTextureHost)>(),
             "NativeLayer should not change type.");
  mBuffer = AsVariant(aTextureHost);
}

NativeLayerAndroidBufferSource::operator bool() const {
  return mBuffer.match(
      [](const RefPtr<AndroidHardwareBuffer>& surface) {
        return surface != nullptr;
      },
      [](const RefPtr<wr::RenderAndroidHardwareBufferTextureHost>& host) {
        return host && host->GetAndroidHardwareBuffer();
      });
}

RefPtr<AndroidHardwareBuffer> NativeLayerAndroidBufferSource::Buffer() const {
  return mBuffer.match(
      [](const RefPtr<AndroidHardwareBuffer>& buffer) { return buffer; },
      [](const RefPtr<wr::RenderAndroidHardwareBufferTextureHost>& host) {
        return host ? host->GetAndroidHardwareBuffer() : nullptr;
      });
}

bool NativeLayerAndroidBufferSource::IsExternal() const {
  return mBuffer.is<RefPtr<wr::RenderAndroidHardwareBufferTextureHost>>();
}

RefPtr<wr::RenderAndroidHardwareBufferTextureHost>
NativeLayerAndroidBufferSource::AsTextureHost() const {
  return mBuffer.match(
      [](const RefPtr<AndroidHardwareBuffer>& buffer) {
        return RefPtr<wr::RenderAndroidHardwareBufferTextureHost>(nullptr);
      },
      [](const RefPtr<wr::RenderAndroidHardwareBufferTextureHost>& host) {
        return host;
      });
}

/* static */
already_AddRefed<NativeLayerRootAndroid> NativeLayerRootAndroid::Create() {
  if (mozilla::jni::GetAPIVersion() < 31) {
    return nullptr;
  }
  RefPtr<NativeLayerRootAndroid> layerRoot = new NativeLayerRootAndroid();
  return layerRoot.forget();
}

NativeLayerRootAndroid::NativeLayerRootAndroid()
    : mMutex("NativeLayerRootAndroid") {}

already_AddRefed<NativeLayer> NativeLayerRootAndroid::CreateLayer(
    const gfx::IntSize& aSize, bool aIsOpaque,
    SurfacePoolHandle* aSurfacePoolHandle) {
  UniquePtr<ASurfaceControl> surfaceControl =
      WrapUnique(ASurfaceControl_create(mSurfaceControl.get(), "NativeLayer"));

  if (!surfaceControl) {
    gfxCriticalError() << "Failed to create child SurfaceControl";
    return nullptr;
  }

  RefPtr<NativeLayer> layer =
      new NativeLayerAndroid(std::move(surfaceControl), aSize, aIsOpaque,
                             aSurfacePoolHandle->AsSurfacePoolHandleAndroid());

  return layer.forget();
}

already_AddRefed<NativeLayer>
NativeLayerRootAndroid::CreateLayerForExternalTexture(bool aIsOpaque) {
  UniquePtr<ASurfaceControl> surfaceControl =
      WrapUnique(ASurfaceControl_create(mSurfaceControl.get(), "NativeLayer"));

  if (!surfaceControl) {
    gfxCriticalError() << "Failed to create SurfaceControl from parent";
    return nullptr;
  }

  RefPtr<NativeLayer> layer =
      new NativeLayerAndroid(std::move(surfaceControl), aIsOpaque);
  return layer.forget();
}

void NativeLayerRootAndroid::AppendLayer(NativeLayer* aLayer) {
  MOZ_RELEASE_ASSERT(false);
}

void NativeLayerRootAndroid::RemoveLayer(NativeLayer* aLayer) {
  MOZ_RELEASE_ASSERT(false);
  MutexAutoLock lock(mMutex);
}

void NativeLayerRootAndroid::SetLayers(
    const nsTArray<RefPtr<NativeLayer>>& aLayers) {
  MutexAutoLock lock(mMutex);

  nsTArray<RefPtr<NativeLayerAndroid>> newSublayers(aLayers.Length());
  for (const RefPtr<NativeLayer>& layer : aLayers) {
    RefPtr<NativeLayerAndroid> layerAndroid = layer->AsNativeLayerAndroid();
    newSublayers.AppendElement(layerAndroid);
  }

  if (newSublayers != mSublayers) {
    for (const RefPtr<NativeLayerAndroid>& layer : mSublayers) {
      if (!newSublayers.Contains(layer)) {
        mRemovedSublayers.AppendElement(layer);
      }
    }
    mSublayers = std::move(newSublayers);
    mMutatedLayers = true;
  }
}

UniquePtr<NativeLayerRootSnapshotter>
NativeLayerRootAndroid::CreateSnapshotter() {
  return NativeLayerRootSnapshotterAndroid::Create();
}

bool NativeLayerRootAndroid::CommitToScreen() {
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mSurfaceControl);

  ASurfaceTransaction* txn = ASurfaceTransaction_create();

  // FIXME: use compositor clear colour instead of hardcoded-white
  ASurfaceTransaction_setColor(txn, mSurfaceControl.get(), 1.0, 1.0, 1.0, 1.0,
                               ADATASPACE_SRGB);

  // FIXME: explain why is this required
  ASurfaceTransaction_setEnableBackPressure(txn, mSurfaceControl.get(), true);

  std::unordered_map<ASurfaceControl*, PendingBuffer> pendingBuffers;
  if (mMutatedLayers) {
    mMutatedLayers = false;
    for (auto& layer : mRemovedSublayers) {
      ASurfaceTransaction_reparent(txn, layer->mSurfaceControl.get(), nullptr);

      if (layer->mFrontBuffer) {
        pendingBuffers.insert(
            {layer->mSurfaceControl.get(),
             PendingBuffer{.mSurfacePoolHandle = layer->mSurfacePoolHandle,
                           .mBuffer = std::move(layer->mFrontBuffer)}});
      }
      MOZ_ASSERT(!layer->mPrevFrontBuffer,
                 "Removed layer should not have previous front buffer");
    }
    mRemovedSublayers.Clear();
  }

  int32_t zOrder = 0;
  for (auto& layer : mSublayers) {
    ASurfaceControl* sc = layer->mSurfaceControl.get();
    ASurfaceTransaction_reparent(txn, sc, mSurfaceControl.get());
    ASurfaceTransaction_setZOrder(txn, sc, zOrder++);

    if (layer->mFrontBufferUpdated) {
      layer->mFrontBufferUpdated = false;
      UniqueFileHandle fence;
      if (layer->mFrontBuffer.IsExternal()) {
        fence = layer->mFrontBuffer.Buffer()->GetAndResetAcquireFence();
      } else {
        fence = DuplicateFileHandle(mLayersRenderedFence);
      }
      ASurfaceTransaction_setBuffer(
          txn, sc, layer->mFrontBuffer.Buffer()->GetNativeBuffer(),
          fence.release());

      AutoTArray<ARect, 1> dirtyRects;
      dirtyRects.SetCapacity(layer->mDirtyRegion.GetNumRects());
      for (auto iter = layer->mDirtyRegion.RectIter(); !iter.Done();
           iter.Next()) {
        const auto& rect = iter.Get();
        dirtyRects.AppendElement(ARect{
            .left = rect.x,
            .top = rect.y,
            .right = rect.XMost(),
            .bottom = rect.YMost(),
        });
      }
      ASurfaceTransaction_setDamageRegion(txn, sc, dirtyRects.Elements(),
                                          dirtyRects.Length());

      if (layer->mPrevFrontBuffer) {
        pendingBuffers.insert(
            {sc, PendingBuffer{.mSurfacePoolHandle = layer->mSurfacePoolHandle,
                               .mBuffer = std::move(layer->mPrevFrontBuffer)}});
      }
    }

    auto transform2D = layer->mTransform.As2D();
    transform2D.PreTranslate(gfx::Point(layer->mPosition));

    gfx::Rect surfaceRectClipped =
        gfx::Rect(gfx::Point(), gfx::Size(layer->mSize));
    surfaceRectClipped =
        surfaceRectClipped.Intersect(gfx::Rect(layer->mDisplayRect));
    surfaceRectClipped = transform2D.TransformBounds(surfaceRectClipped);
    if (layer->mClipRect) {
      surfaceRectClipped =
          surfaceRectClipped.Intersect(gfx::Rect(*layer->mClipRect));
    }

    auto transform2DInversed = transform2D.Inverse();
    const gfx::Rect bufferClip =
        transform2DInversed.TransformBounds(surfaceRectClipped);

    if (surfaceRectClipped.IsEmpty() || bufferClip.IsEmpty()) {
      // We must explicitly hide the surface, as ASurfaceControl_setGeometry
      // does not support empty rects.
      ASurfaceTransaction_setVisibility(txn, sc,
                                        ASURFACE_TRANSACTION_VISIBILITY_HIDE);
    } else {
      ASurfaceTransaction_setVisibility(txn, sc,
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

      int32_t transform = ANATIVEWINDOW_TRANSFORM_IDENTITY;
      if (transform2D._11 < 0.0) {
        transform |= ANATIVEWINDOW_TRANSFORM_MIRROR_HORIZONTAL;
      }
      if (transform2D._22 < 0.0) {
        transform |= ANATIVEWINDOW_TRANSFORM_MIRROR_VERTICAL;
      }
      ASurfaceTransaction_setGeometry(txn, sc, src, dest, transform);
    }
  }

  mLayersRenderedFence = nullptr;

  mPendingBuffers.push(std::move(pendingBuffers));

  AddRef();
  ASurfaceTransaction_setOnComplete(
      txn, this, [](void* context, ASurfaceTransactionStats* stats) {
        auto* root = static_cast<NativeLayerRootAndroid*>(context);
        root->OnTransactionComplete(stats);
        root->Release();
      });

  // Failing to call setOnCommit() (even if the callback does nothing) results
  // in the surface controls in the *onComplete* callback unexpectedly having
  // release fences, despite them having not had a buffer released in the
  // completed transaction.
  ASurfaceTransaction_setOnCommit(
      txn, this, [](void* context, ASurfaceTransactionStats* stats) {});

  ASurfaceTransaction_apply(txn);
  ASurfaceTransaction_delete(txn);
  return true;
}

bool NativeLayerRootAndroid::Attach(ANativeWindow* aNativeWindow) {
  // FIXME: does this race with CommitToScreen on the render thread?
  MutexAutoLock lock(mMutex);
  mSurfaceControl = WrapUnique(
      ASurfaceControl_createFromWindow(aNativeWindow, "NativeLayerRoot"));
  if (!mSurfaceControl) {
    gfxCriticalError() << "Failed to create SurfaceControl from NativeWindow";
    return false;
  }

  return true;
}

void NativeLayerRootAndroid::Detach() {
  MutexAutoLock lock(mMutex);
  mSurfaceControl = nullptr;
}

void NativeLayerRootAndroid::SetLayersRenderedFence(UniqueFileHandle&& aFence) {
  MOZ_ASSERT(!mLayersRenderedFence);
  mLayersRenderedFence = std::move(aFence);
}

void NativeLayerRootAndroid::OnTransactionComplete(
    ASurfaceTransactionStats* stats) {
  MutexAutoLock lock(mMutex);

  std::unordered_map<ASurfaceControl*, PendingBuffer> pendingBuffers =
      std::move(mPendingBuffers.front());
  mPendingBuffers.pop();

  ASurfaceControl** surfaceControls;
  size_t numSurfaceControls;
  ASurfaceTransactionStats_getASurfaceControls(stats, &surfaceControls,
                                               &numSurfaceControls);

  for (size_t i = 0; i < numSurfaceControls; i++) {
    ASurfaceControl* const sc = surfaceControls[i];

    UniqueFileHandle releaseFence = UniqueFileHandle(
        ASurfaceTransactionStats_getPreviousReleaseFenceFd(stats, sc));
    const auto& releasedBuffer = pendingBuffers.find(sc);
    if (releasedBuffer != pendingBuffers.end()) {
      releasedBuffer->second.mBuffer.Buffer()->SetReleaseFence(
          std::move(releaseFence));
      if (releasedBuffer->second.mBuffer.IsExternal()) {
        // Ensure external texture is released on the render threaad.
        // Really we need to ensure it is kept alive until this point
        printf_stderr(
            "jamiedbg FIXME: handle released external hardware buffer %" PRIu64
            "\n",
            releasedBuffer->second.mBuffer.AsTextureHost()
                ->GetAndroidHardwareBuffer()
                ->mId);
      } else {
        releasedBuffer->second.mSurfacePoolHandle->ReturnBufferToPool(
            releasedBuffer->second.mBuffer.Buffer());
      }
      pendingBuffers.erase(sc);
    } else {
      MOZ_ASSERT(!releaseFence,
                 "No PendingBuffer entry found for release fence.");
    }
  }

  MOZ_ASSERT(pendingBuffers.empty(),
             "PendingBuffer entry found with no corresponding release fence.");

  ASurfaceTransactionStats_releaseASurfaceControls(surfaceControls);
}

bool NativeLayerRootSnapshotterAndroid::ReadbackPixels(
    const gfx::IntSize& aReadbackSize, gfx::SurfaceFormat aReadbackFormat,
    const Range<uint8_t>& aReadbackBuffer) {
  return true;
}

already_AddRefed<profiler_screenshots::RenderSource>
NativeLayerRootSnapshotterAndroid::GetWindowContents(
    const gfx::IntSize& aWindowSize) {
  return nullptr;
}

already_AddRefed<profiler_screenshots::DownscaleTarget>
NativeLayerRootSnapshotterAndroid::CreateDownscaleTarget(
    const gfx::IntSize& aSize) {
  return nullptr;
}

already_AddRefed<profiler_screenshots::AsyncReadbackBuffer>
NativeLayerRootSnapshotterAndroid::CreateAsyncReadbackBuffer(
    const gfx::IntSize& aSize) {
  return nullptr;
}

NativeLayerAndroid::NativeLayerAndroid(
    UniquePtr<ASurfaceControl>&& aSurfaceControl, const gfx::IntSize& aSize,
    bool aIsOpaque, SurfacePoolHandleAndroid* aSurfacePoolHandle)
    : mMutex("NativeLayerAndroid"),
      mSurfacePoolHandle(aSurfacePoolHandle),
      mSize(aSize),
      mIsOpaque(aIsOpaque),
      mSurfaceControl(std::move(aSurfaceControl)),
      mFrontBuffer(static_cast<AndroidHardwareBuffer*>(nullptr)),
      mPrevFrontBuffer(static_cast<AndroidHardwareBuffer*>(nullptr)) {
  MOZ_RELEASE_ASSERT(mSurfacePoolHandle,
                     "Need a non-null surface pool handle.");
}

NativeLayerAndroid::NativeLayerAndroid(
    UniquePtr<ASurfaceControl>&& aSurfaceControl, bool aIsOpaque)
    : mMutex("NativeLayerAndroid"),
      mSurfacePoolHandle(nullptr),
      mIsOpaque(aIsOpaque),
      mSurfaceControl(std::move(aSurfaceControl)),
      mFrontBuffer(
          static_cast<wr::RenderAndroidHardwareBufferTextureHost*>(nullptr)),
      mPrevFrontBuffer(
          static_cast<wr::RenderAndroidHardwareBufferTextureHost*>(nullptr)) {}

void NativeLayerAndroid::AttachExternalImage(
    wr::RenderTextureHost* aExternalImage) {
  RefPtr<wr::RenderAndroidHardwareBufferTextureHost> externalImage =
      aExternalImage->AsRenderAndroidHardwareBufferTextureHost();
  MOZ_ASSERT(aExternalImage);
  // printf_stderr(
  //     "jamiedbg NativeLayerAndroid::AttachExternalImage() ID: %" PRIu64 "\n",
  //     externalImage->GetAndroidHardwareBuffer()->mId);
  if (mFrontBuffer.AsTextureHost() != externalImage) {
    // printf_stderr(
    //     "jamiedbg Updating external front buffer with hardwarebuffer %p\n",
    //     externalImage->GetAndroidHardwareBuffer()->GetNativeBuffer());
    mPrevFrontBuffer = std::move(mFrontBuffer);
    mFrontBuffer = externalImage;
    mFrontBufferUpdated = true;
  }
  mSize = externalImage->GetSize();
  mDisplayRect = gfx::IntRect({}, mSize);
  mDirtyRegion = gfx::IntRect({}, mSize);
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
  MOZ_ASSERT(aTransform.IsRectilinear());
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

  MOZ_RELEASE_ASSERT(false, "Not implemented");

  return nullptr;
}

Maybe<GLuint> NativeLayerAndroid::NextSurfaceAsFramebuffer(
    const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
    bool aNeedsDepth) {
  MutexAutoLock lock(mMutex);

  mDisplayRect = aDisplayRect;
  mDirtyRegion = aUpdateRegion;

  MOZ_ASSERT(!mInProgressBuffer);
  mInProgressBuffer = mSurfacePoolHandle->ObtainBufferFromPool(mSize);

  if (!mInProgressBuffer) {
    gfxCriticalError() << "Failed to obtain buffer";
    wr::RenderThread::Get()->HandleWebRenderError(
        wr::WebRenderError::NEW_SURFACE);
    return Nothing();
  }

  // get the framebuffer before handling partial damage so we don't accidently
  // create one without depth buffer
  Maybe<GLuint> fbo = mSurfacePoolHandle->GetFramebufferForBuffer(
      mInProgressBuffer, aNeedsDepth);
  MOZ_RELEASE_ASSERT(fbo, "GetFramebufferForBuffer failed.");

  if (mFrontBuffer) {
    HandlePartialUpdate(lock);
  }

  return fbo;
}

void NativeLayerAndroid::NotifySurfaceReady() {
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(!mPrevFrontBuffer);
  MOZ_ASSERT(mInProgressBuffer);
  mPrevFrontBuffer = std::move(mFrontBuffer);
  mFrontBuffer = std::move(mInProgressBuffer);
  mInProgressBuffer = nullptr;
  mFrontBufferUpdated = true;
}

void NativeLayerAndroid::DiscardBackbuffers() {}

void NativeLayerAndroid::HandlePartialUpdate(
    const MutexAutoLock& aProofOfLock) {
  gfx::IntRegion copyRegion = gfx::IntRegion(mDisplayRect);
  copyRegion.SubOut(mDirtyRegion);

  if (!copyRegion.IsEmpty()) {
    auto& gl = mSurfacePoolHandle->gl();
    if (gl) {
      gl->MakeCurrent();
      Maybe<GLuint> sourceFB = mSurfacePoolHandle->GetFramebufferForBuffer(
          mFrontBuffer.Buffer(), false);
      MOZ_RELEASE_ASSERT(sourceFB);
      Maybe<GLuint> destFB =
          mSurfacePoolHandle->GetFramebufferForBuffer(mInProgressBuffer, false);
      MOZ_RELEASE_ASSERT(destFB);
      for (auto iter = copyRegion.RectIter(); !iter.Done(); iter.Next()) {
        gfx::IntRect r = iter.Get();
        gl->BlitHelper()->BlitFramebufferToFramebuffer(*sourceFB, *destFB, r, r,
                                                       LOCAL_GL_NEAREST);
      }
    } else {
      MOZ_RELEASE_ASSERT(false, "Not implemented");
    }
  }
}

}  // namespace mozilla::layers
