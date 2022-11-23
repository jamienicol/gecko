/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/NativeLayerAndroid.h"
#include "AndroidSurfaceControl.h"
#include "mozilla/Assertions.h"
#include "mozilla/Maybe.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/SurfacePoolAndroid.h"
#include "mozilla/webrender/RenderThread.h"
#include "nsDebug.h"
#include "nsISupports.h"
#include "nsTArray.h"
#include <android/data_space.h>
#include <android/native_window.h>
#include <android/surface_control.h>
#include "mozilla/ToString.h"

namespace mozilla::layers {

/* static */
already_AddRefed<NativeLayerRootAndroid> NativeLayerRootAndroid::Create(
    ANativeWindow* aNativeWindow) {
  auto api = AndroidSurfaceControlApi::Get();
  UniquePtr<ASurfaceControl> surfaceControl(
      api->ASurfaceControl_createFromWindow(aNativeWindow, "NativeLayerRoot"));
  if (!surfaceControl) {
    gfxCriticalError() << "Failed to create SurfaceControl from NativeWindow";
    return nullptr;
  }

  RefPtr<NativeLayerRootAndroid> layerRoot =
      new NativeLayerRootAndroid(std::move(surfaceControl));
  return layerRoot.forget();
}

NativeLayerRootAndroid::NativeLayerRootAndroid(
    UniquePtr<ASurfaceControl>&& aSurfaceControl)
    : mMutex("NativeLayerRootAndroid"),
      mSurfaceControl(std::move(aSurfaceControl)) {
    printf_stderr("jamiedbg new NativeLayerRootAndroid() %p\n", this);
}

NativeLayerRootAndroid::~NativeLayerRootAndroid() {
    printf_stderr("jamiedbg ~NativeLayerRootAndroid() %p\n", this);
}

already_AddRefed<NativeLayer> NativeLayerRootAndroid::CreateLayer(
    const gfx::IntSize& aSize, bool aIsOpaque,
    SurfacePoolHandle* aSurfacePoolHandle) {
  // printf_stderr("jamiedbg NativeLayerRootAndroid::CreateLayer() %s\n",
  //               mozilla::ToString(aSize).c_str());
  auto pool = aSurfacePoolHandle->AsSurfacePoolHandleAndroid();
  UniquePtr<ASurfaceControl> surfaceControl =
      pool->ObtainSurfaceControl(mSurfaceControl.get());
  if (!surfaceControl) {
    return nullptr;
  }

  RefPtr<NativeLayer> layer =
      new NativeLayerAndroid(std::move(surfaceControl), aSize, aIsOpaque, pool);
  return layer.forget();
}

already_AddRefed<NativeLayer>
NativeLayerRootAndroid::CreateLayerForExternalTexture(
    bool aIsOpaque) {
  MOZ_ASSERT_UNREACHABLE("Not implemented");
}

void NativeLayerRootAndroid::AppendLayer(NativeLayer* aLayer) {
  // FIXME: mixing Append/RemoveLayer with SetLayers might lead to problems...
  printf_stderr("jamiedbg NativeLayerRootAndroid::AppendLayer() %p\n", aLayer);

  MutexAutoLock lock(mMutex);
  RefPtr<NativeLayerAndroid> layer = aLayer->AsNativeLayerAndroid();
  MOZ_RELEASE_ASSERT(layer);

  mSublayers.AppendElement(layer);
  mLayersMutated = true;
};

void NativeLayerRootAndroid::RemoveLayer(NativeLayer* aLayer) {
  printf_stderr("jamiedbg NativeLayerRootAndroid::RemoveLayer() %p\n", aLayer);

  MutexAutoLock lock(mMutex);
  RefPtr<NativeLayerAndroid> layer = aLayer->AsNativeLayerAndroid();
  MOZ_RELEASE_ASSERT(layer);

  mSublayers.RemoveElement(layer);
  mOldSublayers.AppendElement(layer);
  mLayersMutated = true;
};

void NativeLayerRootAndroid::SetLayers(
    const nsTArray<RefPtr<NativeLayer>>& aLayers) {
  MutexAutoLock lock(mMutex);
  printf_stderr("jamiedbg NativeLayerRootAndroid::SetLayers() %zu\n",
                aLayers.Length());

  nsTArray<RefPtr<NativeLayerAndroid>> newSublayers(aLayers.Length());
  for (const RefPtr<NativeLayer>& sublayer : aLayers) {
    printf_stderr("jamiedbg NativeLayerRootAndroid::SetLayers() %p\n",
                  sublayer.get());
    RefPtr<NativeLayerAndroid> layer = sublayer->AsNativeLayerAndroid();
    newSublayers.AppendElement(layer);
  }

  if (newSublayers != mSublayers) {
    for (const RefPtr<NativeLayerAndroid>& layer : mSublayers) {
      if (!newSublayers.Contains(layer)) {
        // printf_stderr("jamiedbg removing old sublayer %p\n", layer.get());
        mOldSublayers.AppendElement(layer);
      }
    }
    mSublayers = std::move(newSublayers);
    mLayersMutated = true;
  }
};

bool NativeLayerRootAndroid::CommitToScreen() {
  MutexAutoLock lock(mMutex);
  printf_stderr("jamiedbg NativeLayerRootAndroid::CommitToScreen()\n");

  auto api = AndroidSurfaceControlApi::Get();

  ASurfaceTransaction* transaction = api->ASurfaceTransaction_create();

  ReleasedBufferMap prevBuffers;

  if (mLayersMutated) {
    // FIXME: reparent old layers to null?
    for (auto& layer : mOldSublayers) {
      // printf_stderr("jamiedbg Unparenting old sublayer\n");
      layer->Remove(transaction, prevBuffers);
    }
    mOldSublayers.Clear();
    mLayersMutated = false;
  }

  int z = 0;
  for (RefPtr<NativeLayerAndroid>& layer : mSublayers) {
    layer->Update(transaction, mSurfaceControl.get(), prevBuffers, z,
                  mLayersRenderedFence);
    z++;
  }

  if (mLayersRenderedFence >= 0) {
    ::close(mLayersRenderedFence);
    mLayersRenderedFence = -1;
  }

  // Handle transcation complete in callback
  mReleasedBuffers.push(std::move(prevBuffers));
  // FIXME: we probably want to use a weak reference here? Because
  // otherwise the callback can keep the NativeLayerRoot alive, but
  // nothing will schedule the required transactions to clean up
  AddRef();
  api->ASurfaceTransaction_setOnComplete(
      transaction, this, [](void* context, ASurfaceTransactionStats* stats) {
        NativeLayerRootAndroid* root = (NativeLayerRootAndroid*)context;
        root->OnTransactionComplete(stats);
        root->Release();
      });

  // Apply the transaction
  api->ASurfaceTransaction_apply(transaction);
  api->ASurfaceTransaction_delete(transaction);
  return true;
};

void NativeLayerRootAndroid::SetLayersRenderedFence(int aFence) {
  printf_stderr(
      "jamiedbg NativeLayerRootAndroid::SetLayersRenderedFence() %d\n", aFence);
  MOZ_RELEASE_ASSERT(mLayersRenderedFence == -1);
  MOZ_RELEASE_ASSERT(aFence != -1);
  mLayersRenderedFence = aFence;
}

void NativeLayerRootAndroid::OnTransactionComplete(
    ASurfaceTransactionStats* stats) {
  MutexAutoLock lock(mMutex);
  printf_stderr("jamiedbg OnTransactionComplete()\n");
  auto api = AndroidSurfaceControlApi::Get();
  ASurfaceControl** surfaceControls;
  size_t surfaceControlsSize;
  api->ASurfaceTransactionStats_getASurfaceControls(stats, &surfaceControls,
                                                    &surfaceControlsSize);

  ReleasedBufferMap& releasedBuffers = mReleasedBuffers.front();

  for (size_t i = 0; i < surfaceControlsSize; i++) {
    ASurfaceControl* surfaceControl = surfaceControls[i];
    const auto& released = releasedBuffers.find(surfaceControl);
    if (released == releasedBuffers.end()) {
      // printf_stderr(
      //     "jamiedbg ASurfaceControl %p not present in releasedBuffers",
      //     surfaceControl);
    } else {
      int releaseFence =
          api->ASurfaceTransactionStats_getPreviousReleaseFenceFd(
              stats, surfaceControl);
      printf_stderr("jamiedbg Releasing buffer: %p fence: %d\n",
                    released->second.mBuffer.get(),
                    releaseFence);
      released->second.mBuffer->OnConsumerRelease(releaseFence);
      released->second.mSurfacePoolHandle->ReturnBufferToPool(
          std::move(released->second.mBuffer));
      releasedBuffers.erase(surfaceControl);
    }
  }

  MOZ_RELEASE_ASSERT(releasedBuffers.empty());

  mReleasedBuffers.pop();
  api->ASurfaceTransactionStats_releaseASurfaceControls(surfaceControls);
}

NativeLayerAndroid::NativeLayerAndroid(
    UniquePtr<ASurfaceControl> aSurfaceControl, const gfx::IntSize& aSize,
    bool aIsOpaque, SurfacePoolHandleAndroid* aSurfacePoolHandle)
    : mMutex("NativeLayerAndroid"),
      mSurfacePoolHandle(aSurfacePoolHandle),
      mSize(aSize),
      mIsOpaque(aIsOpaque),
      mSurfaceControl(std::move(aSurfaceControl)) {
  printf_stderr("jamiedbg new NativeLayerAndroid() %p\n", this);
  MOZ_RELEASE_ASSERT(mSurfacePoolHandle,
                     "Need a non-null surface pool handle.");
}

NativeLayerAndroid::~NativeLayerAndroid() {
  printf_stderr(
                "jamiedbg ~NativeLayerAndroid() %p\n", this);

  // mPrevFrontBuffer should be null here, as either the layer has never been
  // displayed, or Remove() should have been called.
  MOZ_ASSERT(!mPrevFrontBuffer);
  if (mFrontBuffer) {
    // Likewise we may have a front buffer, but it must never have been
    // attached otherwise Remove() would have been called. This means we can
    // return the buffer to the pool immediately.


      // I THINK WE ARE HITTING THIS CRASH WHEN OPENING THE KEYBOARD
      // BECAUSE THE NATIVELAYERROOT IS DESTROYED DUE TO RESIZE, AND
      // THEREFORE THE NEW ONE DOES NOT HOLD A REFERENCE TO THE DEBUG
      // OVERLAY LAYER.  THEN WE CALL DESTROYSURFACE() FOR THE DEBUG
      // OVERLAY LAYER, WHICH MAKES RENDERCOMPOSITORNATIVE DROP THE
      // LAST REFERENCE. IF WE STILL HELD THE REFERENCE IN THE
      // NATIVELAYERROOT THEN THIS WOULDN'T BE CALLED UNTIL AFTER WE
      // HAVE REMOVED() IT.

      // I THINK. NEED TO CONFIRM.  Also, should probably not hit this
      // assertion anyway, because the native layer root may in fact
      // be destroyed.


    MOZ_ASSERT(!mFrontBuffer->IsConsumerAttached());
    mSurfacePoolHandle->ReturnBufferToPool(std::move(mFrontBuffer));
  }

  mSurfacePoolHandle->ReturnSurfaceControl(std::move(mSurfaceControl));
}

gfx::IntSize NativeLayerAndroid::GetSize() { return mSize; }

bool NativeLayerAndroid::IsOpaque() { return mIsOpaque; }

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
  MOZ_ASSERT_UNREACHABLE("Not implemented");
}

Maybe<GLuint> NativeLayerAndroid::NextSurfaceAsFramebuffer(
    const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
    bool aNeedsDepth) {
  MutexAutoLock lock(mMutex);

  printf_stderr(
      "jamiedbg NativeLayerAndroid::NextSurfaceAsFramebuffer() %p display: %s, "
      "update: %s\n",
      this, mozilla::ToString(aDisplayRect).c_str(),
      mozilla::ToString(aUpdateRegion).c_str());

  mDisplayRect = aDisplayRect;
  mDirtyRegion = aUpdateRegion;

  MOZ_ASSERT(!mInProgressBuffer);
  if (mFrontBuffer && !mFrontBuffer->IsConsumerAttached()) {
    printf_stderr("jamiedbg FrontBuffer %p is not attached. re-using\n",
                  mFrontBuffer.get());
    // The layer's front buffer has not been attached, so we can re-use it
    mInProgressBuffer = std::move(mFrontBuffer);
  } else {
    printf_stderr("jamiedbg Obtaining new front buffer\n");
    mInProgressBuffer = mSurfacePoolHandle->ObtainBufferFromPool(mSize);
  }

  if (!mInProgressBuffer) {
    gfxCriticalError() << "Failed to obtain buffer";
    wr::RenderThread::Get()->HandleWebRenderError(
        wr::WebRenderError::NEW_SURFACE);
    return Nothing();
  }

  // get the framebuffer before handling partial damage so we don't accidently
  // create one without depth buffer
  Maybe<GLuint> fbo = mInProgressBuffer->GetFramebuffer(aNeedsDepth);
  if (!fbo) {
    return Nothing();
  }

  if (mFrontBuffer) {
    // FIXME: (optionally?) use buffer age instead of handling the partial
    // update ourselves
    HandlePartialUpdate(lock);
    // FIXME: do I need to do this move prior to any error early
    // returns? And make HandlePartialUpdate use mPrevFrontBuffer?
    // same for CPU case?
    mPrevFrontBuffer = std::move(mFrontBuffer);
  }

  return fbo;
}

void NativeLayerAndroid::HandlePartialUpdate(
    const MutexAutoLock& aProofOfLock) {
  gfx::IntRegion copyRegion = gfx::IntRegion(mDisplayRect);
  copyRegion.SubOut(mDirtyRegion);

  if (!copyRegion.IsEmpty()) {
    auto& gl = mSurfacePoolHandle->gl();
    if (gl) {
      gl->MakeCurrent();
      Maybe<GLuint> sourceFB = mFrontBuffer->GetFramebuffer(false);
      Maybe<GLuint> destFB = mInProgressBuffer->GetFramebuffer(false);
      // FIXME: handle error?
      MOZ_RELEASE_ASSERT(sourceFB);
      MOZ_RELEASE_ASSERT(destFB);
      // FIXME: single blit with bounds instead of multiple?
      for (auto iter = copyRegion.RectIter(); !iter.Done(); iter.Next()) {
        gfx::IntRect r = iter.Get();
        gl->BlitHelper()->BlitFramebufferToFramebuffer(*sourceFB, *destFB, r, r,
                                                       LOCAL_GL_NEAREST);
      }
    }
  }
}

void NativeLayerAndroid::NotifySurfaceReady() {
  printf_stderr("jamiedbg NativeLayerAndroid::NotifySurfaceReady() %p\n", this);
  MOZ_ASSERT(!mFrontBuffer);
  MOZ_ASSERT(mInProgressBuffer);
  mFrontBuffer = std::move(mInProgressBuffer);
  mFrontBufferChanged = true;
};

void NativeLayerAndroid::DiscardBackbuffers(){};

void NativeLayerAndroid::AttachExternalImage(
    wr::RenderTextureHost* aExternalImage){};

void NativeLayerAndroid::Update(
    ASurfaceTransaction* aTransaction, ASurfaceControl* aParent,
    NativeLayerRootAndroid::ReleasedBufferMap& aPrevSurfaces, int z,
    int aFence) {
  printf_stderr("jamiedbg NativeLayerAndroid::Update() %p\n", this);
  auto api = AndroidSurfaceControlApi::Get();
  api->ASurfaceTransaction_reparent(aTransaction, mSurfaceControl.get(),
                                    aParent);

  if (mPrevFrontBuffer) {
    printf_stderr("jamiedbg Removing prev buffer %p\n",
                  mPrevFrontBuffer.get());
    aPrevSurfaces.insert(
        {mSurfaceControl.get(),
         NativeLayerRootAndroid::ReleasedBuffer{std::move(mPrevFrontBuffer),
                                                mSurfacePoolHandle}});
  }

  if (mFrontBuffer) {
    if (mFrontBufferChanged) {
      printf_stderr("jamiedbg Setting buffer %p, fence: %d\n",
                    mFrontBuffer.get(), aFence);
      mFrontBufferChanged = false;
      mFrontBuffer->OnConsumerAttach();
      int fence = -1;
      if (aFence >= 0) {
        fence = ::dup(aFence);
      }
      api->ASurfaceTransaction_setBuffer(aTransaction, mSurfaceControl.get(),
                                         mFrontBuffer->GetBuffer(), fence);

      // FIXME use individual rects in region instead of bounds
      if (!mDirtyRegion.IsEmpty()) {
        AutoTArray<ARect, 4> damage;
        damage.SetCapacity(mDirtyRegion.GetNumRects());
        for (auto iter = mDirtyRegion.RectIter(); !iter.Done(); iter.Next()) {
          damage.AppendElement(ARect{
              .left = (int32_t)mDirtyRegion.GetBounds().x,
              .top = (int32_t)mDirtyRegion.GetBounds().y,
              .right = (int32_t)mDirtyRegion.GetBounds().XMost(),
              .bottom = (int32_t)mDirtyRegion.GetBounds().YMost(),
          });
        }
        api->ASurfaceTransaction_setDamageRegion(
            aTransaction, mSurfaceControl.get(), &damage[0], damage.Length());
        mDirtyRegion.SetEmpty();
      }
    } else {
      printf_stderr("jamiedbg keeping buffer %p\n", mFrontBuffer.get());
    }
  } else {
    printf_stderr("jamiedbg unsetting buffer %p\n", mPrevFrontBuffer.get());
    api->ASurfaceTransaction_setBuffer(aTransaction, mSurfaceControl.get(),
                                       nullptr, -1);
  }

  MOZ_RELEASE_ASSERT(mTransform.Is2D());
  auto transform2D = mTransform.As2D();

  gfx::Rect surfaceRectClipped =
      gfx::Rect(0, 0, (float)mSize.width, (float)mSize.height);
  surfaceRectClipped = surfaceRectClipped.Intersect(gfx::Rect(mDisplayRect));

  transform2D.PostTranslate((float)mPosition.x, (float)mPosition.y);
  surfaceRectClipped = transform2D.TransformBounds(surfaceRectClipped);

  if (mClipRect) {
    surfaceRectClipped =
        surfaceRectClipped.Intersect(gfx::Rect(mClipRect.value()));
  }

  // FIXME: handle flip/rotate transforms

  auto transform2DInversed = transform2D.Inverse();
  gfx::Rect bufferClip =
      transform2DInversed.TransformBounds(surfaceRectClipped);

  if (surfaceRectClipped.IsEmpty() || bufferClip.IsEmpty()) {
    // We must explicitly hide the surface, as ASurfaceControl_setGeometry
    // does not support empty rects.
    api->ASurfaceTransaction_setVisibility(
        aTransaction, mSurfaceControl.get(),
        ASURFACE_TRANSACTION_VISIBILITY_HIDE);

    // FIXME: can we avoid setting the buffer here? (and adding to current
    // buffers)
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

  api->ASurfaceTransaction_setZOrder(aTransaction, mSurfaceControl.get(), z);
}

void NativeLayerAndroid::Remove(
    ASurfaceTransaction* aTransaction,
    NativeLayerRootAndroid::ReleasedBufferMap& aPrevBuffers) {
  printf_stderr("jamiedbg NativeLayerAndroid::Remove() %p\n", this);
  auto api = AndroidSurfaceControlApi::Get();
  api->ASurfaceTransaction_reparent(aTransaction, mSurfaceControl.get(),
                                    nullptr);
  if (mPrevFrontBuffer) {
    aPrevBuffers.insert({mSurfaceControl.get(),
                         NativeLayerRootAndroid::ReleasedBuffer{
                             std::move(mPrevFrontBuffer), mSurfacePoolHandle}});
    // If we have a prev front buffer, then we must have a new front buffer too.
    // And if Remove() has been called rather than Update(), the buffer must not
    // be attached. We can therefore return it to the pool immediately.
    MOZ_ASSERT(mFrontBuffer);
    MOZ_ASSERT(!mFrontBuffer->IsConsumerAttached());
    mSurfacePoolHandle->ReturnBufferToPool(std::move(mFrontBuffer));
  } else if (mFrontBuffer) {
    if (mFrontBuffer->IsConsumerAttached()) {
      api->ASurfaceTransaction_setBuffer(aTransaction, mSurfaceControl.get(),
                                         nullptr, -1);
      // FIXME: do we need to explicitly unset the surface's buffer here?
      aPrevBuffers.insert({mSurfaceControl.get(),
                           NativeLayerRootAndroid::ReleasedBuffer{
                               std::move(mFrontBuffer), mSurfacePoolHandle}});
    } else {
      mSurfacePoolHandle->ReturnBufferToPool(std::move(mFrontBuffer));
    }
  }
}

}  // namespace mozilla::layers
