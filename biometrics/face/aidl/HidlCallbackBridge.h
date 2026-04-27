// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/biometrics/face/ISessionCallback.h>
#include <vendor/samsung/hardware/biometrics/face/2.0/ISehBiometricsFaceClientCallback.h>

#include <memory>
#include <mutex>
#include <optional>

namespace aidl::android::hardware::common {
class NativeHandle;
}

namespace lineageos::samsung::biometrics::face {

class FacePreviewTap;

/**
 * HIDL-side callback registered via sehSetCallback with the Samsung
 * face HAL.  Forwards every event to the currently-active
 * AIDL ISessionCallback.  Also holds the preview Surface supplied at
 * enroll() time and renders sehOnPreviewFrame YUV payloads into it.
 *
 * Only one SamsungSession is active at a time.  SamsungSession calls
 * setActiveSessionCallback() when a new operation starts and again
 * with nullptr when it ends / on close().
 */
class HidlCallbackBridge
    : public ::vendor::samsung::hardware::biometrics::face::V2_0::
              ISehBiometricsFaceClientCallback {
   public:
    enum class OpMode {
        kNone = 0,
        kEnroll,
        kAuthenticate,
        kDetectInteraction,
    };

    // --- V1.0 callbacks ---
    ::android::hardware::Return<void> onEnrollResult(uint64_t deviceId,
                                                     uint32_t faceId,
                                                     int32_t userId,
                                                     uint32_t remaining)
            override;
    ::android::hardware::Return<void> onAuthenticated(
            uint64_t deviceId, uint32_t faceId, int32_t userId,
            const ::android::hardware::hidl_vec<uint8_t>& token) override;
    ::android::hardware::Return<void> onAcquired(
            uint64_t deviceId, int32_t userId,
            ::android::hardware::biometrics::face::V1_0::FaceAcquiredInfo
                    acquiredInfo,
            int32_t vendorCode) override;
    ::android::hardware::Return<void> onError(
            uint64_t deviceId, int32_t userId,
            ::android::hardware::biometrics::face::V1_0::FaceError error,
            int32_t vendorCode) override;
    ::android::hardware::Return<void> onRemoved(
            uint64_t deviceId,
            const ::android::hardware::hidl_vec<uint32_t>& removed,
            int32_t userId) override;
    ::android::hardware::Return<void> onEnumerate(
            uint64_t deviceId,
            const ::android::hardware::hidl_vec<uint32_t>& faceIds,
            int32_t userId) override;
    ::android::hardware::Return<void> onLockoutChanged(uint64_t duration)
            override;

    // --- V2.0 extensions ---
    ::android::hardware::Return<void> sehOnPreviewUpdated(
            const ::android::hardware::hidl_vec<uint8_t>& data,
            int32_t confidence, int32_t faceId, int32_t userId,
            uint32_t timestamp) override;
    ::android::hardware::Return<void> sehOnAuthenticated(
            uint64_t deviceId, uint32_t faceId, int32_t userId,
            const ::android::hardware::hidl_vec<uint8_t>& token,
            const ::android::hardware::hidl_vec<uint8_t>& hardwareAuthToken)
            override;
    ::android::hardware::Return<void> sehOnPreviewFrame(
            const ::android::hardware::hidl_memory& frame, int32_t w,
            int32_t h, int32_t format, uint32_t timestamp) override;

    // --- session-side API ---

    /** Called by SamsungSession when an op starts / ends. */
    void beginOperation(
            OpMode mode,
            const std::shared_ptr<
                    ::aidl::android::hardware::biometrics::face::ISessionCallback>&
                    cb);
    void endOperation();

    /** Settings-supplied Surface for preview rendering during enroll. */
    void setPreviewSurface(
            const std::optional<
                    ::aidl::android::hardware::common::NativeHandle>& handle);

    /** Sink for preview-frame fan-out via the IFacePreviewTap AIDL. */
    void setPreviewTap(std::shared_ptr<FacePreviewTap> tap);

   private:
    std::shared_ptr<::aidl::android::hardware::biometrics::face::ISessionCallback>
    lockCb();
    OpMode currentMode();
    // Atomic snapshot of (mode, cb) — used by op-specific callbacks
    // to avoid a TOCTOU race where the mode is checked separately
    // from acquiring the callback pointer.
    struct CbSnapshot {
        OpMode mode;
        std::shared_ptr<
                ::aidl::android::hardware::biometrics::face::ISessionCallback>
                cb;
    };
    CbSnapshot snapshot();

    std::mutex mLock;
    std::shared_ptr<::aidl::android::hardware::biometrics::face::ISessionCallback>
            mCb;
    std::shared_ptr<FacePreviewTap> mTap;
    OpMode mMode = OpMode::kNone;
};

}  // namespace lineageos::samsung::biometrics::face
