/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MediaCodecDecoder_h_
#define MediaCodecDecoder_h_

#include "PlatformDecoderModule.h"
#include "MediaData.h"
#include "mozilla/Atomics.h"
#include "mozilla/ThreadSafeWeakPtr.h"
#include "nsDeque.h"
#include "nsTArrayForwardDeclare.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaError.h>
#include <media/NdkMediaFormat.h>

namespace mozilla {

class AsyncMediaCodec : public SupportsThreadSafeWeakPtr<AsyncMediaCodec> {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(AsyncMediaCodec)

  struct Buffer {
   public:
    friend class AsyncMediaCodec;

    Buffer(size_t aIndex, uint64_t aSession)
        : mIndex(aIndex), mSession(aSession) {}

   private:
    const size_t mIndex;
    const uint64_t mSession;
  };

  class Callbacks {
   public:
    NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

    virtual void OnAsyncInputAvailable(Buffer aBuffer) = 0;
    virtual void OnAsyncOutputAvailable(Buffer aBuffer,
                                        AMediaCodecBufferInfo aBufferInfo) = 0;
    virtual void OnAsyncFormatChanged(AMediaFormat* aFormat) = 0;
    virtual void OnAsyncError(media_status_t aError, int32_t aActionCode,
                              const char* aDetail) = 0;
  };

  static RefPtr<AsyncMediaCodec> Create(AMediaFormat* aFormat,
                                        Callbacks* aCallbacks,
                                        ANativeWindow* aNativeWindow,
                                        bool aIsEncoder);
  ~AsyncMediaCodec();

  bool Start();
  bool Flush();
  bool Stop();

  Span<uint8_t> GetInputBuffer(Buffer aBuffer);
  bool QueueInputBuffer(Buffer aBuffer, off_t aOffset, size_t aSize,
                        uint64_t aTime, uint32_t aFlags);
  bool ReleaseOutputBuffer(Buffer aBuffer, bool aRender);

  bool IsHardwareAccelerated() const { return mIsHardwareAccelerated; }

 private:
  AsyncMediaCodec(AMediaCodec* aMediaCodec, Callbacks* aCallbacks,
                  bool aIsHardwareAccelerated);

  static nsTArray<nsCString> FindMatchingCodecNames(AMediaFormat* aFormat,
                                                    bool aIsEncoder);

  static void OnAsyncInputAvailable(AMediaCodec* codec, void* userdata,
                                    int32_t index);
  static void OnAsyncOutputAvailable(AMediaCodec* codec, void* userdata,
                                     int32_t index,
                                     AMediaCodecBufferInfo* bufferInfo);
  static void OnAsyncFormatChanged(AMediaCodec* codec, void* userdata,
                                   AMediaFormat* format);
  static void OnAsyncError(AMediaCodec* codec, void* userdata,
                           media_status_t error, int32_t actionCode,
                           const char* detail);

  AMediaCodec* const mMediaCodec;
  const nsCOMPtr<nsISerialEventTarget> mThread;
  Callbacks* const mCallbacks;
  const bool mIsHardwareAccelerated;
  Atomic<uint64_t> mSession;
  Atomic<bool> mIsRunning;
};

DDLoggedTypeDeclNameAndBase(MediaCodecDecoder, MediaDataDecoder);

class MediaCodecDecoder : public MediaDataDecoder,
                          public DecoderDoctorLifeLogger<MediaCodecDecoder>,
                          public AsyncMediaCodec::Callbacks {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaCodecDecoder, final);

  // static already_AddRefed<MediaDataDecoder> CreateAudioDecoder(
  //     const CreateDecoderParams& aParams, const nsString& aDrmStubId,
  //     CDMProxy* aProxy);

  static already_AddRefed<MediaDataDecoder> CreateVideoDecoder(
      const CreateDecoderParams& aParams, const nsString& aDrmStubId,
      CDMProxy* aProxy);

  RefPtr<DecodePromise> Decode(MediaRawData* aSample) override;
  RefPtr<DecodePromise> Drain() override;
  RefPtr<FlushPromise> Flush() override;
  RefPtr<ShutdownPromise> Shutdown() override;
  nsCString GetDescriptionName() const override { return "android decoder"_ns; }

 protected:
  virtual ~MediaCodecDecoder();
  MediaCodecDecoder(MediaData::Type aType, const nsACString& aMimeType,
                    AMediaFormat* aFormat, const nsString& aDrmStubId);

  nsTArray<nsCString> FindMatchingCodecNames();

  // Methods only called on mThread.
  void ProcessInputs();
  void UpdateInputStatus();
  void UpdateOutputStatus(RefPtr<MediaData>&& aSample);
  void ReturnDecodedData();
  void DrainComplete();
  void Error(const MediaResult& aError);
  void AssertOnThread() const {
    // mThread may not be set if Init hasn't been called first.
    MOZ_ASSERT(!mThread || mThread->IsOnCurrentThread());
  }

  enum class State { DRAINED, DRAINABLE, DRAINING, SHUTDOWN };
  void SetState(State aState) {
    AssertOnThread();
    mState = aState;
  }
  State GetState() const {
    AssertOnThread();
    return mState;
  }

  // Whether the sample will be used.
  virtual bool IsUsefulData(const RefPtr<MediaData>& aSample) { return true; }

  MediaData::Type mType;

  nsAutoCString mMimeType;
  AMediaFormat* mFormat;

  RefPtr<AsyncMediaCodec> mMediaCodec;
  nsString mDrmStubId;

  nsCOMPtr<nsISerialEventTarget> mThread;

  // FIXME: make private instead of protected. needs OnInput callback to be
  // moved to this class
  nsRefPtrDeque<MediaRawData> mQueuedSamples;
  nsTArray<AsyncMediaCodec::Buffer> mAvailableInputBuffers;

 private:
  enum class PendingOp { INCREASE, DECREASE, CLEAR };
  void UpdatePendingInputStatus(PendingOp aOp);
  size_t HasPendingInputs() {
    AssertOnThread();
    return mQueuedSamples.GetSize() > 0;
  }

  // Returns true if we are in a state which requires a new decoder to be
  // created. In this case all errors will be reported as
  // NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER to avoid reporting errors as fatal when
  // they can be fixed with a new decoder.
  virtual bool NeedsNewDecoder() const { return false; }

  // The following members must only be accessed on mThread.
  MozPromiseHolder<DecodePromise> mDecodePromise;
  MozPromiseHolder<DecodePromise> mDrainPromise;
  DecodedData mDecodedData;
  State mState = State::DRAINED;
};

}  // namespace mozilla

#endif
