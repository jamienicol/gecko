/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.gfx;

import android.util.LongSparseArray;
import android.view.Surface;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.mozglue.JNIObject;

@WrapForJNI
public final class GeckoImageReader extends JNIObject {
  static final LongSparseArray<GeckoImageReader> sImageReaders = new LongSparseArray<>();

  public static GeckoImageReader create(
      final long handle,
      final int width,
      final int height,
      final int format,
      final int maxImages,
      final long usage) {
    final GeckoImageReader imageReader = nativeCreate(width, height, format, maxImages, usage);
    synchronized (sImageReaders) {
      sImageReaders.put(handle, imageReader);
    }
    return imageReader;
  }

  public static void release(final long handle) {
    synchronized (sImageReaders) {
      sImageReaders.remove(handle);
    }
  }

  public static GeckoImageReader lookup(final long handle) {
    synchronized (sImageReaders) {
      return sImageReaders.get(handle);
    }
  }

  private static native GeckoImageReader nativeCreate(
      final int width, final int height, final int format, final int maxImages, final long usage);

  @Override
  protected native void disposeNative();

  public native Surface getSurface();
}
