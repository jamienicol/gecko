/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.gfx;

import android.annotation.SuppressLint;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Choreographer;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.mozglue.JNIObject;

/** This class receives HW vsync events through a {@link Choreographer}. */
@WrapForJNI
/* package */ final class AndroidVsync extends JNIObject {
  @WrapForJNI
  @Override // JNIObject
  protected native void disposeNative();

  private static final String LOGTAG = "AndroidVsync";

  private final Choreographer.FrameCallback mFrameCallback;
  private final Choreographer.VsyncCallback mVsyncCallback;
  private boolean mPrintedTimelines = false;

  /* package */ Choreographer mChoreographer;
  private volatile boolean mObservingVsync;

  public AndroidVsync() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      mVsyncCallback =
          frameData -> {
            Choreographer.FrameTimeline chosenTimeline = frameData.getPreferredFrameTimeline();
            for (Choreographer.FrameTimeline timeline : frameData.getFrameTimelines()) {
              // On 120Hz devices the preferred timeline typically has a deadline of 2 frames, ie
              // 16ms. On 90Hz devices, however, it is typically 1 frame, ie 11ms. Realistically we
              // aren't going to submit a frame in less than 16ms, so don't choose a faster timeline
              // than that.
              if (timeline.getDeadlineNanos() - frameData.getFrameTimeNanos() >= 16000000) {
                chosenTimeline = timeline;
                break;
              }
            }

            if (!mPrintedTimelines) {
              for (Choreographer.FrameTimeline timeline : frameData.getFrameTimelines()) {
                Log.w(LOGTAG, String.format("jamiedbg Possible timeline: deadline in %dns", timeline.getDeadlineNanos() - frameData.getFrameTimeNanos()));
              }
              Log.w(LOGTAG, String.format("jamiedbg Preferred timeline: deadline in %dns", frameData.getPreferredFrameTimeline().getDeadlineNanos() - frameData.getFrameTimeNanos()));
              Log.w(LOGTAG, String.format("jamiedbg Chosen timeline: deadline in %dns", chosenTimeline.getDeadlineNanos() - frameData.getFrameTimeNanos()));
              mPrintedTimelines = true;
            }

            if (mObservingVsync) {
              postCallback();
              nativeNotifyVsync(
                  frameData.getFrameTimeNanos(),
                  chosenTimeline.getVsyncId(),
                  chosenTimeline.getDeadlineNanos(),
                  chosenTimeline.getExpectedPresentationTimeNanos());
            }
          };
      mFrameCallback = null;
    } else {
      mFrameCallback =
          frameTimeNanos -> {
            if (mObservingVsync) {
              postCallback();
              nativeNotifyVsync(frameTimeNanos, 0, 0, 0);
            }
          };
      mVsyncCallback = null;
    }

    final Handler mainHandler = new Handler(Looper.getMainLooper());
    mainHandler.post(
        new Runnable() {
          @Override
          public void run() {
            mChoreographer = Choreographer.getInstance();
            if (mObservingVsync) {
              postCallback();
            }
          }
        });
  }

  @SuppressLint("NewApi")
  private void postCallback() {
    if (mVsyncCallback != null) {
      mChoreographer.postVsyncCallback(mVsyncCallback);
    } else {
      mChoreographer.postFrameCallback(mFrameCallback);
    }
  }

  @SuppressLint("NewApi")
  private void removeCallback() {
    if (mVsyncCallback != null) {
      mChoreographer.removeVsyncCallback(mVsyncCallback);
    } else {
      mChoreographer.removeFrameCallback(mFrameCallback);
    }
  }

  @WrapForJNI(stubName = "NotifyVsync")
  private native void nativeNotifyVsync(
      final long frameTimeNanos,
      final long vsyncId,
      final long deadlineNanos,
      final long presentTimeNanos);

  /**
   * Start/stop observing Vsync event.
   *
   * @param enable true to start observing; false to stop.
   * @return true if observing and false if not.
   */
  @WrapForJNI
  public synchronized boolean observeVsync(final boolean enable) {
    if (mObservingVsync != enable) {
      mObservingVsync = enable;

      if (mChoreographer != null) {
        if (enable) {
          postCallback();
        } else {
          removeCallback();
        }
      }
    }
    return mObservingVsync;
  }
}
