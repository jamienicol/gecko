/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidVsync.h"

#include "AndroidBridge.h"
#include "AndroidChoreographer.h"
#include "AndroidUiThread.h"
#include "mozilla/RecursiveMutex.h"
#include "nsTArray.h"

/**
 * Implementation for the AndroidVsync class.
 */

namespace mozilla {
namespace widget {

StaticDataMutex<ThreadSafeWeakPtr<AndroidVsync>> AndroidVsync::sInstance(
    "AndroidVsync::sInstance");

/* static */ RefPtr<AndroidVsync> AndroidVsync::GetInstance() {
  auto weakInstance = sInstance.Lock();
  RefPtr<AndroidVsync> instance(*weakInstance);
  if (!instance) {
    instance = new AndroidVsync();
    *weakInstance = instance;
  }
  return instance;
}

/**
 * Owned by the Java AndroidVsync instance.
 */
class AndroidVsyncSupport final
    : public java::AndroidVsync::Natives<AndroidVsyncSupport> {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AndroidVsyncSupport)

  using Base = java::AndroidVsync::Natives<AndroidVsyncSupport>;
  using Base::AttachNative;
  using Base::DisposeNative;

  explicit AndroidVsyncSupport(AndroidVsync* aAndroidVsync)
      : mAndroidVsync(std::move(aAndroidVsync),
                      "AndroidVsyncSupport::mAndroidVsync") {}

  // Called by Java
  void NotifyVsync(const java::AndroidVsync::LocalRef& aInstance,
                   int64_t aFrameTimeNanos) {
    auto androidVsync = mAndroidVsync.Lock();
    if (*androidVsync) {
      (*androidVsync)->NotifyVsync(aFrameTimeNanos);
    }
  }

  // Called by the AndroidVsync destructor
  void Unlink() {
    auto androidVsync = mAndroidVsync.Lock();
    *androidVsync = nullptr;
  }

 protected:
  ~AndroidVsyncSupport() = default;

  DataMutex<AndroidVsync*> mAndroidVsync;
};

class AndroidNativeVsync {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AndroidNativeVsync);

  AndroidNativeVsync(AndroidVsync* aAndroidVsync)
      : mMutex("AndroidNativeVsync::mMutex"), mAndroidVsync(aAndroidVsync) {}

  // Called by NDK callback
  void NotifyVsync(int64_t aFrameTimeNanos) {
    printf_stderr("jamiedbg AndroidNativeVsync::NotifyVsync()");
    auto lock = RecursiveMutexAutoLock(mMutex);
    mPendingCallback = false;
    if (mObservingVsync) {
      PostCallback();
      if (mAndroidVsync) {
        mAndroidVsync->NotifyVsync(aFrameTimeNanos);
      }
    }
  }

  // Called by the AndroidVsync destructor
  void Unlink() {
    printf_stderr("jamiedbg AndroidNativeVsync::Unlink()");
    auto lock = RecursiveMutexAutoLock(mMutex);
    mAndroidVsync = nullptr;
  }

  bool ObserveVsync(bool aEnable) {
    printf_stderr("jamiedbg AndroidNativeVsync::ObserveVsync() enable: %d",
                  aEnable);
    auto lock = RecursiveMutexAutoLock(mMutex);
    if (aEnable != mObservingVsync) {
      mObservingVsync = aEnable;
      if (mAndroidVsync && mObservingVsync) {
        PostCallback();
      }
    }
    return mObservingVsync;
  }

 private:
  ~AndroidNativeVsync() = default;

  void PostCallback() {
    auto lock = RecursiveMutexAutoLock(mMutex);
    printf_stderr(
        "jamiedbg AndroidNativeVsync::PostCallback() mChoreographer: %p\n",
        mChoreographer);
    RefPtr<nsThread> uiThread = GetAndroidUiThread();
    if (!mChoreographer && !uiThread->IsOnCurrentThread()) {
      printf_stderr("jamiedbg mChoreographer is null and not on UI thread");
      uiThread->Dispatch(
          NewRunnableMethod<>("AndroidNativeVsync::PostCallback", this,
                              &AndroidNativeVsync::PostCallback),
          nsIThread::DISPATCH_NORMAL);
      return;
    }

    const auto* api = AndroidChoreographerApi::Get();

    if (!mChoreographer) {
      printf_stderr("jamiedbg Acquiring choreographer on UI thread");
      MOZ_ASSERT(uiThread->IsOnCurrentThread());
      mChoreographer = api->AChoreographer_getInstance();
    }

    MOZ_ASSERT(mChoreographer);

    // FIXME: if we enable and disable multiple times in a single interval then
    // we will post multiple callbacks, and therefore AddRef multiple times, but
    // we only release once in the callback.
    if (mPendingCallback) {
      return;
    }
    AddRef();
    printf_stderr("jamiedbg posting callback");
    api->AChoreographer_postFrameCallback64(
        mChoreographer,
        [](int64_t frameTimeNanos, void* data) {
          printf_stderr("jamiedbg frame callback");
          AndroidNativeVsync* self = (AndroidNativeVsync*)data;
          self->NotifyVsync(frameTimeNanos);
          self->Release();
        },
        this);
    printf_stderr("jamiedbg finished posting callback");
  }

  RecursiveMutex mMutex;
  AndroidVsync* mAndroidVsync MOZ_GUARDED_BY(mMutex);
  AChoreographer* mChoreographer MOZ_GUARDED_BY(mMutex) = nullptr;
  bool mObservingVsync MOZ_GUARDED_BY(mMutex) = false;
  bool mPendingCallback MOZ_GUARDED_BY(mMutex) = false;
};

AndroidVsync::AndroidVsync() : mImpl("AndroidVsync.mImpl") {
  // FIXME: would like to put this in gfxPlatform, but the vsync singleton gets
  // initialized before then
  AndroidChoreographerApi::Init();
  AndroidVsyncSupport::Init();

  auto impl = mImpl.Lock();
  if (true) {  // FIXME: if required NDK functions are supported
    impl->mNative = new AndroidNativeVsync(this);
  } else {
    impl->mSupport = new AndroidVsyncSupport(this);
    impl->mSupportJava = java::AndroidVsync::New();
    AndroidVsyncSupport::AttachNative(impl->mSupportJava, impl->mSupport);
  }
}

AndroidVsync::~AndroidVsync() {
  auto impl = mImpl.Lock();
  impl->mInputObservers.Clear();
  impl->mRenderObservers.Clear();
  impl->UpdateObservingVsync();
  if (impl->mNative) {
    impl->mNative->Unlink();
  } else {
    impl->mSupport->Unlink();
  }
}

void AndroidVsync::RegisterObserver(Observer* aObserver, ObserverType aType) {
  auto impl = mImpl.Lock();
  if (aType == AndroidVsync::INPUT) {
    impl->mInputObservers.AppendElement(aObserver);
  } else {
    impl->mRenderObservers.AppendElement(aObserver);
  }
  impl->UpdateObservingVsync();
}

void AndroidVsync::UnregisterObserver(Observer* aObserver, ObserverType aType) {
  auto impl = mImpl.Lock();
  if (aType == AndroidVsync::INPUT) {
    impl->mInputObservers.RemoveElement(aObserver);
  } else {
    impl->mRenderObservers.RemoveElement(aObserver);
  }
  aObserver->Dispose();
  impl->UpdateObservingVsync();
}

void AndroidVsync::Impl::UpdateObservingVsync() {
  bool shouldObserve =
      !mInputObservers.IsEmpty() || !mRenderObservers.IsEmpty();
  if (shouldObserve != mObservingVsync) {
    if (mNative) {
      mObservingVsync = mNative->ObserveVsync(shouldObserve);
    } else {
      mObservingVsync = mSupportJava->ObserveVsync(shouldObserve);
    }
  }
}

// Always called on the Java UI thread.
void AndroidVsync::NotifyVsync(int64_t aFrameTimeNanos) {
  MOZ_ASSERT(AndroidBridge::IsJavaUiThread());

  // Convert aFrameTimeNanos to a TimeStamp. The value converts trivially to
  // the internal ticks representation of TimeStamp_posix; both use the
  // monotonic clock and are in nanoseconds.
  TimeStamp timeStamp = TimeStamp::FromSystemTime(aFrameTimeNanos);

  // Do not keep the lock held while calling OnVsync.
  nsTArray<Observer*> observers;
  {
    auto impl = mImpl.Lock();
    observers.AppendElements(impl->mInputObservers);
    observers.AppendElements(impl->mRenderObservers);
  }
  for (Observer* observer : observers) {
    observer->OnVsync(timeStamp);
  }
}

void AndroidVsync::OnMaybeUpdateRefreshRate() {
  MOZ_ASSERT(NS_IsMainThread());

  auto impl = mImpl.Lock();

  nsTArray<Observer*> observers;
  observers.AppendElements(impl->mRenderObservers);

  for (Observer* observer : observers) {
    observer->OnMaybeUpdateRefreshRate();
  }
}

}  // namespace widget
}  // namespace mozilla
