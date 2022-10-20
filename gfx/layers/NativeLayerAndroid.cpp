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
#include "nsTArray.h"
#include <android/data_space.h>
#include <android/native_window.h>
#include <android/surface_control.h>

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
      mSurfaceControl(std::move(aSurfaceControl)) {}

NativeLayerRootAndroid::~NativeLayerRootAndroid() {}

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
    bool aIsOpaque, SurfacePoolHandle* aSurfacePoolHandle) {
  // printf_stderr("jamiedbg
  // NativeLayerRootAndroid::CreateLayerForExternalTexture()\n");
  auto pool = aSurfacePoolHandle->AsSurfacePoolHandleAndroid();
  UniquePtr<ASurfaceControl> surfaceControl =
      pool->ObtainSurfaceControl(mSurfaceControl.get());
  if (!surfaceControl) {
    return nullptr;
  }

  RefPtr<NativeLayer> layer =
      new NativeLayerAndroid(std::move(surfaceControl), aIsOpaque, pool);
  return layer.forget();
}

already_AddRefed<NativeLayer> NativeLayerRootAndroid::CreateLayerForColor(
    gfx::DeviceColor aColor, SurfacePoolHandle* aSurfacePoolHandle) {
  // printf_stderr("jamiedbg NativeLayerRootAndroid::CreateLayerForColor()
  // %s\n",
  //               mozilla::ToString(aColor).c_str());
  auto pool = aSurfacePoolHandle->AsSurfacePoolHandleAndroid();
  UniquePtr<ASurfaceControl> surfaceControl =
      pool->ObtainSurfaceControl(mSurfaceControl.get());
  if (!surfaceControl) {
    return nullptr;
  }

  RefPtr<NativeLayer> layer =
      new NativeLayerAndroid(std::move(surfaceControl), aColor, pool);
  return layer.forget();
}

void NativeLayerRootAndroid::AppendLayer(NativeLayer* aLayer) {
  MOZ_RELEASE_ASSERT(false);
};

void NativeLayerRootAndroid::RemoveLayer(NativeLayer* aLayer) {
  MOZ_RELEASE_ASSERT(false);
};

void NativeLayerRootAndroid::SetLayers(
    const nsTArray<RefPtr<NativeLayer>>& aLayers) {
  MutexAutoLock lock(mMutex);
  // printf_stderr("jamiedbg NativeLayerRootAndroid::SetLayers() %zu\n",
  //               aLayers.Length());

  nsTArray<RefPtr<NativeLayerAndroid>> newSublayers(aLayers.Length());
  for (const RefPtr<NativeLayer>& sublayer : aLayers) {
    // printf_stderr("jamiedbg NativeLayerRootAndroid::SetLayers() %p\n",
    // sublayer.get());
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
    mNewLayers = true;
  }
};

void NativeLayerRootAndroid::PrepareForCommit() {
  MutexAutoLock lock(mMutex);
  // printf_stderr("jamiedbg NativeLayerRootAndroid::PrepareForCommit()\n");
}

bool NativeLayerRootAndroid::CommitToScreen() {
  MutexAutoLock lock(mMutex);
  // printf_stderr("jamiedbg NativeLayerRootAndroid::CommitToScreen()\n");

  auto api = AndroidSurfaceControlApi::Get();

  ASurfaceTransaction* transaction = api->ASurfaceTransaction_create();

  std::map<ASurfaceControl*, ReleasedSurface> prevSurfaces;

  if (mNewLayers) {
    // FIXME: reparent old layers to null?
    for (auto& layer : mOldSublayers) {
      // printf_stderr("jamiedbg Unparenting old sublayer\n");
      layer->Remove(transaction, prevSurfaces);
    }
    mOldSublayers.Clear();
    mNewLayers = false;
  }

  // FIXME: set clear color if there are no layers?
  // api->ASurfaceTransaction_setColor(transaction, mSurfaceControl.get(), 1.0,
  //                                   0.0, 0.0, 1.0, ADATASPACE_SRGB);

  int z = 0;
  for (RefPtr<NativeLayerAndroid>& layer : mSublayers) {
    layer->Update(transaction, mSurfaceControl.get(), prevSurfaces, z,
                  mLayersRenderedFence);
    z++;
  }

  mLayersRenderedFence.apply(::close).reset();

  // printf_stderr("jamiedbg prevBuffers:\n");
  // for (const auto& it : prevSurfaces) {
  // printf_stderr("jamiedbg sc %p, buffer: %p\n", it.first,
  //               it.second.mSurface.get());
  // }

  mReleasedSurfaces.push(std::move(prevSurfaces));

  AddRef();
  api->ASurfaceTransaction_setOnComplete(
      transaction, this, [](void* context, ASurfaceTransactionStats* stats) {
        NativeLayerRootAndroid* root = (NativeLayerRootAndroid*)context;
        root->OnTransactionComplete(stats);
        root->Release();
      });

  api->ASurfaceTransaction_apply(transaction);
  api->ASurfaceTransaction_delete(transaction);
  return true;
};

void NativeLayerRootAndroid::SetLayersRenderedFence(int aFence) {
  // This must only be called once per frame, after the OpenGL commands to
  // render every layer have been submitted. CommitToScreen() will then take
  // this value, meaning it will be Nothing again next time this function is
  // called.
  mLayersRenderedFence.emplace(aFence);
}

void NativeLayerRootAndroid::OnTransactionComplete(
    ASurfaceTransactionStats* stats) {
  MutexAutoLock lock(mMutex);
  // printf_stderr("jamiedbg OnTransactionComplete()\n");
  auto api = AndroidSurfaceControlApi::Get();
  ASurfaceControl** surfaceControls;
  size_t surfaceControlsSize;
  api->ASurfaceTransactionStats_getASurfaceControls(stats, &surfaceControls,
                                                    &surfaceControlsSize);

  std::map<ASurfaceControl*, ReleasedSurface>& releasedSurfaces =
      mReleasedSurfaces.front();

  for (size_t i = 0; i < surfaceControlsSize; i++) {
    ASurfaceControl* surfaceControl = surfaceControls[i];
    const auto& released = releasedSurfaces.find(surfaceControl);
    if (released == releasedSurfaces.end()) {
      // printf_stderr(
      //     "jamiedbg ASurfaceControl %p not present in releasedSurfaces",
      //     surfaceControl);
    } else {
      int releaseFence =
          api->ASurfaceTransactionStats_getPreviousReleaseFenceFd(
              stats, surfaceControl);
      // printf_stderr("jamiedbg SC: %p, prevBuffer: %p, releaseFence: %d\n",
      //               surfaceControl, released->second.mSurface.get(),
      //               releaseFence);
      released->second.mSurface->OnConsumerRelease(releaseFence);
      released->second.mSurfacePoolHandle->ReturnBufferToPool(
          std::move(released->second.mSurface));
      releasedSurfaces.erase(surfaceControl);
    }
  }

  MOZ_ASSERT(releasedSurfaces.empty());

  mReleasedSurfaces.pop();
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
  // printf_stderr("jamiedbg new NativeLayerAndroid() %p\n", this);
  MOZ_RELEASE_ASSERT(mSurfacePoolHandle,
                     "Need a non-null surface pool handle.");
}

// FIXME: just use the one shared constructor?
NativeLayerAndroid::NativeLayerAndroid(
    UniquePtr<ASurfaceControl> aSurfaceControl, bool aIsOpaque,
    SurfacePoolHandleAndroid* aSurfacePoolHandle)
    : mMutex("NativeLayerAndroid"),
      mSurfacePoolHandle(aSurfacePoolHandle),
      mIsOpaque(aIsOpaque),
      mSurfaceControl(std::move(aSurfaceControl)) {
  // printf_stderr("jamiedbg new NativeLayerAndroid() %p\n", this);
}

NativeLayerAndroid::NativeLayerAndroid(
    UniquePtr<ASurfaceControl> aSurfaceControl, gfx::DeviceColor aColor,
    SurfacePoolHandleAndroid* aSurfacePoolHandle)
    : mMutex("NativeLayerAndroid"),
      mSurfacePoolHandle(aSurfacePoolHandle),
      mColor(Some(aColor)),
      mSurfaceControl(std::move(aSurfaceControl)) {
  // printf_stderr("jamiedbg new NativeLayerAndroid() %p\n", this);
}

NativeLayerAndroid::~NativeLayerAndroid() {
  // printf_stderr(
  //     "jamiedbg ~NativeLayerAndroid() this=%p, front=%p, prevFront=%p, "
  //     "inProg=%p\n",
  //     this, mFrontBuffer.get(), mPrevFrontBuffer.get(),
  //     mInProgressBuffer.get());

  // mPrevFrontBuffer should be null here, as either the layer has never been
  // displayed, or Remove() should have been called.
  MOZ_ASSERT(!mPrevFrontBuffer);
  if (mFrontBuffer) {
    // Likewise we may have a front buffer, but it must never have been
    // attached otherwise Remove() would have been called. This means we can
    // return the buffer to the pool immediately.
    MOZ_ASSERT(!mFrontBuffer->IsConsumerAttached());
    mSurfacePoolHandle->ReturnBufferToPool(std::move(mFrontBuffer));
  }

  if (mSurfaceControl) {
    mSurfacePoolHandle->ReturnSurfaceControl(std::move(mSurfaceControl));
  }
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
  MutexAutoLock lock(mMutex);
  // printf_stderr(
  //               "jamiedbg NativeLayerAndroid::NextSurfaceAsDrawTarget() %p
  //               sc: %p\n", this,
  //     mSurfaceControl.get());

  mDisplayRect = aDisplayRect;
  mDirtyRegion = aUpdateRegion;

  MOZ_ASSERT(!mInProgressBuffer);
  if (mFrontBuffer && !mFrontBuffer->IsConsumerAttached()) {
    // printf_stderr("jamiedbg FrontBuffer %p is not attached. re-using\n",
    //               mFrontBuffer.get());
    // The layer's front buffer has not been attached, so we can re-use it
    mInProgressBuffer = std::move(mFrontBuffer);
  } else {
    // printf_stderr("jamiedbg Obtaining new front buffer\n");
    mInProgressBuffer = mSurfacePoolHandle->ObtainBufferFromPool(mSize);
    // FIXME: handle partial update
    if (mFrontBuffer) {
      HandlePartialUpdate(lock);
      mPrevFrontBuffer = std::move(mFrontBuffer);
    }
  }

  if (!mInProgressBuffer) {
    gfxCriticalError() << "Failed to obtain buffer";
    wr::RenderThread::Get()->HandleWebRenderError(
        wr::WebRenderError::NEW_SURFACE);
    return nullptr;
  }

  return mInProgressBuffer->WriteLock();
}

Maybe<GLuint> NativeLayerAndroid::NextSurfaceAsFramebuffer(
    const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
    bool aNeedsDepth) {
  MutexAutoLock lock(mMutex);

  // printf_stderr(
  //     "jamiedbg NativeLayerAndroid::NextSurfaceAsFramebuffer() %p sc: %p\n",
  //     this, mSurfaceControl.get());

  mDisplayRect = aDisplayRect;
  mDirtyRegion = aUpdateRegion;

  MOZ_ASSERT(!mInProgressBuffer);
  if (mFrontBuffer && !mFrontBuffer->IsConsumerAttached()) {
    // printf_stderr("jamiedbg FrontBuffer %p is not attached. re-using\n",
    //               mFrontBuffer.get());
    // The layer's front buffer has not been attached, so we can re-use it
    mInProgressBuffer = std::move(mFrontBuffer);
  } else {
    // printf_stderr("jamiedbg Obtaining new front buffer\n");
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
      // FIXME: different function to obtain read-only framebuffer?
      // which doesn't need to wait on release fence?
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
    } else {
      RefPtr<gfx::DataSourceSurface> dataSourceSurface =
          mFrontBuffer->ReadLock();
      RefPtr<gfx::DrawTarget> dt = mInProgressBuffer->WriteLock();

      for (auto iter = copyRegion.RectIter(); !iter.Done(); iter.Next()) {
        gfx::IntRect r = iter.Get();
        dt->CopySurface(dataSourceSurface, r, r.TopLeft());
      }

      dt = nullptr;
      dataSourceSurface = nullptr;
      mFrontBuffer->Unlock();
      mInProgressBuffer->Unlock();
    }
  }
}

void NativeLayerAndroid::NotifySurfaceReady() {
  // printf_stderr(
  //             "jamiedbg NativeLayerAndroid::NotifySurfaceReady() %p\n",
  //             this);
  MOZ_ASSERT(!mFrontBuffer);
  MOZ_ASSERT(mInProgressBuffer);
  if (!mSurfacePoolHandle->gl()) {
    mInProgressBuffer->Unlock();
  }
  mFrontBuffer = std::move(mInProgressBuffer);
  mFrontBufferChanged = true;
};

void NativeLayerAndroid::DiscardBackbuffers(){};

void NativeLayerAndroid::AttachExternalImage(
    wr::RenderTextureHost* aExternalImage){};

void NativeLayerAndroid::Update(
    ASurfaceTransaction* aTransaction, ASurfaceControl* aParent,
    std::map<ASurfaceControl*, NativeLayerRootAndroid::ReleasedSurface>&
        aPrevSurfaces,
    int z, const Maybe<int>& aFence) {
  // printf_stderr("jamiedbg NativeLayerAndroid::Update() %p\n", this);
  auto api = AndroidSurfaceControlApi::Get();
  api->ASurfaceTransaction_reparent(aTransaction, mSurfaceControl.get(),
                                    aParent);

  if (mPrevFrontBuffer) {
    // printf_stderr("jamiedbg SurfaceControl %p releasing prev buffer %p\n",
    //               mSurfaceControl.get(), mPrevFrontBuffer.get());
    aPrevSurfaces.insert(
        {mSurfaceControl.get(),
         NativeLayerRootAndroid::ReleasedSurface{std::move(mPrevFrontBuffer),
                                                 mSurfacePoolHandle}});
  }

  if (mFrontBuffer) {
    if (mFrontBufferChanged) {
      mFrontBufferChanged = false;
      int fence = mFrontBuffer->SetConsumerAttached();
      if (fence == -1 && aFence.isSome()) {
        fence = ::dup(*aFence);
      }
      // printf_stderr("jamiedbg SurfaceControl %p setting buffer %p, fence
      // %d\n",
      //               mSurfaceControl.get(), mFrontBuffer.get(), fence);
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
      // printf_stderr("jamiedbg SurfaceControl %p keeping buffer %p\n",
      //               mSurfaceControl.get(), mFrontBuffer.get());
    }
  } else {
    // printf_stderr("jamiedbg SurfaceControl %p unsetting buffer %p\n",
    //               mSurfaceControl.get(), mPrevFrontBuffer.get());
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
    std::map<ASurfaceControl*, NativeLayerRootAndroid::ReleasedSurface>&
        aPrevSurfaces) {
  // printf_stderr("jamiedbg NativeLayerAndroid::Remove() %p\n", this);
  auto api = AndroidSurfaceControlApi::Get();
  api->ASurfaceTransaction_reparent(aTransaction, mSurfaceControl.get(),
                                    nullptr);
  if (mPrevFrontBuffer) {
    aPrevSurfaces.insert(
        {mSurfaceControl.get(),
         NativeLayerRootAndroid::ReleasedSurface{std::move(mPrevFrontBuffer),
                                                 mSurfacePoolHandle}});
    // If we have a prev front buffer, then we must have a new front buffer too. And if Remove()
    // has been called rather than Update(), the buffer must not be attached. We can therefore return it to the pool
    // immediately.
    MOZ_ASSERT(mFrontBuffer);
    MOZ_ASSERT(!mFrontBuffer->IsConsumerAttached());
    mSurfacePoolHandle->ReturnBufferToPool(std::move(mFrontBuffer));
  } else if (mFrontBuffer) {
      if (mFrontBuffer->IsConsumerAttached()) {
        api->ASurfaceTransaction_setBuffer(aTransaction, mSurfaceControl.get(), nullptr, -1);
          // FIXME: do we need to explicitly unset the surface's buffer here?
          aPrevSurfaces.insert({mSurfaceControl.get(),
                  NativeLayerRootAndroid::ReleasedSurface{
                      std::move(mFrontBuffer), mSurfacePoolHandle}});
      } else {
          mSurfacePoolHandle->ReturnBufferToPool(std::move(mFrontBuffer));
      }
  }
}

}  // namespace mozilla::layers
