// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

package com.lineageos.samsung.face.client;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ImageFormat;
import android.graphics.Matrix;
import android.graphics.Rect;
import android.graphics.SurfaceTexture;
import android.graphics.YuvImage;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.util.AttributeSet;
import android.util.Log;
import android.view.Display;
import android.view.Surface;
import android.view.TextureView;

import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;

import vendor.lineageos.samsung.face.IFacePreviewListener;
import vendor.lineageos.samsung.face.IFacePreviewTap;

/**
 * TextureView subclass that binds the vendor
 * IFacePreviewTap/default AIDL service, subscribes for preview
 * frames emitted by the Samsung face HAL during enroll / authenticate,
 * and renders each frame onto itself.
 *
 * Designed to be dropped into any Settings overlay (SettingsGta8,
 * future SettingsA51) as a replacement for the standard Settings
 * FaceSquareTextureView.  No code inside packages/apps/Settings is
 * touched — this is a res/layout-only swap.
 *
 * Thread model: AIDL callbacks land on a binder thread.  We render
 * on the binder thread directly because TextureView::lockCanvas
 * itself is thread-safe and the render takes a few ms at 640×480.
 */
public class FacePreviewView extends TextureView
        implements TextureView.SurfaceTextureListener {

    private static final String TAG = "FacePreviewView";
    private static final String SERVICE_INSTANCE =
            "vendor.lineageos.samsung.face.IFacePreviewTap/default";

    // Samsung front camera is mounted at 270° relative to the device's
    // natural orientation.  The display-relative rotation is derived
    // per-frame from the current Surface rotation so portrait and
    // landscape both render upright.
    private static final int SENSOR_ORIENTATION_DEG = 270;

    private IFacePreviewTap mTap;
    private IFacePreviewListener mListener;
    private final Object mBindLock = new Object();
    private final BitmapFactory.Options mDecodeOpts = new BitmapFactory.Options();

    public FacePreviewView(Context ctx) {
        this(ctx, null);
    }
    public FacePreviewView(Context ctx, AttributeSet attrs) {
        this(ctx, attrs, 0);
    }
    public FacePreviewView(Context ctx, AttributeSet attrs, int defStyle) {
        super(ctx, attrs, defStyle);
        Log.i(TAG, "FacePreviewView ctor — will bind IFacePreviewTap when SurfaceTexture becomes available");
        setSurfaceTextureListener(this);
        mDecodeOpts.inPreferredConfig = Bitmap.Config.ARGB_8888;
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        Log.i(TAG, "onAttachedToWindow; visibility=" + getVisibility()
                + " width=" + getWidth() + " height=" + getHeight());
    }

    @Override
    public void onSurfaceTextureAvailable(SurfaceTexture st, int w, int h) {
        Log.i(TAG, "onSurfaceTextureAvailable " + w + "x" + h);
        bindTap();
    }
    @Override
    public void onSurfaceTextureSizeChanged(SurfaceTexture st, int w, int h) {}
    @Override
    public boolean onSurfaceTextureDestroyed(SurfaceTexture st) {
        unbindTap();
        return true;
    }
    @Override
    public void onSurfaceTextureUpdated(SurfaceTexture st) {}

    private void bindTap() {
        synchronized (mBindLock) {
            if (mTap != null) return;
            IBinder b = ServiceManager.getService(SERVICE_INSTANCE);
            if (b == null) {
                Log.w(TAG, "service " + SERVICE_INSTANCE + " not registered");
                return;
            }
            mTap = IFacePreviewTap.Stub.asInterface(b);
            mListener = new IFacePreviewListener.Stub() {
                @Override
                public void onFrame(ParcelFileDescriptor fd, int size,
                                    int width, int height, int format,
                                    long timestampNs) {
                    renderFrame(fd, size, width, height);
                }
                @Override
                public int getInterfaceVersion() {
                    return IFacePreviewListener.VERSION;
                }
                @Override
                public String getInterfaceHash() {
                    return IFacePreviewListener.HASH;
                }
            };
            try {
                mTap.registerListener(mListener);
                Log.i(TAG, "bound IFacePreviewTap/default");
            } catch (RemoteException e) {
                Log.e(TAG, "registerListener failed", e);
                mTap = null;
                mListener = null;
            }
        }
    }

    private void unbindTap() {
        synchronized (mBindLock) {
            if (mTap != null && mListener != null) {
                try { mTap.unregisterListener(mListener); }
                catch (RemoteException e) { /* service gone — ignore */ }
            }
            mTap = null;
            mListener = null;
        }
    }

    private static final int MAX_FRAME_DIM = 4096;
    private static final int MAX_FRAME_BYTES = 16 * 1024 * 1024;

    private void renderFrame(ParcelFileDescriptor fd, int size,
                             int width, int height) {
        // Defence in depth: the HAL bridge already validates these,
        // but the AIDL surface accepts any int and a buggy or
        // compromised vendor service could still push bad geometry.
        if (width <= 0 || height <= 0
                || width > MAX_FRAME_DIM || height > MAX_FRAME_DIM
                || size <= 0 || size > MAX_FRAME_BYTES
                || size < (long) width * height * 3 / 2) {
            Log.w(TAG, "renderFrame: rejecting "
                    + width + "x" + height + " size=" + size);
            return;
        }
        byte[] nv21 = new byte[size];
        // The HAL keeps a single ashmem region across all frames and
        // sends the same fd via Binder — receivers get a new fd in their
        // table but it points to the same struct file as the HAL's,
        // sharing the file position.  A plain FileInputStream.read()
        // advances that shared position, so frame N+1 starts at EOF and
        // returns zeros.  Use FileChannel positional reads (pread()
        // semantics) so the file position is never touched.
        try (FileInputStream fis =
                     new ParcelFileDescriptor.AutoCloseInputStream(fd);
             FileChannel ch = fis.getChannel()) {
            ByteBuffer bb = ByteBuffer.wrap(nv21);
            long pos = 0;
            while (bb.hasRemaining()) {
                int n = ch.read(bb, pos);
                if (n < 0) break;
                pos += n;
            }
        } catch (IOException e) {
            Log.w(TAG, "frame read", e);
            return;
        }
        Bitmap bmp;
        try {
            YuvImage yuv = new YuvImage(nv21, ImageFormat.NV21, width, height,
                                         null);
            ByteArrayOutputStream baos = new ByteArrayOutputStream(size);
            yuv.compressToJpeg(new Rect(0, 0, width, height), 80, baos);
            byte[] jpeg = baos.toByteArray();
            bmp = BitmapFactory.decodeByteArray(jpeg, 0, jpeg.length,
                                                mDecodeOpts);
        } catch (Exception e) {
            Log.w(TAG, "yuv decode", e);
            return;
        }
        if (bmp == null) return;

        Canvas canvas = lockCanvas();
        if (canvas == null) {
            bmp.recycle();
            return;
        }
        try {
            canvas.drawColor(Color.BLACK);
            final int vw = getWidth();
            final int vh = getHeight();
            final int rotation = computePreviewRotationDeg();
            final boolean swapped = rotation % 180 != 0;
            final float srcAspect = swapped
                    ? (float) height / width
                    : (float) width / height;
            final float dstAspect = (float) vw / vh;
            final float scale = (srcAspect > dstAspect)
                    ? (float) vh / (swapped ? width : height)
                    : (float) vw / (swapped ? height : width);
            Matrix m = new Matrix();
            m.postRotate(rotation, width / 2f, height / 2f);
            if (swapped) {
                m.postTranslate((height - width) / 2f, (width - height) / 2f);
            }
            m.postScale(scale, scale);
            final float drawW = (swapped ? height : width) * scale;
            final float drawH = (swapped ? width : height) * scale;
            m.postTranslate((vw - drawW) / 2f, (vh - drawH) / 2f);
            canvas.drawBitmap(bmp, m, null);
        } finally {
            unlockCanvasAndPost(canvas);
            bmp.recycle();
        }
    }

    private int computePreviewRotationDeg() {
        Display d = getDisplay();
        int displayRotationDeg = 0;
        if (d != null) {
            switch (d.getRotation()) {
                case Surface.ROTATION_0:   displayRotationDeg = 0; break;
                case Surface.ROTATION_90:  displayRotationDeg = 90; break;
                case Surface.ROTATION_180: displayRotationDeg = 180; break;
                case Surface.ROTATION_270: displayRotationDeg = 270; break;
            }
        }
        // Front-facing sensor: standard CameraManager formula adds the
        // display rotation to the sensor mount, then accounts for the
        // mirrored image plane.  We don't mirror the displayed pixels (the
        // matcher uses the unmirrored frame, so the preview matches what
        // it sees) but the angle accumulates the same way.
        return (SENSOR_ORIENTATION_DEG + displayRotationDeg) % 360;
    }
}
