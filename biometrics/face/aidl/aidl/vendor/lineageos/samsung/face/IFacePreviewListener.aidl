/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
package vendor.lineageos.samsung.face;

/**
 * Subscriber callback registered with IFacePreviewTap.  Fires once per
 * preview frame emitted by the Samsung HIDL HAL (via sehOnPreviewFrame).
 *
 * The frame arrives in a shared-memory region whose descriptor is
 * handed over via `ashmem`.  The listener mmaps the region, reads
 * `size` bytes starting at offset 0, and unmaps when done.  Format
 * codes mirror HAL_PIXEL_FORMAT_* values — in practice always 17
 * (HAL_PIXEL_FORMAT_YCrCb_420_SP / NV21) on SPRD devices.
 */
@VintfStability
interface IFacePreviewListener {
    oneway void onFrame(in ParcelFileDescriptor ashmem, int size,
                        int width, int height, int format,
                        long timestampNs);
}

