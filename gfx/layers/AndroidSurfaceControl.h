/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_ANDROID_SURFACE_CONTROL
#define MOZILLA_LAYERS_ANDROID_SURFACE_CONTROL

#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <queue>
#include <unordered_map>
#include <vector>

#include "mozilla/gfx/Types.h"
#include "mozilla/gfx/2D.h"
#include "nsISupports.h"
#include "mozilla/Monitor.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/RefCounted.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/ThreadSafeWeakPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/webrender/WebRenderTypes.h"

extern "C" {
/**
 * The SurfaceControl API can be used to provide a hierarchy of surfaces for
 * composition to the system compositor. ASurfaceControl represents a content
 * node in this hierarchy.
 */
typedef struct ASurfaceControl ASurfaceControl;

/**
 * ASurfaceTransaction is a collection of updates to the surface tree that must
 * be applied atomically.
 */
typedef struct ASurfaceTransaction ASurfaceTransaction;

/**
 * An opaque handle returned during a callback that can be used to query general
 * stats and stats for surfaces which were either removed or for which buffers
 * were updated after this transaction was applied.
 */
typedef struct ASurfaceTransactionStats ASurfaceTransactionStats;

typedef void (*ASurfaceTransaction_OnComplete)(void* context,
                                               ASurfaceTransactionStats* stats);

/* Parameter for ASurfaceTransaction_setVisibility */
enum {
  ASURFACE_TRANSACTION_VISIBILITY_HIDE = 0,
  ASURFACE_TRANSACTION_VISIBILITY_SHOW = 1,
};

/* Parameter for ASurfaceTransaction_setBufferTransparency */
enum {
  ASURFACE_TRANSACTION_TRANSPARENCY_TRANSPARENT = 0,
  ASURFACE_TRANSACTION_TRANSPARENCY_TRANSLUCENT = 1,
  ASURFACE_TRANSACTION_TRANSPARENCY_OPAQUE = 2,
};
}

namespace mozilla {
namespace layers {

class AndroidHardwareBuffer;
class AndroidSurfaceControlManager;
class AndroidSurfaceTransaction;

/**
 * AndroidSurfaceControlApi provides apis for managing ASurfaceControl,
 * ASurfaceTransaction and ASurfaceTransactionStats. The apis are supported
 * since Android P(APIVersion 29).
 */
class AndroidSurfaceControlApi final {
 public:
  static void Init();
  static void Shutdown();

  static AndroidSurfaceControlApi* Get() { return sInstance; }

  ASurfaceControl* ASurfaceControl_createFromWindow(ANativeWindow* aParent,
                                                    const char* aDebugName);
  void ASurfaceControl_release(ASurfaceControl* aSurfaceControl);

  ASurfaceTransaction* ASurfaceTransaction_create();
  void ASurfaceTransaction_delete(ASurfaceTransaction* aTransaction);
  void ASurfaceTransaction_apply(ASurfaceTransaction* aTransaction);
  void ASurfaceTransaction_setOnComplete(ASurfaceTransaction* aTransaction,
                                         void* aContext,
                                         ASurfaceTransaction_OnComplete aFunc);
  void ASurfaceTransaction_setVisibility(ASurfaceTransaction* aTransaction,
                                         ASurfaceControl* aSurfaceControl,
                                         int8_t aVisibility);
  void ASurfaceTransaction_setZOrder(ASurfaceTransaction* aTransaction,
                                     ASurfaceControl* aSurfaceControl,
                                     int32_t aZOrder);
  void ASurfaceTransaction_setBuffer(ASurfaceTransaction* aTransaction,
                                     ASurfaceControl* aSurfaceControl,
                                     AHardwareBuffer* aBuffer,
                                     int aAcquireFenceFd);
  void ASurfaceTransaction_setColor(ASurfaceTransaction* aTransaction,
                                    ASurfaceControl* aSurfaceControl, float aR,
                                    float aG, float aB, float aAlpha,
                                    ADataSpace aDataspace);
  void ASurfaceTransaction_setGeometry(ASurfaceTransaction* aTransaction,
                                       ASurfaceControl* aSurfaceControl,
                                       const ARect& aSource,
                                       const ARect& aDestination,
                                       int32_t aTransform);
  void ASurfaceTransaction_setBufferTransparency(
      ASurfaceTransaction* aTransaction, ASurfaceControl* aSurfaceControl,
      int8_t aTransparency);
  void ASurfaceTransaction_setDamageRegion(ASurfaceTransaction* aTransaction,
                                           ASurfaceControl* aSurfaceControl,
                                           const ARect aRects[],
                                           uint32_t aCount);
  void ASurfaceTransaction_setBufferAlpha(ASurfaceTransaction* aTransaction,
                                          ASurfaceControl* aSurfaceControl,
                                          float aAlpha);

  int64_t ASurfaceTransactionStats_getLatchTime(
      ASurfaceTransactionStats* aSurfaceTransactionStats);
  int ASurfaceTransactionStats_getPresentFenceFd(
      ASurfaceTransactionStats* aSurfaceTransactionStats);
  void ASurfaceTransactionStats_getASurfaceControls(
      ASurfaceTransactionStats* aSurfaceTransactionStats,
      ASurfaceControl*** aOutASurfaceControls,
      size_t* aOutASurfaceControlsSize);
  void ASurfaceTransactionStats_releaseASurfaceControls(
      ASurfaceControl** aSurfaceControls);
  int ASurfaceTransactionStats_getPreviousReleaseFenceFd(
      ASurfaceTransactionStats* aSurfaceTransactionStats,
      ASurfaceControl* aSurfaceControl);

 private:
  AndroidSurfaceControlApi();
  bool Load();

  // For ASurfaceControl
  using _ASurfaceControl_createFromWindow =
      ASurfaceControl* (*)(ANativeWindow* parent, const char* debug_name);
  using _ASurfaceControl_create = ASurfaceControl* (*)(ASurfaceControl* parent,
                                                       const char* debug_name);
  using _ASurfaceControl_release = void (*)(ASurfaceControl* surface_control);

  // For ASurfaceTransaction
  using _ASurfaceTransaction_create = ASurfaceTransaction* (*)();
  using _ASurfaceTransaction_delete =
      void (*)(ASurfaceTransaction* transaction);
  using _ASurfaceTransaction_apply = void (*)(ASurfaceTransaction* transaction);
  using _ASurfaceTransaction_setOnComplete =
      void (*)(ASurfaceTransaction* transaction, void* context,
               ASurfaceTransaction_OnComplete func);
  using _ASurfaceTransaction_reparent =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, ASurfaceControl* new_parent);
  using _ASurfaceTransaction_setVisibility =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, int8_t visibility);
  using _ASurfaceTransaction_setZOrder =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, int32_t z_order);
  using _ASurfaceTransaction_setBuffer = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      AHardwareBuffer* buffer, int acquire_fence_fd);
  using _ASurfaceTransaction_setColor = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      float r, float g, float b, float alpha, ADataSpace dataspace);
  using _ASurfaceTransaction_setGeometry = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      const ARect& source, const ARect& destination, int32_t transform);
  using _ASurfaceTransaction_setBufferTransparency =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, int8_t transparency);
  using _ASurfaceTransaction_setDamageRegion = void (*)(
      ASurfaceTransaction* transaction, ASurfaceControl* surface_control,
      const ARect rects[], uint32_t count);
  using _ASurfaceTransaction_setBufferAlpha =
      void (*)(ASurfaceTransaction* transaction,
               ASurfaceControl* surface_control, float alpha);

  // For ASurfaceTransactionStats
  using _ASurfaceTransactionStats_getLatchTime =
      int64_t (*)(ASurfaceTransactionStats* surface_transaction_stats);
  using _ASurfaceTransactionStats_getPresentFenceFd =
      int (*)(ASurfaceTransactionStats* surface_transaction_stats);
  using _ASurfaceTransactionStats_getASurfaceControls = void (*)(
      ASurfaceTransactionStats* surface_transaction_stats,
      ASurfaceControl*** outASurfaceControls, size_t* outASurfaceControlsSize);
  using _ASurfaceTransactionStats_releaseASurfaceControls =
      void (*)(ASurfaceControl** surface_controls);
  using _ASurfaceTransactionStats_getPreviousReleaseFenceFd =
      int (*)(ASurfaceTransactionStats* surface_transaction_stats,
              ASurfaceControl* surface_control);

  _ASurfaceControl_createFromWindow mASurfaceControl_createFromWindow = nullptr;
  _ASurfaceControl_create mASurfaceControl_create = nullptr;
  _ASurfaceControl_release mASurfaceControl_release = nullptr;

  _ASurfaceTransaction_create mASurfaceTransaction_create = nullptr;
  _ASurfaceTransaction_delete mASurfaceTransaction_delete = nullptr;
  _ASurfaceTransaction_apply mASurfaceTransaction_apply = nullptr;
  _ASurfaceTransaction_setOnComplete mASurfaceTransaction_setOnComplete =
      nullptr;
  _ASurfaceTransaction_reparent mASurfaceTransaction_reparent = nullptr;
  _ASurfaceTransaction_setVisibility mASurfaceTransaction_setVisibility =
      nullptr;
  _ASurfaceTransaction_setZOrder mASurfaceTransaction_setZOrder = nullptr;
  _ASurfaceTransaction_setBuffer mASurfaceTransaction_setBuffer = nullptr;
  _ASurfaceTransaction_setColor mASurfaceTransaction_setColor = nullptr;
  _ASurfaceTransaction_setGeometry mASurfaceTransaction_setGeometry = nullptr;
  _ASurfaceTransaction_setBufferTransparency
      mASurfaceTransaction_setBufferTransparency = nullptr;
  _ASurfaceTransaction_setDamageRegion mASurfaceTransaction_setDamageRegion =
      nullptr;
  _ASurfaceTransaction_setBufferAlpha mASurfaceTransaction_setBufferAlpha =
      nullptr;

  _ASurfaceTransactionStats_getLatchTime
      mASurfaceTransactionStats_getLatchTime = nullptr;
  _ASurfaceTransactionStats_getPresentFenceFd
      mASurfaceTransactionStats_getPresentFenceFd = nullptr;
  _ASurfaceTransactionStats_getASurfaceControls
      mASurfaceTransactionStats_getASurfaceControls = nullptr;
  _ASurfaceTransactionStats_releaseASurfaceControls
      mASurfaceTransactionStats_releaseASurfaceControls = nullptr;
  _ASurfaceTransactionStats_getPreviousReleaseFenceFd
      mASurfaceTransactionStats_getPreviousReleaseFenceFd = nullptr;

  static StaticAutoPtr<AndroidSurfaceControlApi> sInstance;
};

class AndroidSurfaceControl
    : public SupportsThreadSafeWeakPtr<AndroidSurfaceControl> {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(AndroidSurfaceControl)

  AndroidSurfaceControl(AndroidSurfaceControlManager* aOwner,
                        ASurfaceControl* aASurfaceControl);
  virtual ~AndroidSurfaceControl();

  void SetVisibility(int8_t aVisibility);
  void SetZOrder(int32_t aZOrder);
  void SetBuffer(AHardwareBuffer* aBuffer, int aAcquireFenceFd);
  void SetColor(float aR, float aG, float aB, float aAlpha);
  void SetGeometry(const ARect& aSource, const ARect& aDestination,
                   int32_t aTransform);
  void SetBufferTransparency(int8_t aTransparency);
  void SetDamageRegion(const ARect aRects[], uint32_t aCount);
  void SetBufferAlpha(float aAlpha);

 protected:
  RefPtr<AndroidSurfaceControlManager> mOwner;
  ASurfaceControl* mASurfaceControl;

  friend class AndroidSurfaceControlManager;
};

class AndroidSurfaceTransaction {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AndroidSurfaceTransaction)

  static already_AddRefed<AndroidSurfaceTransaction> Create();

 protected:
  AndroidSurfaceTransaction(ASurfaceTransaction* aASurfaceTransaction);
  ~AndroidSurfaceTransaction();

  ASurfaceTransaction* mASurfaceTransaction;

  friend class AndroidSurfaceControlManager;
};

struct AndroidSurfaceControlStats {
  ASurfaceControl* mASurfaceControl = nullptr;
  ipc::FileDescriptor mPreviousReleaseFenceFd;
};

struct AndroidTransactionStats {
  // when the frame was latched by the framework. Once a frame is
  // latched by the framework, it is presented at the following hardware vsync.
  int64_t mLatchTime = 0;
  TimeStamp mStart;
  TimeStamp mEnd;

  ipc::FileDescriptor mPresentFenceFd;

  std::vector<AndroidSurfaceControlStats> mSurfaceControlStats;
};

class AndroidSurfaceControlManager {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AndroidSurfaceControlManager)

  static already_AddRefed<AndroidSurfaceControlManager> Create();

  already_AddRefed<AndroidSurfaceControl> createFromWindow(
      ANativeWindow* aParent);

  void Commit(wr::RenderedFrameId aFrameId);

  bool WaitForFrameComplete(wr::RenderedFrameId aFrameId);

 protected:
  AndroidSurfaceControlManager();
  ~AndroidSurfaceControlManager();

  void Register(RefPtr<AndroidSurfaceControl> aSurfaceControl);

  void Unregister(AndroidSurfaceControl* aSurfaceControl);

  already_AddRefed<AndroidSurfaceControl> GetSurfaceControl(
      ASurfaceControl* aNativeSurfaceControl);

  ASurfaceTransaction* GetASurfaceTransaction();

  void ApplyTransaction(RefPtr<AndroidSurfaceTransaction> aTransaction);

  static void HandleOnComplete(void* aContext,
                               ASurfaceTransactionStats* aStats);
  // Called on Binder thread
  void DoHandleOnComplete(wr::RenderedFrameId aFrameId,
                          UniquePtr<AndroidTransactionStats> aStats);

  struct TxnCompleteContext {
    TxnCompleteContext() {}
    ~TxnCompleteContext() {}

    RefPtr<AndroidSurfaceControlManager> mManager = nullptr;
    wr::RenderedFrameId mFrameId;
    TimeStamp mStart;
  };

  RefPtr<AndroidSurfaceTransaction> mPendingTransaction;

  Monitor mMonitor;
  wr::RenderedFrameId mLastCompletedFrameId;
  std::queue<std::pair<wr::RenderedFrameId, UniquePtr<AndroidTransactionStats>>>
      mCompletedFrames;

  Mutex mLock;
  std::unordered_map<ASurfaceControl*, ThreadSafeWeakPtr<AndroidSurfaceControl>>
      mSurfaceControls;

  friend class AndroidSurfaceControl;
};

}  // namespace layers
}  // namespace mozilla

#endif
