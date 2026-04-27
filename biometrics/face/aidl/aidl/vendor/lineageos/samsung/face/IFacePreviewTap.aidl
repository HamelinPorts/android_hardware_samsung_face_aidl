/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
package vendor.lineageos.samsung.face;

import vendor.lineageos.samsung.face.IFacePreviewListener;

/**
 * Vendor-side side-channel that lets priv-apps (Settings, future
 * debug tools) receive preview frames from the Samsung face HAL
 * without having to own or synthesise a NativeHandle Surface on the
 * framework side.
 *
 * The service is hosted inside
 * `android.hardware.biometrics.face-service.samsung`, which is also
 * the framework-facing AIDL IFace HAL.  Both run in the same process
 * and share one registered HIDL callback so there's no extra
 * sehSetCallback contention.
 *
 * Subscribers are notified only while the HAL is in an active enroll
 * or authenticate operation — outside those, no frames are emitted
 * by the HIDL HAL and the listener sees nothing.  Multiple
 * subscribers are allowed; each gets the same frames.
 */
@VintfStability
interface IFacePreviewTap {
    /** Start receiving onFrame callbacks for this listener. */
    void registerListener(IFacePreviewListener listener);

    /** Stop receiving onFrame callbacks for this listener. */
    void unregisterListener(IFacePreviewListener listener);
}
