/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaCodecDecoder.h"

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaError.h>
#include <media/NdkMediaFormat.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AndroidBuild.h"
#include "AndroidDecoderModule.h"
#include "EMEDecoderModule.h"
#include "ErrorList.h"
#include "GLImages.h"
#include "MediaCodec.h"
#include "MediaData.h"
#include "MediaInfo.h"
#include "PerformanceRecorder.h"
#include "SimpleMap.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/java/GeckoSurfaceWrappers.h"
#include "mozilla/java/HardwareCodecCapabilityUtilsWrappers.h"
#include "mozilla/java/SurfaceAllocatorWrappers.h"
#include "mozilla/jni/Refs.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "nsString.h"
#include "nsThreadUtils.h"

#undef LOG
#define LOG(arg, ...)                                         \
  MOZ_LOG(sAndroidDecoderModuleLog, mozilla::LogLevel::Debug, \
          ("MediaCodecDecoder(%p)::%s: " arg, this, __func__, ##__VA_ARGS__))

using namespace mozilla;
using namespace mozilla::gl;
using media::TimeUnit;

namespace mozilla {

static const nsLiteralCString SW_DECODE_PREFIX = "OMX.google."_ns;

/* static */
RefPtr<AsyncMediaCodec> AsyncMediaCodec::Create(AMediaFormat* aFormat,
                                                Callbacks* aCallbacks,
                                                ANativeWindow* aNativeWindow,
                                                bool aIsEncoder) {
  const char* mimeType = nullptr;
  AMediaFormat_getString(aFormat, AMEDIAFORMAT_KEY_MIME, &mimeType);

  const auto codecInfos = FindMatchingCodecInfos(aFormat, aIsEncoder);
  for (const auto& info : codecInfos) {
    const nsCString name = info->GetName()->ToCString();
    AMediaCodec* codec = AMediaCodec_createCodecByName(name.get());
    if (codec == nullptr) {
      continue;
    }

    const bool isHardwareAccelerated =
        !StringBeginsWith(name, SW_DECODE_PREFIX);
    // FIXME: bug 1789846 get stride and height from codec
    RefPtr<AsyncMediaCodec> ret =
        new AsyncMediaCodec(codec, aCallbacks, isHardwareAccelerated);

    const AMediaCodecOnAsyncNotifyCallback callback = {
        .onAsyncInputAvailable = &OnAsyncInputAvailable,
        .onAsyncOutputAvailable = &OnAsyncOutputAvailable,
        .onAsyncFormatChanged = &OnAsyncFormatChanged,
        .onAsyncError = &OnAsyncError,
    };
    if (__builtin_available(android 28, *)) {
      const media_status_t res =
          AMediaCodec_setAsyncNotifyCallback(codec, callback, ret.get());
      MOZ_RELEASE_ASSERT(res == AMEDIA_OK);
    } else {
      MOZ_CRASH("SDK level 28 is required");
    }

    // FIXME: crypto

    ret->SetupAdaptivePlayback(info, aFormat);

    media_status_t res =
        AMediaCodec_configure(codec, aFormat, aNativeWindow, nullptr, 0);
    if (res != AMEDIA_OK) {
      continue;
    }

    return ret;
  }

  return nullptr;
}

AsyncMediaCodec::AsyncMediaCodec(AMediaCodec* aMediaCodec,
                                 Callbacks* aCallbacks,
                                 bool aIsHardwareAccelerated)
    : mMediaCodec(aMediaCodec),
      mThread(GetCurrentSerialEventTarget()),
      mCallbacks(aCallbacks),
      mIsHardwareAccelerated(aIsHardwareAccelerated),
      mSession(0),
      mIsRunning(false) {
}

AsyncMediaCodec::~AsyncMediaCodec() {
  // If we're still running here it means we could get a callback on the NDK
  // thread, which would be bad news.
  MOZ_RELEASE_ASSERT(!mIsRunning);
  AMediaCodec_delete(mMediaCodec);
}

bool AsyncMediaCodec::Start() {
  MOZ_RELEASE_ASSERT(mThread->IsOnCurrentThread());
  mIsRunning = true;
  const media_status_t res = AMediaCodec_start(mMediaCodec);
  if (res != AMEDIA_OK) {
    // FIXME: log
    return false;
  }
  return true;
}

bool AsyncMediaCodec::Flush() {
  MOZ_RELEASE_ASSERT(mThread->IsOnCurrentThread());
  mIsRunning = false;
  const media_status_t res = AMediaCodec_flush(mMediaCodec);
  if (res != AMEDIA_OK) {
    // FIXME: log
    return false;
  }
  // Increment mSession after AMediaCodec_flush so that any
  // OnAsync{Input,Output}Available callbacks that occur prior to flush
  // returning use the old value. No more callbacks can then occur until we call
  // Start, which can only happen from the current thread after the increment.
  mSession++;
  return true;
}

bool AsyncMediaCodec::Stop() {
  MOZ_RELEASE_ASSERT(mThread->IsOnCurrentThread());
  mIsRunning = false;
  // FIXME: do we need to increment mSession here too? Does stopping the codec
  // invalidate the buffers?
  const media_status_t res = AMediaCodec_stop(mMediaCodec);
  if (res != AMEDIA_OK) {
    // FIXME: log
    return false;
  }
  return true;
}

Span<uint8_t> AsyncMediaCodec::GetInputBuffer(Buffer aBuffer) {
  MOZ_RELEASE_ASSERT(mThread->IsOnCurrentThread());
  MOZ_RELEASE_ASSERT(aBuffer.mSession == mSession);
  size_t size;
  uint8_t* buffer =
      AMediaCodec_getInputBuffer(mMediaCodec, aBuffer.mIndex, &size);
  if (!buffer) {
    // FIXME: log
    return {};
  }
  return Span(buffer, size);
}

bool AsyncMediaCodec::QueueInputBuffer(Buffer aBuffer, off_t aOffset,
                                       size_t aSize, uint64_t aTime,
                                       uint32_t aFlags) {
  MOZ_RELEASE_ASSERT(mThread->IsOnCurrentThread());
  MOZ_RELEASE_ASSERT(aBuffer.mSession == mSession);
  const media_status_t res = AMediaCodec_queueInputBuffer(
      mMediaCodec, aBuffer.mIndex, aOffset, aSize, aTime, aFlags);
  if (res != AMEDIA_OK) {
    // FIXME: log
    return false;
  }
  return true;
}

bool AsyncMediaCodec::ReleaseOutputBuffer(Buffer aBuffer, bool aRender) {
  // This can potentially race with Flush. The codec may been flushed but
  // mSession has not yet incremented. This is fine, however, as in this case
  // AMediaCodec_releaseOutputBuffer will return an error as the index will be
  // invalid. There is no risk of accidentally releasing a valid buffer with the
  // same index, as mSession will be incremented before the codec is restarted.
  if (!mIsRunning || aBuffer.mSession != mSession) {
    return false;
  }
  const media_status_t res =
      AMediaCodec_releaseOutputBuffer(mMediaCodec, aBuffer.mIndex, aRender);
  if (res != AMEDIA_OK) {
    // FIXME: log
    return false;
  }
  return true;
}

// FIXME: Use existing java implementation instead? Then we can remove the
// additional generated bindings from MediaCodec-classes.txt
/* static */
nsTArray<java::sdk::MediaCodecInfo::LocalRef>
AsyncMediaCodec::FindMatchingCodecInfos(AMediaFormat* aFormat,
                                        bool aIsEncoder) {
  const char* mimeType = nullptr;
  AMediaFormat_getString(aFormat, AMEDIAFORMAT_KEY_MIME, &mimeType);

  int32_t width = 0;
  AMediaFormat_getInt32(aFormat, AMEDIAFORMAT_KEY_WIDTH, &width);
  int32_t height = 0;
  AMediaFormat_getInt32(aFormat, AMEDIAFORMAT_KEY_HEIGHT, &height);

  int numCodecs = 0;
  nsTArray<java::sdk::MediaCodecInfo::LocalRef> found;
  // FIXME: use nsresult and handle exception. we handle it in java code
  numCodecs = java::sdk::MediaCodecList::GetCodecCount();

  for (int i = 0; i < numCodecs; i++) {
    // FIXME: use nsresult and handle exception
    java::sdk::MediaCodecInfo::LocalRef info =
        java::sdk::MediaCodecList::GetCodecInfoAt(i);
    if (info->IsEncoder() != aIsEncoder) {
      continue;
    }

    mozilla::jni::ObjectArray::LocalRef types = info->GetSupportedTypes();
    for (size_t type_idx = 0; type_idx < types->Length(); type_idx++) {
      nsCString type =
          jni::String::LocalRef(types->GetElement(type_idx))->ToCString();
      if (!type.EqualsIgnoreCase(mimeType)) {
        continue;
      }
      if (aIsEncoder && width > 0 && height > 0) {
        java::sdk::MediaCodecInfo::CodecCapabilities::LocalRef codecCaps =
            info->GetCapabilitiesForType(mimeType);
        java::sdk::MediaCodecInfo::VideoCapabilities::LocalRef videoCaps =
            codecCaps->GetVideoCapabilities();

        if (videoCaps != nullptr &&
            !videoCaps->IsSizeSupported(width, height)) {
          continue;
        }
      }

      found.AppendElement(info);
    }
  }
  return found;
}

void AsyncMediaCodec::SetupAdaptivePlayback(
    java::sdk::MediaCodecInfo::Param aCodecInfo, AMediaFormat* aFormat) {
  const char* mimeType = nullptr;
  AMediaFormat_getString(aFormat, AMEDIAFORMAT_KEY_MIME, &mimeType);

  mIsAdaptivePlaybackSupported =
      java::HardwareCodecCapabilityUtils::CheckSupportsAdaptivePlayback(
          aCodecInfo, mimeType);

  // FIXME: in java impl we set max width and height for adaptive playback. do
  // we need to do the same here?
}

/* static */
void AsyncMediaCodec::OnAsyncInputAvailable(AMediaCodec* aCodec,
                                            void* aUserdata, int32_t aIndex) {
  const RefPtr<AsyncMediaCodec> self = static_cast<AsyncMediaCodec*>(aUserdata);
  uint64_t session = self->mSession;
  const nsresult rv = self->mThread->Dispatch(NS_NewRunnableFunction(
      "AsyncMediaCodec::OnAsyncInputAvailable",
      [cb = RefPtr{self->mCallbacks}, self, index = aIndex, session]() {
        if (session == self->mSession) {
          cb->OnAsyncInputAvailable(Buffer(index, session));
        }
      }));
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  Unused << rv;
}

/* static */
void AsyncMediaCodec::OnAsyncOutputAvailable(
    AMediaCodec* aCodec, void* aUserdata, int32_t aIndex,
    AMediaCodecBufferInfo* aBufferInfo) {
  const RefPtr<AsyncMediaCodec> self = static_cast<AsyncMediaCodec*>(aUserdata);
  uint64_t session = self->mSession;
  const nsresult rv = self->mThread->Dispatch(NS_NewRunnableFunction(
      "AsyncMediaCodec::OnAsyncOutputAvailable",
      [cb = RefPtr{self->mCallbacks}, self, index = aIndex, session,
       bufferInfo = *aBufferInfo]() {
        if (session == self->mSession) {
          cb->OnAsyncOutputAvailable(Buffer(index, session), bufferInfo);
        }
      }));
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  Unused << rv;
}

/* static */
void AsyncMediaCodec::OnAsyncFormatChanged(AMediaCodec* aCodec, void* aUserdata,
                                           AMediaFormat* aFormat) {
  AsyncMediaCodec* const self = static_cast<AsyncMediaCodec*>(aUserdata);
  const nsresult rv = self->mThread->Dispatch(NS_NewRunnableFunction(
      "AsyncMediaCodec::OnAsyncFormatChanged",
      // FIXME: do we need to copy format?
      [cb = RefPtr{self->mCallbacks}, format = aFormat]() {
        cb->OnAsyncFormatChanged(format);
      }));
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  Unused << rv;
}

/* static */
void AsyncMediaCodec::OnAsyncError(AMediaCodec* aCodec, void* aUserdata,
                                   media_status_t aError, int32_t aActionCode,
                                   const char* aDetail) {
  AsyncMediaCodec* const self = static_cast<AsyncMediaCodec*>(aUserdata);
  const nsresult rv = self->mThread->Dispatch(NS_NewRunnableFunction(
      "AsyncMediaCodec::OnAsyncError",
      [cb = RefPtr{self->mCallbacks}, error = aError, actionCode = aActionCode,
       detail = aDetail]() { cb->OnAsyncError(error, actionCode, detail); }));
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  Unused << rv;
}

// Hold a reference to the output buffer until we're ready to release it back to
// the MediaCodec (for rendering or not).
class RenderOrReleaseOutput2 {
 public:
  RenderOrReleaseOutput2(AsyncMediaCodec* aCodec,
                         AsyncMediaCodec::Buffer aBuffer)
      : mCodec(aCodec), mBuffer(aBuffer) {}

  virtual ~RenderOrReleaseOutput2() { ReleaseOutput(false); }

 protected:
  void ReleaseOutput(bool aToRender) {
    RefPtr<AsyncMediaCodec> codec(mCodec);
    if (codec) {
      codec->ReleaseOutputBuffer(mBuffer, aToRender);
    }
    mCodec = nullptr;
  }

 private:
  ThreadSafeWeakPtr<AsyncMediaCodec> mCodec;  // FIXME: refptr/weakptr?
  AsyncMediaCodec::Buffer mBuffer;
};

static bool areSmpte432ColorPrimariesBuggy2() {
  if (jni::GetAPIVersion() >= 34) {
    const auto socManufacturer =
        java::sdk::Build::SOC_MANUFACTURER()->ToString();
    if (socManufacturer.EqualsASCII("Google")) {
      return true;
    }
  }
  return false;
}

class MediaCodecVideoDecoder final : public MediaCodecDecoder {
 public:
  // Render the output to the surface when the frame is sent
  // to compositor, or release it if not presented.
  class CompositeListener
      : private RenderOrReleaseOutput2,
        public layers::SurfaceTextureImage::SetCurrentCallback {
   public:
    CompositeListener(AsyncMediaCodec* aCodec, AsyncMediaCodec::Buffer aBuffer)
        : RenderOrReleaseOutput2(aCodec, aBuffer) {}

    void operator()(void) override { ReleaseOutput(true); }
  };

  class InputInfo {
   public:
    InputInfo() = default;

    InputInfo(const int64_t aDurationUs, const gfx::IntSize& aImageSize,
              const gfx::IntSize& aDisplaySize)
        : mDurationUs(aDurationUs),
          mImageSize(aImageSize),
          mDisplaySize(aDisplaySize) {}

    int64_t mDurationUs = {};
    gfx::IntSize mImageSize = {};
    gfx::IntSize mDisplaySize = {};
  };

  MediaCodecVideoDecoder(const VideoInfo& aConfig, AMediaFormat* aFormat,
                         const nsString& aDrmStubId,
                         Maybe<TrackingId> aTrackingId)
      : MediaCodecDecoder(MediaData::Type::VIDEO_DATA, aConfig.mMimeType,
                          aFormat, aDrmStubId),
        mConfig(aConfig),
        mTrackingId(std::move(aTrackingId)) {}

  ~MediaCodecVideoDecoder() {
    if (mNativeWindow) {
      ANativeWindow_release(mNativeWindow);
    }
    if (mSurface) {
      java::SurfaceAllocator::DisposeSurface(mSurface);
    }
  }

  RefPtr<InitPromise> Init() override {
    mThread = GetCurrentSerialEventTarget();

    mSurface =
        java::GeckoSurface::LocalRef(java::SurfaceAllocator::AcquireSurface(
            mConfig.mImage.width, mConfig.mImage.height, false));
    if (!mSurface) {
      return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
    }

    mNativeWindow = ANativeWindow_fromSurface(jni::GetEnvForThread(),
                                              mSurface->GetSurface().Get());
    if (!mNativeWindow) {
      return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
    }

    mSurfaceHandle = mSurface->GetHandle();
    mMediaCodec = AsyncMediaCodec::Create(mFormat, this, mNativeWindow,
                                          /* aIsEncoder */ false);
    if (mMediaCodec == nullptr) {
      return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
    }
    mIsHardwareAccelerated = mMediaCodec->IsHardwareAccelerated();
    mIsCodecSupportAdaptivePlayback =
        mMediaCodec->IsAdaptivePlaybackSupported();

    if (!mMediaCodec->Start()) {
      return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
    }

    mMediaInfoFlag = MediaInfoFlag::None;
    mMediaInfoFlag |= mIsHardwareAccelerated ? MediaInfoFlag::HardwareDecoding
                                             : MediaInfoFlag::SoftwareDecoding;
    if (mMimeType.EqualsLiteral("video/mp4") ||
        mMimeType.EqualsLiteral("video/avc")) {
      mMediaInfoFlag |= MediaInfoFlag::VIDEO_H264;
    } else if (mMimeType.EqualsLiteral("video/vp8")) {
      mMediaInfoFlag |= MediaInfoFlag::VIDEO_VP8;
    } else if (mMimeType.EqualsLiteral("video/vp9")) {
      mMediaInfoFlag |= MediaInfoFlag::VIDEO_VP9;
    } else if (mMimeType.EqualsLiteral("video/av1")) {
      mMediaInfoFlag |= MediaInfoFlag::VIDEO_AV1;
    }
    return InitPromise::CreateAndResolve(TrackInfo::kVideoTrack, __func__);
  }

  RefPtr<MediaDataDecoder::FlushPromise> Flush() override {
    AssertOnThread();
    mInputInfos.Clear();
    mSeekTarget.reset();
    mLatestOutputTime.reset();
    mPerformanceRecorder.Record(std::numeric_limits<int64_t>::max());
    return MediaCodecDecoder::Flush();
  }

  nsCString GetCodecName() const override {
    if (mMediaInfoFlag & MediaInfoFlag::VIDEO_H264) {
      return "h264"_ns;
    }
    if (mMediaInfoFlag & MediaInfoFlag::VIDEO_VP8) {
      return "vp8"_ns;
    }
    if (mMediaInfoFlag & MediaInfoFlag::VIDEO_VP9) {
      return "vp9"_ns;
    }
    if (mMediaInfoFlag & MediaInfoFlag::VIDEO_AV1) {
      return "av1"_ns;
    }
    return "unknown"_ns;
  }

  RefPtr<MediaDataDecoder::DecodePromise> Decode(
      MediaRawData* aSample) override {
    AssertOnThread();

    if (NeedsNewDecoder()) {
      return DecodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER,
                                            __func__);
    }

    const VideoInfo* config =
        aSample->mTrackInfo ? aSample->mTrackInfo->GetAsVideoInfo() : &mConfig;
    MOZ_ASSERT(config);

    mTrackingId.apply([&](const auto& aId) {
      MediaInfoFlag flag = mMediaInfoFlag;
      flag |= (aSample->mKeyframe ? MediaInfoFlag::KeyFrame
                                  : MediaInfoFlag::NonKeyFrame);
      mPerformanceRecorder.Start(aSample->mTime.ToMicroseconds(),
                                 "AndroidDecoder"_ns, aId, flag);
    });

    InputInfo info(aSample->mDuration.ToMicroseconds(), config->mImage,
                   config->mDisplay);
    mInputInfos.Insert(aSample->mTime.ToMicroseconds(), info);
    return MediaCodecDecoder::Decode(aSample);
  }

  bool SupportDecoderRecycling() const override {
    return mIsCodecSupportAdaptivePlayback;
  }

  void SetSeekThreshold(const TimeUnit& aTime) override {
    auto setter = [self = RefPtr{this}, aTime] {
      if (aTime.IsValid()) {
        self->mSeekTarget = Some(aTime);
      } else {
        self->mSeekTarget.reset();
      }
    };
    if (mThread->IsOnCurrentThread()) {
      setter();
    } else {
      nsCOMPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
          "MediaCodecVideoDecoder::SetSeekThreshold", std::move(setter));
      nsresult rv = mThread->Dispatch(runnable.forget());
      MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
      Unused << rv;
    }
  }

  bool IsUsefulData(const RefPtr<MediaData>& aSample) override {
    AssertOnThread();

    if (mLatestOutputTime && aSample->mTime < mLatestOutputTime.value()) {
      return false;
    }

    const TimeUnit endTime = aSample->GetEndTime();
    if (mSeekTarget && endTime <= mSeekTarget.value()) {
      return false;
    }

    mSeekTarget.reset();
    mLatestOutputTime = Some(endTime);
    return true;
  }

  bool IsHardwareAccelerated(nsACString& aFailureReason) const override {
    return mIsHardwareAccelerated;
  }

  ConversionRequired NeedsConversion() const override {
    return ConversionRequired::kNeedAnnexB;
  }

 private:
  void OnAsyncInputAvailable(AsyncMediaCodec::Buffer aBuffer) override {
    AssertOnThread();
    if (GetState() == State::SHUTDOWN) {
      return;
    }

    mAvailableInputBuffers.AppendElement(aBuffer);
    ProcessInputs();
  }

  void OnAsyncOutputAvailable(AsyncMediaCodec::Buffer aBuffer,
                              AMediaCodecBufferInfo aBufferInfo) override {
    AssertOnThread();
    if (GetState() == State::SHUTDOWN) {
      return;
    }

    UniquePtr<layers::SurfaceTextureImage::SetCurrentCallback> releaseSample(
        new CompositeListener(mMediaCodec, aBuffer));

    // If our output surface has been released (due to the GPU process crashing)
    // then request a new decoder, which will in turn allocate a new
    // Surface. This is usually be handled by the Error() callback, but on some
    // devices (or at least on the emulator) the java decoder does not raise an
    // error when the Surface is released. So we raise this error here as well.
    if (NeedsNewDecoder()) {
      Error(MediaResult(NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER,
                        RESULT_DETAIL("VideoCallBack::HandleOutput")));
      return;
    }

    Maybe<InputInfo> inputInfo =
        mInputInfos.Take(aBufferInfo.presentationTimeUs);
    bool isEOS = !!(aBufferInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
    if (!inputInfo && !isEOS) {
      // Ignore output with no corresponding input.
      return;
    }

    if (inputInfo &&
        (aBufferInfo.size > 0 || aBufferInfo.presentationTimeUs >= 0)) {
      // On certain devices SMPTE 432 color primaries are rendered incorrectly,
      // so we force BT709 to be used instead.
      // Color space 10 comes from the video in bug 1866020 and corresponds to
      // libstagefright's kColorStandardDCI_P3.
      // 65800 comes from the video in bug 1879720 and is vendor-specific.
      static bool isSmpte432Buggy = areSmpte432ColorPrimariesBuggy2();
      bool forceBT709ColorSpace =
          isSmpte432Buggy &&
          (mColorSpace == Some(10) || mColorSpace == Some(65800));

      RefPtr<layers::Image> img = new layers::SurfaceTextureImage(
          mSurfaceHandle, inputInfo->mImageSize, false /* NOT continuous */,
          gl::OriginPos::BottomLeft, mConfig.HasAlpha(), forceBT709ColorSpace,
          /* aTransformOverride */ Nothing());
      img->AsSurfaceTextureImage()->RegisterSetCurrentCallback(
          std::move(releaseSample));

      RefPtr<VideoData> v = VideoData::CreateFromImage(
          inputInfo->mDisplaySize, aBufferInfo.offset,
          TimeUnit::FromMicroseconds(aBufferInfo.presentationTimeUs),
          TimeUnit::FromMicroseconds(inputInfo->mDurationUs), img.forget(),
          !!(aBufferInfo.flags & AMEDIACODEC_BUFFER_FLAG_KEY_FRAME),
          TimeUnit::FromMicroseconds(aBufferInfo.presentationTimeUs));

      mPerformanceRecorder.Record(
          aBufferInfo.presentationTimeUs, [&](DecodeStage& aStage) {
            using Cap = java::sdk::MediaCodecInfo::CodecCapabilities;
            using Fmt = java::sdk::MediaFormat;
            mColorFormat.apply([&](int32_t aFormat) {
              switch (aFormat) {
                case Cap::COLOR_Format32bitABGR8888:
                case Cap::COLOR_Format32bitARGB8888:
                case Cap::COLOR_Format32bitBGRA8888:
                case Cap::COLOR_FormatRGBAFlexible:
                  aStage.SetImageFormat(DecodeStage::RGBA32);
                  break;
                case Cap::COLOR_Format24bitBGR888:
                case Cap::COLOR_Format24bitRGB888:
                case Cap::COLOR_FormatRGBFlexible:
                  aStage.SetImageFormat(DecodeStage::RGB24);
                  break;
                case Cap::COLOR_FormatYUV411Planar:
                case Cap::COLOR_FormatYUV411PackedPlanar:
                case Cap::COLOR_FormatYUV420Planar:
                case Cap::COLOR_FormatYUV420PackedPlanar:
                case Cap::COLOR_FormatYUV420Flexible:
                  aStage.SetImageFormat(DecodeStage::YUV420P);
                  break;
                case Cap::COLOR_FormatYUV420SemiPlanar:
                case Cap::COLOR_FormatYUV420PackedSemiPlanar:
                case Cap::COLOR_QCOM_FormatYUV420SemiPlanar:
                case Cap::COLOR_TI_FormatYUV420PackedSemiPlanar:
                  aStage.SetImageFormat(DecodeStage::NV12);
                  break;
                case Cap::COLOR_FormatYCbYCr:
                case Cap::COLOR_FormatYCrYCb:
                case Cap::COLOR_FormatCbYCrY:
                case Cap::COLOR_FormatCrYCbY:
                case Cap::COLOR_FormatYUV422Planar:
                case Cap::COLOR_FormatYUV422PackedPlanar:
                case Cap::COLOR_FormatYUV422Flexible:
                  aStage.SetImageFormat(DecodeStage::YUV422P);
                  break;
                case Cap::COLOR_FormatYUV444Interleaved:
                case Cap::COLOR_FormatYUV444Flexible:
                  aStage.SetImageFormat(DecodeStage::YUV444P);
                  break;
                case Cap::COLOR_FormatSurface:
                  aStage.SetImageFormat(DecodeStage::ANDROID_SURFACE);
                  break;
                /* Added in API level 33
                case Cap::COLOR_FormatYUVP010:
                  aStage.SetImageFormat(DecodeStage::P010);
                  break;
                */
                default:
                  NS_WARNING(
                      nsPrintfCString("Unhandled color format %d (0x%08x)",
                                      aFormat, aFormat)
                          .get());
              }
            });
            mColorRange.apply([&](int32_t aRange) {
              switch (aRange) {
                case Fmt::COLOR_RANGE_FULL:
                  aStage.SetColorRange(gfx::ColorRange::FULL);
                  break;
                case Fmt::COLOR_RANGE_LIMITED:
                  aStage.SetColorRange(gfx::ColorRange::LIMITED);
                  break;
                default:
                  NS_WARNING(
                      nsPrintfCString("Unhandled color range %d (0x%08x)",
                                      aRange, aRange)
                          .get());
              }
            });
            mColorSpace.apply([&](int32_t aSpace) {
              switch (aSpace) {
                case Fmt::COLOR_STANDARD_BT2020:
                  aStage.SetYUVColorSpace(gfx::YUVColorSpace::BT2020);
                  break;
                case Fmt::COLOR_STANDARD_BT601_NTSC:
                case Fmt::COLOR_STANDARD_BT601_PAL:
                  aStage.SetYUVColorSpace(gfx::YUVColorSpace::BT601);
                  break;
                case Fmt::COLOR_STANDARD_BT709:
                  aStage.SetYUVColorSpace(gfx::YUVColorSpace::BT709);
                  break;
                default:
                  NS_WARNING(
                      nsPrintfCString("Unhandled color space %d (0x%08x)",
                                      aSpace, aSpace)
                          .get());
              }
            });
            aStage.SetResolution(v->mImage->GetSize().Width(),
                                 v->mImage->GetSize().Height());
            aStage.SetStartTimeAndEndTime(v->mTime.ToMicroseconds(),
                                          v->GetEndTime().ToMicroseconds());
          });

      UpdateOutputStatus(std::move(v));
    }

    if (isEOS) {
      DrainComplete();
    }
  }

  void OnAsyncFormatChanged(AMediaFormat* aFormat) override {
    AssertOnThread();
    if (GetState() == State::SHUTDOWN) {
      return;
    }

    int32_t colorFormat = 0;
    AMediaFormat_getInt32(aFormat, AMEDIAFORMAT_KEY_COLOR_FORMAT, &colorFormat);
    if (colorFormat == 0) {
      Error(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                        RESULT_DETAIL("Invalid color format:%d", colorFormat)));
      return;
    }

    Maybe<int32_t> colorRange;
    {
      int32_t range = 0;
      if (AMediaFormat_getInt32(aFormat, AMEDIAFORMAT_KEY_COLOR_RANGE,
                                &range)) {
        colorRange.emplace(range);
      }
    }

    Maybe<int32_t> colorSpace;
    {
      int32_t space = 0;
      if (AMediaFormat_getInt32(aFormat, AMEDIAFORMAT_KEY_COLOR_STANDARD,
                                &space)) {
        colorSpace.emplace(space);
      }
    }

    mColorFormat = Some(colorFormat);
    mColorRange = colorRange;
    mColorSpace = colorSpace;
  }

  void OnAsyncError(media_status_t aError, int32_t aActionCode,
                    const char* aDetail) override {
    AssertOnThread();
    if (GetState() == State::SHUTDOWN) {
      return;
    }

    LOG("Error %d reported from media codec: %s", aError, aDetail);
    // FIXME: check if recoverable or transient and perhaps return a different
    // error
    Error(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__));
  }

  bool NeedsNewDecoder() const override {
    return !mSurface || mSurface->IsReleased();
  }

  const VideoInfo mConfig;
  java::GeckoSurface::GlobalRef mSurface;
  ANativeWindow* mNativeWindow = nullptr;
  AndroidSurfaceTextureHandle mSurfaceHandle{};
  // Only accessed on reader's task queue.
  bool mIsCodecSupportAdaptivePlayback = false;
  // Can be accessed on any thread, but only written on during init.
  bool mIsHardwareAccelerated = false;
  // Accessed on mThread and reader's thread. SimpleMap however is
  // thread-safe, so it's okay to do so.
  SimpleMap<int64_t, InputInfo, ThreadSafePolicy> mInputInfos;
  // Only accessed on mThread.
  Maybe<TimeUnit> mSeekTarget;
  Maybe<TimeUnit> mLatestOutputTime;
  Maybe<int32_t> mColorFormat;
  Maybe<int32_t> mColorRange;
  Maybe<int32_t> mColorSpace;
  // Only accessed on mThread.
  // Tracking id for the performance recorder.
  const Maybe<TrackingId> mTrackingId;
  // Can be accessed on any thread, but only written during init.
  // Pre-filled decode info used by the performance recorder.
  MediaInfoFlag mMediaInfoFlag = {};
  // Only accessed on mThread.
  // Records decode performance to the profiler.
  PerformanceRecorderMulti<DecodeStage> mPerformanceRecorder;
};

already_AddRefed<MediaDataDecoder> MediaCodecDecoder::CreateVideoDecoder(
    const CreateDecoderParams& aParams, const nsString& aDrmStubId,
    CDMProxy* aProxy) {
  const VideoInfo& config = aParams.VideoConfig();
  AMediaFormat* format = AMediaFormat_new();
  AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME,
                         TranslateMimeType(config.mMimeType).get());
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, config.mImage.width);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, config.mImage.height);

  RefPtr<MediaDataDecoder> decoder = new MediaCodecVideoDecoder(
      config, format, aDrmStubId, aParams.mTrackingId);
  if (aProxy) {
    decoder = new EMEMediaDataDecoderProxy(aParams, decoder.forget(), aProxy);
  }
  return decoder.forget();
}

MediaCodecDecoder::MediaCodecDecoder(MediaData::Type aType,
                                     const nsACString& aMimeType,
                                     AMediaFormat* aFormat,
                                     const nsString& aDrmStubId)
    : mType(aType),
      mMimeType(aMimeType),
      mFormat(aFormat),
      mDrmStubId(aDrmStubId) {}

MediaCodecDecoder::~MediaCodecDecoder() { AMediaFormat_delete(mFormat); }

RefPtr<MediaDataDecoder::FlushPromise> MediaCodecDecoder::Flush() {
  AssertOnThread();
  MOZ_ASSERT(GetState() != State::SHUTDOWN);

  mDecodedData = DecodedData();
  mQueuedSamples.Erase();
  mAvailableInputBuffers.Clear();
  mDecodePromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  mDrainPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  SetState(State::DRAINED);
  if (!mMediaCodec->Flush() || !mMediaCodec->Start()) {
    return FlushPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                         __func__);
  }
  return FlushPromise::CreateAndResolve(true, __func__);
}

RefPtr<MediaDataDecoder::DecodePromise> MediaCodecDecoder::Drain() {
  AssertOnThread();
  if (GetState() == State::SHUTDOWN) {
    return DecodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED,
                                          __func__);
  }
  RefPtr<DecodePromise> p = mDrainPromise.Ensure(__func__);
  if (GetState() == State::DRAINED) {
    // There's no operation to perform other than returning any already
    // decoded data.
    ReturnDecodedData();
    return p;
  }

  if (GetState() == State::DRAINING) {
    // Draining operation already pending, let it complete its course.
    return p;
  }

  SetState(State::DRAINING);

  // Add an EOS sample to the queue and process it
  RefPtr<MediaRawData> eos = new MediaRawData();
  eos->mEOS = true;
  mQueuedSamples.Push(eos);
  mThread->Dispatch(NewRunnableMethod("MediaCodecDecoder::ProcessInputs", this,
                                      &MediaCodecDecoder::ProcessInputs));

  return p;
}

RefPtr<ShutdownPromise> MediaCodecDecoder::Shutdown() {
  LOG("Shutdown");
  AssertOnThread();
  SetState(State::SHUTDOWN);

  mMediaCodec->Stop();
  mMediaCodec = nullptr;

  return ShutdownPromise::CreateAndResolve(true, __func__);
}

// using CryptoInfoResult =
//     Result<java::sdk::MediaCodec::CryptoInfo::LocalRef, nsresult>;

// static CryptoInfoResult GetCryptoInfoFromSample2(const MediaRawData* aSample)
// {
//   const auto& cryptoObj = aSample->mCrypto;
//   java::sdk::MediaCodec::CryptoInfo::LocalRef cryptoInfo;

//   if (!cryptoObj.IsEncrypted()) {
//     return CryptoInfoResult(cryptoInfo);
//   }

//   static bool supportsCBCS = java::CodecProxy::SupportsCBCS();
//   if ((cryptoObj.mCryptoScheme == CryptoScheme::Cbcs ||
//        cryptoObj.mCryptoScheme == CryptoScheme::Cbcs_1_9) &&
//       !supportsCBCS) {
//     return CryptoInfoResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR);
//   }

//   nsresult rv = java::sdk::MediaCodec::CryptoInfo::New(&cryptoInfo);
//   NS_ENSURE_SUCCESS(rv, CryptoInfoResult(rv));

//   uint32_t numSubSamples = std::min<uint32_t>(
//       cryptoObj.mPlainSizes.Length(), cryptoObj.mEncryptedSizes.Length());

//   uint32_t totalSubSamplesSize = 0;
//   for (const auto& size : cryptoObj.mPlainSizes) {
//     totalSubSamplesSize += size;
//   }
//   for (const auto& size : cryptoObj.mEncryptedSizes) {
//     totalSubSamplesSize += size;
//   }

//   // Deep copy the plain sizes so we can modify them.
//   nsTArray<uint32_t> plainSizes = cryptoObj.mPlainSizes.Clone();
//   uint32_t codecSpecificDataSize = aSample->Size() - totalSubSamplesSize;
//   // Size of codec specific data("CSD") for Android java::sdk::MediaCodec
//   usage
//   // should be included in the 1st plain size if it exists.
//   if (codecSpecificDataSize > 0 && !plainSizes.IsEmpty()) {
//     // This shouldn't overflow as the the plain size should be UINT16_MAX at
//     // most, and the CSD should never be that large. Checked int acts like a
//     // diagnostic assert here to help catch if we ever have insane inputs.
//     CheckedUint32 newLeadingPlainSize{plainSizes[0]};
//     newLeadingPlainSize += codecSpecificDataSize;
//     plainSizes[0] = newLeadingPlainSize.value();
//   }

//   static const int kExpectedIVLength = 16;
//   nsTArray<uint8_t> tempIV(kExpectedIVLength);
//   jint mode;
//   switch (cryptoObj.mCryptoScheme) {
//     case CryptoScheme::None:
//       mode = java::sdk::MediaCodec::CRYPTO_MODE_UNENCRYPTED;
//       MOZ_ASSERT(cryptoObj.mIV.Length() <= kExpectedIVLength);
//       tempIV.AppendElements(cryptoObj.mIV);
//       break;
//     case CryptoScheme::Cenc:
//       mode = java::sdk::MediaCodec::CRYPTO_MODE_AES_CTR;
//       MOZ_ASSERT(cryptoObj.mIV.Length() <= kExpectedIVLength);
//       tempIV.AppendElements(cryptoObj.mIV);
//       break;
//     case CryptoScheme::Cbcs:
//     case CryptoScheme::Cbcs_1_9:
//       mode = java::sdk::MediaCodec::CRYPTO_MODE_AES_CBC;
//       MOZ_ASSERT(cryptoObj.mConstantIV.Length() <= kExpectedIVLength);
//       tempIV.AppendElements(cryptoObj.mConstantIV);
//       break;
//   }
//   auto tempIVLength = tempIV.Length();
//   for (size_t i = tempIVLength; i < kExpectedIVLength; i++) {
//     // Padding with 0
//     tempIV.AppendElement(0);
//   }

//   MOZ_ASSERT(numSubSamples <= INT32_MAX);
//   cryptoInfo->Set(static_cast<int32_t>(numSubSamples),
//                   mozilla::jni::IntArray::From(plainSizes),
//                   mozilla::jni::IntArray::From(cryptoObj.mEncryptedSizes),
//                   mozilla::jni::ByteArray::From(cryptoObj.mKeyId),
//                   mozilla::jni::ByteArray::From(tempIV), mode);
//   if (mode == java::sdk::MediaCodec::CRYPTO_MODE_AES_CBC) {
//     java::CodecProxy::SetCryptoPatternIfNeeded(
//         cryptoInfo, cryptoObj.mCryptByteBlock, cryptoObj.mSkipByteBlock);
//   }

//   return CryptoInfoResult(cryptoInfo);
// }

RefPtr<MediaDataDecoder::DecodePromise> MediaCodecDecoder::Decode(
    MediaRawData* aSample) {
  AssertOnThread();
  MOZ_ASSERT(GetState() != State::SHUTDOWN);
  MOZ_ASSERT(aSample != nullptr);

  SetState(State::DRAINABLE);
  MOZ_ASSERT(aSample->Size() <= INT32_MAX);

  RefPtr<DecodePromise> p = mDecodePromise.Ensure(__func__);

  mQueuedSamples.Push(aSample);
  mThread->Dispatch(NewRunnableMethod("MediaCodecDecoder::ProcessInputs", this,
                                      &MediaCodecDecoder::ProcessInputs));

  // FIXME: handle crypto

  // CryptoInfoResult crypto = GetCryptoInfoFromSample2(aSample);
  // if (crypto.isErr()) {
  //   return DecodePromise::CreateAndReject(
  //       MediaResult(crypto.unwrapErr(), __func__), __func__);
  // }
  // int64_t session =
  //     mJavaDecoder->Input(bytes, mInputBufferInfo, crypto.unwrap());
  // if (session == java::CodecProxy::INVALID_SESSION) {
  //   return DecodePromise::CreateAndReject(
  //       MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__), __func__);
  // }
  return p;
}

void MediaCodecDecoder::UpdateInputStatus() {
  AssertOnThread();
  if (GetState() == State::SHUTDOWN) {
    return;
  }

  if (!HasPendingInputs() ||  // Input has been processed, request the next one.
      !mDecodedData.IsEmpty()) {  // Previous output arrived before Decode().
    ReturnDecodedData();
  }
}

void MediaCodecDecoder::UpdateOutputStatus(RefPtr<MediaData>&& aSample) {
  AssertOnThread();
  if (GetState() == State::SHUTDOWN) {
    LOG("Update output status, but decoder has been shut down, dropping the "
        "decoded results");
    return;
  }
  if (IsUsefulData(aSample)) {
    mDecodedData.AppendElement(std::move(aSample));
  } else {
    LOG("Decoded data, but not considered useful");
  }
  ReturnDecodedData();
}

void MediaCodecDecoder::ReturnDecodedData() {
  AssertOnThread();
  MOZ_ASSERT(GetState() != State::SHUTDOWN);

  // We only want to clear mDecodedData when we have resolved the promises.
  if (!mDecodePromise.IsEmpty()) {
    mDecodePromise.Resolve(std::move(mDecodedData), __func__);
    mDecodedData = DecodedData();
  } else if (!mDrainPromise.IsEmpty() &&
             (!mDecodedData.IsEmpty() || GetState() == State::DRAINED)) {
    mDrainPromise.Resolve(std::move(mDecodedData), __func__);
    mDecodedData = DecodedData();
  }
}

void MediaCodecDecoder::DrainComplete() {
  AssertOnThread();
  if (GetState() == State::SHUTDOWN) {
    return;
  }
  SetState(State::DRAINED);
  ReturnDecodedData();
}

void MediaCodecDecoder::Error(const MediaResult& aError) {
  AssertOnThread();
  if (GetState() == State::SHUTDOWN) {
    return;
  }

  // If we know we need a new decoder (eg because MediaCodecVideoDecoder's
  // mSurface has been released due to a GPU process crash) then override the
  // error to request a new decoder.
  const MediaResult& error =
      NeedsNewDecoder()
          ? MediaResult(NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER, __func__)
          : aError;

  mDecodePromise.RejectIfExists(error, __func__);
  mDrainPromise.RejectIfExists(error, __func__);
}

void MediaCodecDecoder::ProcessInputs() {
  AssertOnThread();
  // FIXME: handle crypto
  while (mQueuedSamples.GetSize() && !mAvailableInputBuffers.IsEmpty()) {
    const AsyncMediaCodec::Buffer bufferIndex =
        mAvailableInputBuffers.PopLastElement();
    const RefPtr<MediaRawData> sample = mQueuedSamples.PopFront();
    const uint32_t flags =
        sample->mEOS ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0;

    if (sample->Size()) {
      Span<uint8_t> buffer = mMediaCodec->GetInputBuffer(bufferIndex);
      if (buffer.empty()) {
        LOG("Failed to get input buffer from media codec");
        Error(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__));
        return;
      }
      if (buffer.size() < sample->Size()) {
        LOG("Sample too large for input buffer");
        Error(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__));
        return;
      }
      Span<const uint8_t> sampleSpan(sample->Data(), sample->Size());
      std::copy(sampleSpan.cbegin(), sampleSpan.cend(), buffer.begin());
    }
    if (!mMediaCodec->QueueInputBuffer(bufferIndex, 0, sample->Size(),
                                       sample->mTime.ToMicroseconds(), flags)) {
      LOG("Failed to queue input buffer to media codec");
      Error(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__));
      return;
    }
  }
  UpdateInputStatus();
}

}  // namespace mozilla
#undef LOG
