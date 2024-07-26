/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.gfx;

import android.hardware.HardwareBuffer;
import android.os.Build;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;
import android.util.LongSparseArray;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.process.GeckoProcessManager;
import org.mozilla.gecko.process.GeckoServiceChildProcess;

/* package */ final class SurfaceAllocator {
  private static final String LOGTAG = "SurfaceAllocator";

  private static ISurfaceAllocator sAllocator;

  // Keep a reference to all allocated Surfaces, so that we can release them if we lose the
  // connection to the allocator service.
  private static final LongSparseArray<GeckoSurface> sSurfaces =
      new LongSparseArray<GeckoSurface>();

  private static synchronized void ensureConnection() {
    if (sAllocator != null) {
      return;
    }

    try {
      if (GeckoAppShell.isParentProcess()) {
        sAllocator = GeckoProcessManager.getInstance().getSurfaceAllocator();
      } else {
        sAllocator = GeckoServiceChildProcess.getSurfaceAllocator();
      }

      if (sAllocator == null) {
        Log.w(LOGTAG, "Failed to connect to RemoteSurfaceAllocator");
        return;
      }
      sAllocator
          .asBinder()
          .linkToDeath(
              new IBinder.DeathRecipient() {
                @Override
                public void binderDied() {
                  Log.w(LOGTAG, "RemoteSurfaceAllocator died");
                  synchronized (SurfaceAllocator.class) {
                    // Our connection to the remote allocator has died, so all our surfaces are
                    // invalid.  Release them all now. When their owners attempt to render in to
                    // them they can detect they have been released and allocate new ones instead.
                    for (int i = 0; i < sSurfaces.size(); i++) {
                      sSurfaces.valueAt(i).release();
                    }
                    sSurfaces.clear();
                    sAllocator = null;
                  }
                }
              },
              0);
    } catch (final RemoteException e) {
      Log.w(LOGTAG, "Failed to connect to RemoteSurfaceAllocator", e);
      sAllocator = null;
    }
  }

  // Acquires a Surface attached to a SurfaceTexture consumer
  @WrapForJNI
  public static synchronized GeckoSurface acquireSurfaceTexture(
      final int width, final int height, final boolean singleBufferMode) {
    try {
      ensureConnection();

      if (sAllocator == null) {
        Log.w(LOGTAG, "Failed to acquire GeckoSurface: not connected");
        return null;
      }

      final GeckoSurface surface = sAllocator.acquireSurfaceTexture(width, height, singleBufferMode);
      if (surface == null) {
        Log.w(LOGTAG, "Failed to acquire GeckoSurface: RemoteSurfaceAllocator returned null");
        return null;
      }
      sSurfaces.put(surface.getHandle(), surface);

      if (!surface.inProcess()) {
        final SyncConfig config = surface.initSyncSurface(width, height);
        if (config != null) {
          sAllocator.configureSync(config);
        }
      }
      return surface;
    } catch (final RemoteException e) {
      Log.w(LOGTAG, "Failed to acquire GeckoSurface", e);
      return null;
    }
  }

  // Acquires a Surface attached to an ImageReader consumer
  @WrapForJNI
  public static synchronized GeckoSurface acquireImageReader(
          final int width, final int height, final int format, final int maxImages, final long usage) {
    try {
      // We require SDK Level 26 (Android 8.0) for AImage_getHardwareBuffer() without which we
      // cannot render the Images acquired from the ImageReader, and ANativeWindow_toSurface()
      // without which we cannot obtain a Java Surface from the ImageReader to return from this
      // function.
      if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
        return null;
      }

      ensureConnection();

      if (sAllocator == null) {
        Log.w(LOGTAG, "Failed to acquire ImageReader: not connected");
        return null;
      }

      final GeckoSurface surface =
              sAllocator.acquireImageReader(width, height, format, maxImages, usage);
      if (surface == null) {
        Log.w(LOGTAG, "Failed to acquire ImageReader: RemoteSurfaceAllocator returned null");
        return null;
      }
      sSurfaces.put(surface.getHandle(), surface);

      return surface;
    } catch (final RemoteException e) {
      Log.w(LOGTAG, "Failed to acquire ImageReader", e);
      return null;
    }
  }

  @WrapForJNI
  public static synchronized void disposeSurface(final GeckoSurface surface) {
    // If the surface has already been released (probably due to losing connection to the remote
    // allocator) then there is nothing to do here.
    if (surface.isReleased()) {
      return;
    }

    sSurfaces.remove(surface.getHandle());

    // Release our Surface
    surface.release();

    if (sAllocator == null) {
      return;
    }

    // Release the SurfaceTexture on the other side. If we have lost connection then do nothing, as
    // there is nothing on the other side to release.
    try {
      if (surface.getConsumerType() == GeckoSurface.ConsumerType.SURFACE_TEXTURE) {
        sAllocator.releaseSurfaceTexture(surface.getHandle());
      } else if (surface.getConsumerType() == GeckoSurface.ConsumerType.IMAGE_READER) {
        sAllocator.releaseImageReader(surface.getHandle());
      }
    } catch (final RemoteException e) {
      Log.w(LOGTAG, "Failed to release surface", e);
    }
  }

  public static synchronized void sync(final long upstream) {
    // Sync from the SurfaceTexture on the other side. If we have lost connection then do nothing,
    // as there is nothing on the other side to sync from.
    try {
      if (sAllocator != null) {
        sAllocator.sync(upstream);
      }
    } catch (final RemoteException e) {
      Log.w(LOGTAG, "Failed to sync texture", e);
    }
  }

  @WrapForJNI
  HardwareBuffer getHardwareBufferFromImageReader(final long handle, final long timestamp) {

  }
}
