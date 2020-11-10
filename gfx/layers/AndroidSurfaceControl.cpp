/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidSurfaceControl.h"

#include <dlfcn.h>
#include <string>

#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/AndroidHardwareBuffer.h"
#include "mozilla/UniquePtrExtensions.h"

namespace mozilla {
namespace layers {

StaticAutoPtr<AndroidSurfaceControlApi> AndroidSurfaceControlApi::sInstance;

/* static */
void AndroidSurfaceControlApi::Init() {
  sInstance = new AndroidSurfaceControlApi();
  if (!sInstance->Load()) {
    sInstance = nullptr;
  }
}

/* static */
void AndroidSurfaceControlApi::Shutdown() { sInstance = nullptr; }

AndroidSurfaceControlApi::AndroidSurfaceControlApi() {}

bool AndroidSurfaceControlApi::Load() {
  void* handle = dlopen("libandroid.so", RTLD_LAZY | RTLD_LOCAL);
  MOZ_ASSERT(handle);
  if (!handle) {
    gfxCriticalNote << "Failed to load libmediandk.so";
    return false;
  }

  mASurfaceControl_createFromWindow = (_ASurfaceControl_createFromWindow)dlsym(
      handle, "ASurfaceControl_createFromWindow");
  mASurfaceControl_create =
      (_ASurfaceControl_create)dlsym(handle, "ASurfaceControl_create");
  mASurfaceControl_release =
      (_ASurfaceControl_release)dlsym(handle, "ASurfaceControl_release");

  mASurfaceTransaction_create =
      (_ASurfaceTransaction_create)dlsym(handle, "ASurfaceTransaction_create");
  mASurfaceTransaction_delete =
      (_ASurfaceTransaction_delete)dlsym(handle, "ASurfaceTransaction_delete");
  mASurfaceTransaction_apply =
      (_ASurfaceTransaction_apply)dlsym(handle, "ASurfaceTransaction_apply");
  mASurfaceTransaction_setOnComplete =
      (_ASurfaceTransaction_setOnComplete)dlsym(
          handle, "ASurfaceTransaction_setOnComplete");
  mASurfaceTransaction_reparent = (_ASurfaceTransaction_reparent)dlsym(
      handle, "ASurfaceTransaction_reparent");
  mASurfaceTransaction_setVisibility =
      (_ASurfaceTransaction_setVisibility)dlsym(
          handle, "ASurfaceTransaction_setVisibility");
  mASurfaceTransaction_setZOrder = (_ASurfaceTransaction_setZOrder)dlsym(
      handle, "ASurfaceTransaction_setZOrder");
  mASurfaceTransaction_setBuffer = (_ASurfaceTransaction_setBuffer)dlsym(
      handle, "ASurfaceTransaction_setBuffer");
  mASurfaceTransaction_setColor = (_ASurfaceTransaction_setColor)dlsym(
      handle, "ASurfaceTransaction_setColor");
  mASurfaceTransaction_setGeometry = (_ASurfaceTransaction_setGeometry)dlsym(
      handle, "ASurfaceTransaction_setGeometry");
  mASurfaceTransaction_setBufferTransparency =
      (_ASurfaceTransaction_setBufferTransparency)dlsym(
          handle, "ASurfaceTransaction_setBufferTransparency");
  mASurfaceTransaction_setDamageRegion =
      (_ASurfaceTransaction_setDamageRegion)dlsym(
          handle, "ASurfaceTransaction_setDamageRegion");
  mASurfaceTransaction_setBufferAlpha =
      (_ASurfaceTransaction_setBufferAlpha)dlsym(
          handle, "ASurfaceTransaction_setBufferAlpha");

  mASurfaceTransactionStats_getLatchTime =
      (_ASurfaceTransactionStats_getLatchTime)dlsym(
          handle, "ASurfaceTransactionStats_getLatchTime");
  mASurfaceTransactionStats_getPresentFenceFd =
      (_ASurfaceTransactionStats_getPresentFenceFd)dlsym(
          handle, "ASurfaceTransactionStats_getPresentFenceFd");
  mASurfaceTransactionStats_getASurfaceControls =
      (_ASurfaceTransactionStats_getASurfaceControls)dlsym(
          handle, "ASurfaceTransactionStats_getASurfaceControls");
  mASurfaceTransactionStats_releaseASurfaceControls =
      (_ASurfaceTransactionStats_releaseASurfaceControls)dlsym(
          handle, "ASurfaceTransactionStats_releaseASurfaceControls");
  mASurfaceTransactionStats_getPreviousReleaseFenceFd =
      (_ASurfaceTransactionStats_getPreviousReleaseFenceFd)dlsym(
          handle, "ASurfaceTransactionStats_getPreviousReleaseFenceFd");

  if (!mASurfaceControl_createFromWindow | !mASurfaceControl_create |
      !mASurfaceControl_release | !mASurfaceTransaction_create |
      !mASurfaceTransaction_delete | !mASurfaceTransaction_apply |
      !mASurfaceTransaction_setOnComplete | !mASurfaceTransaction_reparent |
      !mASurfaceTransaction_setVisibility | !mASurfaceTransaction_setZOrder |
      !mASurfaceTransaction_setBuffer | !mASurfaceTransaction_setColor |
      !mASurfaceTransaction_setGeometry |
      !mASurfaceTransaction_setBufferTransparency |
      !mASurfaceTransaction_setDamageRegion |
      !mASurfaceTransaction_setBufferAlpha |
      !mASurfaceTransactionStats_getLatchTime |
      !mASurfaceTransactionStats_getPresentFenceFd |
      !mASurfaceTransactionStats_getASurfaceControls |
      !mASurfaceTransactionStats_releaseASurfaceControls |
      !mASurfaceTransactionStats_getPreviousReleaseFenceFd) {
    gfxCriticalNote << "Failed to load ASurfaceControl";
    return false;
  }

  return true;
}

ASurfaceControl* AndroidSurfaceControlApi::ASurfaceControl_createFromWindow(
    ANativeWindow* aParent, const char* aDebugName) {
  return mASurfaceControl_createFromWindow(aParent, aDebugName);
}

void AndroidSurfaceControlApi::ASurfaceControl_release(
    ASurfaceControl* aSurfaceControl) {
  mASurfaceControl_release(aSurfaceControl);
}

ASurfaceTransaction* AndroidSurfaceControlApi::ASurfaceTransaction_create() {
  return mASurfaceTransaction_create();
}

void AndroidSurfaceControlApi::ASurfaceTransaction_delete(
    ASurfaceTransaction* aTransaction) {
  mASurfaceTransaction_delete(aTransaction);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_apply(
    ASurfaceTransaction* aTransaction) {
  mASurfaceTransaction_apply(aTransaction);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setOnComplete(
    ASurfaceTransaction* aTransaction, void* aContext,
    ASurfaceTransaction_OnComplete aFunc) {
  mASurfaceTransaction_setOnComplete(aTransaction, aContext, aFunc);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setVisibility(
    ASurfaceTransaction* aTransaction, ASurfaceControl* aSurfaceControl,
    int8_t aVisibility) {
  mASurfaceTransaction_setVisibility(aTransaction, aSurfaceControl,
                                     aVisibility);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setZOrder(
    ASurfaceTransaction* aTransaction, ASurfaceControl* aSurfaceControl,
    int32_t aZOrder) {
  mASurfaceTransaction_setZOrder(aTransaction, aSurfaceControl, aZOrder);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setBuffer(
    ASurfaceTransaction* aTransaction, ASurfaceControl* aSurfaceControl,
    AHardwareBuffer* aBuffer, int aAcquireFenceFd) {
  mASurfaceTransaction_setBuffer(aTransaction, aSurfaceControl, aBuffer,
                                 aAcquireFenceFd);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setColor(
    ASurfaceTransaction* aTransaction, ASurfaceControl* aSurfaceControl,
    float aR, float aG, float aB, float aAlpha, ADataSpace aDataspace) {
  mASurfaceTransaction_setColor(aTransaction, aSurfaceControl, aR, aG, aB,
                                aAlpha, aDataspace);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setGeometry(
    ASurfaceTransaction* aTransaction, ASurfaceControl* aSurfaceControl,
    const ARect& aSource, const ARect& aDestination, int32_t aTransform) {
  mASurfaceTransaction_setGeometry(aTransaction, aSurfaceControl, aSource,
                                   aDestination, aTransform);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setBufferTransparency(
    ASurfaceTransaction* aTransaction, ASurfaceControl* aSurfaceControl,
    int8_t aTransparency) {
  mASurfaceTransaction_setBufferTransparency(aTransaction, aSurfaceControl,
                                             aTransparency);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setDamageRegion(
    ASurfaceTransaction* aTransaction, ASurfaceControl* aSurfaceControl,
    const ARect aRects[], uint32_t aCount) {
  mASurfaceTransaction_setDamageRegion(aTransaction, aSurfaceControl, aRects,
                                       aCount);
}

void AndroidSurfaceControlApi::ASurfaceTransaction_setBufferAlpha(
    ASurfaceTransaction* aTransaction, ASurfaceControl* aSurfaceControl,
    float aAlpha) {
  mASurfaceTransaction_setBufferAlpha(aTransaction, aSurfaceControl, aAlpha);
}

int64_t AndroidSurfaceControlApi::ASurfaceTransactionStats_getLatchTime(
    ASurfaceTransactionStats* aSurfaceTransactionStats) {
  return mASurfaceTransactionStats_getLatchTime(aSurfaceTransactionStats);
}

int AndroidSurfaceControlApi::ASurfaceTransactionStats_getPresentFenceFd(
    ASurfaceTransactionStats* aSurfaceTransactionStats) {
  return mASurfaceTransactionStats_getPresentFenceFd(aSurfaceTransactionStats);
}

void AndroidSurfaceControlApi::ASurfaceTransactionStats_getASurfaceControls(
    ASurfaceTransactionStats* aSurfaceTransactionStats,
    ASurfaceControl*** aOutASurfaceControls, size_t* aOutASurfaceControlsSize) {
  mASurfaceTransactionStats_getASurfaceControls(
      aSurfaceTransactionStats, aOutASurfaceControls, aOutASurfaceControlsSize);
}

void AndroidSurfaceControlApi::ASurfaceTransactionStats_releaseASurfaceControls(
    ASurfaceControl** aSurfaceControls) {
  mASurfaceTransactionStats_releaseASurfaceControls(aSurfaceControls);
}

int AndroidSurfaceControlApi::
    ASurfaceTransactionStats_getPreviousReleaseFenceFd(
        ASurfaceTransactionStats* aSurfaceTransactionStats,
        ASurfaceControl* aSurfaceControl) {
  return mASurfaceTransactionStats_getPreviousReleaseFenceFd(
      aSurfaceTransactionStats, aSurfaceControl);
}

AndroidSurfaceControl::AndroidSurfaceControl(
    AndroidSurfaceControlManager* aOwner, ASurfaceControl* aASurfaceControl)
    : mOwner(aOwner), mASurfaceControl(aASurfaceControl) {
  MOZ_ASSERT(mASurfaceControl);
}

AndroidSurfaceControl::~AndroidSurfaceControl() {
  mOwner->Unregister(this);
  AndroidSurfaceControlApi::Get()->ASurfaceControl_release(mASurfaceControl);
}

void AndroidSurfaceControl::SetVisibility(int8_t aVisibility) {
  AndroidSurfaceControlApi::Get()->ASurfaceTransaction_setVisibility(
      mOwner->GetASurfaceTransaction(), mASurfaceControl, aVisibility);
}

void AndroidSurfaceControl::SetZOrder(int32_t aZOrder) {
  AndroidSurfaceControlApi::Get()->ASurfaceTransaction_setZOrder(
      mOwner->GetASurfaceTransaction(), mASurfaceControl, aZOrder);
}

void AndroidSurfaceControl::SetBuffer(AHardwareBuffer* aBuffer,
                                      int aAcquireFenceFd) {
  AndroidSurfaceControlApi::Get()->ASurfaceTransaction_setBuffer(
      mOwner->GetASurfaceTransaction(), mASurfaceControl, aBuffer,
      aAcquireFenceFd);
}

void AndroidSurfaceControl::SetColor(float aR, float aG, float aB,
                                     float aAlpha) {
  AndroidSurfaceControlApi::Get()->ASurfaceTransaction_setColor(
      mOwner->GetASurfaceTransaction(), mASurfaceControl, aR, aG, aB, aAlpha,
      ADATASPACE_UNKNOWN);
}

void AndroidSurfaceControl::SetGeometry(const ARect& aSource,
                                        const ARect& aDestination,
                                        int32_t aTransform) {
  AndroidSurfaceControlApi::Get()->ASurfaceTransaction_setGeometry(
      mOwner->GetASurfaceTransaction(), mASurfaceControl, aSource, aDestination,
      aTransform);
}

void AndroidSurfaceControl::SetBufferTransparency(int8_t aTransparency) {
  AndroidSurfaceControlApi::Get()->ASurfaceTransaction_setBufferTransparency(
      mOwner->GetASurfaceTransaction(), mASurfaceControl, aTransparency);
}

void AndroidSurfaceControl::SetDamageRegion(const ARect aRects[],
                                            uint32_t aCount) {
  AndroidSurfaceControlApi::Get()->ASurfaceTransaction_setDamageRegion(
      mOwner->GetASurfaceTransaction(), mASurfaceControl, aRects, aCount);
}

void AndroidSurfaceControl::SetBufferAlpha(float aAlpha) {
  AndroidSurfaceControlApi::Get()->ASurfaceTransaction_setBufferAlpha(
      mOwner->GetASurfaceTransaction(), mASurfaceControl, aAlpha);
}

/* static */
already_AddRefed<AndroidSurfaceTransaction>
AndroidSurfaceTransaction::Create() {
  ASurfaceTransaction* nativeTransaction =
      AndroidSurfaceControlApi::Get()->ASurfaceTransaction_create();
  if (!nativeTransaction) {
    gfxCriticalNote << "ASurfaceTransaction_create failed";
    return nullptr;
  }

  RefPtr<AndroidSurfaceTransaction> surfaceControl =
      new AndroidSurfaceTransaction(nativeTransaction);
  return surfaceControl.forget();
}

AndroidSurfaceTransaction::AndroidSurfaceTransaction(
    ASurfaceTransaction* aASurfaceTransaction)
    : mASurfaceTransaction(aASurfaceTransaction) {}

AndroidSurfaceTransaction::~AndroidSurfaceTransaction() {
  AndroidSurfaceControlApi::Get()->ASurfaceTransaction_delete(
      mASurfaceTransaction);
}

/* static */
already_AddRefed<AndroidSurfaceControlManager>
AndroidSurfaceControlManager::Create() {
  RefPtr<AndroidSurfaceControlManager> manager =
      new AndroidSurfaceControlManager();
  return manager.forget();
}

AndroidSurfaceControlManager::AndroidSurfaceControlManager()
    : mMonitor("AndroidHardwareBufferManager.mMonitor"),
      mLock("AndroidSurfaceControl.mLock") {}

AndroidSurfaceControlManager::~AndroidSurfaceControlManager() {}

void AndroidSurfaceControlManager::Register(
    RefPtr<AndroidSurfaceControl> aSurfaceControl) {
  MutexAutoLock lock(mLock);

  ThreadSafeWeakPtr<AndroidSurfaceControl> weak(aSurfaceControl);

#ifdef DEBUG
  const auto it = mSurfaceControls.find(aSurfaceControl->mASurfaceControl);
  MOZ_ASSERT(it == mSurfaceControls.end());
#endif
  mSurfaceControls.emplace(aSurfaceControl->mASurfaceControl, weak);
}

void AndroidSurfaceControlManager::Unregister(
    AndroidSurfaceControl* aSurfaceControl) {
  MutexAutoLock lock(mLock);

  const auto it = mSurfaceControls.find(aSurfaceControl->mASurfaceControl);
  MOZ_ASSERT(it != mSurfaceControls.end());
  if (it == mSurfaceControls.end()) {
    gfxCriticalNote << "ASurfaceControl mismatch happened";
    return;
  }
  mSurfaceControls.erase(it);
}

already_AddRefed<AndroidSurfaceControl>
AndroidSurfaceControlManager::GetSurfaceControl(
    ASurfaceControl* aNativeSurfaceControl) {
  MutexAutoLock lock(mLock);

  const auto it = mSurfaceControls.find(aNativeSurfaceControl);
  if (it == mSurfaceControls.end()) {
    return nullptr;
  }
  auto surfaceControl = RefPtr<AndroidSurfaceControl>(it->second);
  return surfaceControl.forget();
}

ASurfaceTransaction* AndroidSurfaceControlManager::GetASurfaceTransaction() {
  if (!mPendingTransaction) {
    mPendingTransaction = AndroidSurfaceTransaction::Create();
  }

  if (!mPendingTransaction) {
    return nullptr;
  }

  return mPendingTransaction->mASurfaceTransaction;
}

already_AddRefed<AndroidSurfaceControl>
AndroidSurfaceControlManager::createFromWindow(ANativeWindow* aParent) {
  if (!mPendingTransaction) {
    mPendingTransaction = AndroidSurfaceTransaction::Create();
  }

  if (!mPendingTransaction) {
    return nullptr;
  }

  const std::string debugName = "SurfaceControl_createFromWindow";
  ASurfaceControl* nativeControl =
      AndroidSurfaceControlApi::Get()->ASurfaceControl_createFromWindow(
          aParent, debugName.c_str());
  if (!nativeControl) {
    gfxCriticalNote << "ASurfaceControl_createFromWindow failed";
    return nullptr;
  }

  RefPtr<AndroidSurfaceControl> surfaceControl =
      new AndroidSurfaceControl(this, nativeControl);
  Register(surfaceControl);
  return surfaceControl.forget();
}

/* static */
void AndroidSurfaceControlManager::HandleOnComplete(
    void* aContext, ASurfaceTransactionStats* aStats) {
  auto* context = static_cast<TxnCompleteContext*>(aContext);
  RefPtr<AndroidSurfaceControlManager> manager = context->mManager.forget();

  // XXX unique pointer
  UniquePtr<AndroidTransactionStats> transactionStats =
      MakeUnique<AndroidTransactionStats>();

  auto begin = TimeStamp::Now();

  transactionStats->mLatchTime =
      AndroidSurfaceControlApi::Get()->ASurfaceTransactionStats_getLatchTime(
          aStats);

  // Get present fence
  int fenceFd = AndroidSurfaceControlApi::Get()
                    ->ASurfaceTransactionStats_getPresentFenceFd(aStats);
  if (fenceFd >= 0) {
    transactionStats->mPresentFenceFd =
        ipc::FileDescriptor(UniqueFileHandle(fenceFd));
  }

  ASurfaceControl** surfaceControls = nullptr;
  size_t size = 0;
  AndroidSurfaceControlApi::Get()->ASurfaceTransactionStats_getASurfaceControls(
      aStats, &surfaceControls, &size);

  transactionStats->mSurfaceControlStats.resize(size);
  for (size_t i = 0; i < size; i++) {
    transactionStats->mSurfaceControlStats[i].mASurfaceControl =
        surfaceControls[i];
    int fenceFd = AndroidSurfaceControlApi::Get()
                      ->ASurfaceTransactionStats_getPreviousReleaseFenceFd(
                          aStats, surfaceControls[i]);
    if (fenceFd >= 0) {
      transactionStats->mSurfaceControlStats[i].mPreviousReleaseFenceFd =
          ipc::FileDescriptor(UniqueFileHandle(fenceFd));
    }
  }

  auto end = TimeStamp::Now();

  printf_stderr(
      "AndroidSurfaceControlManager::HandleOnComplete() duration %f us\n",
      (end - begin).ToMicroseconds());

  transactionStats->mStart = context->mStart;
  transactionStats->mEnd = TimeStamp::Now();

  AndroidSurfaceControlApi::Get()
      ->ASurfaceTransactionStats_releaseASurfaceControls(surfaceControls);

  manager->DoHandleOnComplete(context->mFrameId, std::move(transactionStats));

  delete context;

  // XXX release manager on created thread?
}

void AndroidSurfaceControlManager::DoHandleOnComplete(
    wr::RenderedFrameId aFrameId, UniquePtr<AndroidTransactionStats> aStats) {
  // MOZ_ASSERT(mWaitingTransactionComplete);

  MonitorAutoLock lock(mMonitor);

  mLastCompletedFrameId = aFrameId;
  mCompletedFrames.emplace(aFrameId, std::move(aStats));
  mMonitor.NotifyAll();
}

void AndroidSurfaceControlManager::Commit(wr::RenderedFrameId aFrameId) {
  if (!mPendingTransaction) {
    return;
  }

  TxnCompleteContext* context = new TxnCompleteContext;
  context->mManager = this;
  context->mFrameId = aFrameId;
  context->mStart = TimeStamp::Now();

  AndroidSurfaceControlApi::Get()->ASurfaceTransaction_setOnComplete(
      mPendingTransaction->mASurfaceTransaction, context, &HandleOnComplete);
  ApplyTransaction(mPendingTransaction.forget());
}

void AndroidSurfaceControlManager::ApplyTransaction(
    RefPtr<AndroidSurfaceTransaction> aTransaction) {
  MOZ_ASSERT(aTransaction);

  AndroidSurfaceControlApi::Get()->ASurfaceTransaction_apply(
      aTransaction->mASurfaceTransaction);
}

bool AndroidSurfaceControlManager::WaitForFrameComplete(
    wr::RenderedFrameId aFrameId) {
  MonitorAutoLock lock(mMonitor);

  // XXX
  while (mCompletedFrames.size() > 0) {
    auto& stats = mCompletedFrames.front().second;
    printf_stderr(
        "AndroidSurfaceControlManager::WaitForFrameComplete() duration %f us "
        "this %p\n",
        (stats->mEnd - stats->mStart).ToMicroseconds(), this);

    mCompletedFrames.pop();
  }

  if (aFrameId <= mLastCompletedFrameId) {
    return true;
  }

  const double waitWarningTimeoutMs = 300;
  const double maxTimeoutSec = 3;
  auto begin = TimeStamp::Now();

  bool isWaiting = true;
  while (isWaiting) {
    TimeDuration timeout = TimeDuration::FromMilliseconds(waitWarningTimeoutMs);
    CVStatus status = mMonitor.Wait(timeout);
    if (status == CVStatus::Timeout) {
      gfxCriticalNoteOnce << "AndroidSurfaceControlManager wait is slow";
    }

    if (aFrameId <= mLastCompletedFrameId) {
      return true;
    }
    auto now = TimeStamp::Now();
    if ((now - begin).ToSeconds() > maxTimeoutSec) {
      isWaiting = false;
      gfxCriticalNote << "AndroidSurfaceControlManager wait timeout";
    }
  }

  return false;
}

}  // namespace layers
}  // namespace mozilla
