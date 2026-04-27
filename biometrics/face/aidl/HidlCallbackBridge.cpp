// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "SehFaceBridge"

#include "HidlCallbackBridge.h"

#include "FacePreviewTap.h"
#include "SamsungFace.h"

#include <aidl/android/hardware/biometrics/face/Error.h>
#include <aidl/android/hardware/common/NativeHandle.h>
#include <aidl/android/hardware/keymaster/HardwareAuthToken.h>
#include <aidl/android/hardware/keymaster/HardwareAuthenticatorType.h>
#include <android-base/logging.h>

#include <cstring>
#include <limits>

namespace lineageos::samsung::biometrics::face {

using ::aidl::android::hardware::biometrics::face::Error;
using ::aidl::android::hardware::biometrics::face::ISessionCallback;
using ::aidl::android::hardware::keymaster::HardwareAuthenticatorType;
using ::aidl::android::hardware::keymaster::HardwareAuthToken;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::biometrics::face::V1_0::FaceAcquiredInfo;
using ::android::hardware::biometrics::face::V1_0::FaceError;

namespace {

// HIDL HAT wire format (matches libhardware/include/hardware/hw_auth_token.h):
//   u8   version (=0)
//   u64  challenge     (network byte order)
//   u64  userId        (network byte order)
//   u64  authenticatorId (network byte order)
//   u32  authenticatorType (network byte order)
//   u64  timestamp       (network byte order)
//   u8[32] hmac
// Total 69 bytes.  HIDL ArrayList<Byte> → hidl_vec<uint8_t> from setCallback
// carries exactly this payload on success; empty vec on failure.

constexpr size_t kHatSize = 69;

uint64_t readLE64(const uint8_t* p) {
    uint64_t r = 0;
    for (int i = 0; i < 8; ++i) r |= (static_cast<uint64_t>(p[i]) << (8 * i));
    return r;
}
uint64_t readBE64(const uint8_t* p) {
    return (static_cast<uint64_t>(p[0]) << 56) |
           (static_cast<uint64_t>(p[1]) << 48) |
           (static_cast<uint64_t>(p[2]) << 40) |
           (static_cast<uint64_t>(p[3]) << 32) |
           (static_cast<uint64_t>(p[4]) << 24) |
           (static_cast<uint64_t>(p[5]) << 16) |
           (static_cast<uint64_t>(p[6]) << 8) |
           static_cast<uint64_t>(p[7]);
}
uint32_t readBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

// Inverse of frameworks/base HardwareAuthTokenUtils.toByteArray:
// challenge/userId/authenticatorId are little-endian, authenticator
// Type/timestamp are big-endian.  See SamsungSession.cpp::hatToBytes.
bool parseHat(const ::android::hardware::hidl_vec<uint8_t>& in,
              HardwareAuthToken* out) {
    if (in.size() != kHatSize) return false;
    const uint8_t* p = in.data();
    if (p[0] != 0) return false;  // version must be 0
    out->challenge = static_cast<int64_t>(readLE64(p + 1));
    out->userId = static_cast<int64_t>(readLE64(p + 9));
    out->authenticatorId = static_cast<int64_t>(readLE64(p + 17));
    out->authenticatorType =
            static_cast<HardwareAuthenticatorType>(readBE32(p + 25));
    out->timestamp.milliSeconds = static_cast<int64_t>(readBE64(p + 29));
    out->mac.assign(p + 37, p + 69);
    return true;
}

Error hidlErrorToAidl(FaceError e) {
    switch (e) {
        case FaceError::HW_UNAVAILABLE:
            return Error::HW_UNAVAILABLE;
        case FaceError::UNABLE_TO_PROCESS:
            return Error::UNABLE_TO_PROCESS;
        case FaceError::TIMEOUT:
            return Error::TIMEOUT;
        case FaceError::NO_SPACE:
            return Error::NO_SPACE;
        case FaceError::CANCELED:
            return Error::CANCELED;
        case FaceError::UNABLE_TO_REMOVE:
            return Error::UNABLE_TO_REMOVE;
        case FaceError::VENDOR:
        default:
            return Error::VENDOR;
    }
}

}  // namespace

std::shared_ptr<ISessionCallback> HidlCallbackBridge::lockCb() {
    std::lock_guard<std::mutex> lk(mLock);
    return mCb;
}
HidlCallbackBridge::OpMode HidlCallbackBridge::currentMode() {
    std::lock_guard<std::mutex> lk(mLock);
    return mMode;
}
HidlCallbackBridge::CbSnapshot HidlCallbackBridge::snapshot() {
    std::lock_guard<std::mutex> lk(mLock);
    return {mMode, mCb};
}

void HidlCallbackBridge::beginOperation(
        OpMode mode, const std::shared_ptr<ISessionCallback>& cb) {
    std::lock_guard<std::mutex> lk(mLock);
    mMode = mode;
    mCb = cb;
}
void HidlCallbackBridge::endOperation() {
    std::lock_guard<std::mutex> lk(mLock);
    mMode = OpMode::kNone;
    // Clear the callback pointer so any in-flight vendor event that
    // arrives after the op ended cannot deliver to a stale session
    // (or worse, to a different session that just started).
    mCb.reset();
}
void HidlCallbackBridge::setPreviewSurface(
        const std::optional<::aidl::android::hardware::common::NativeHandle>&) {
    // We don't reconstruct a C++ Surface from NativeHandle — preview
    // rendering goes through the IFacePreviewTap side-channel AIDL
    // (subscribed-to by priv-apps like SettingsGta8) where the Java
    // side does YUV→Bitmap and TextureView draw.  Keeping the
    // NativeHandle arg as-is so the framework plumbing stays happy.
}

void HidlCallbackBridge::setPreviewTap(std::shared_ptr<FacePreviewTap> tap) {
    std::lock_guard<std::mutex> lk(mLock);
    mTap = std::move(tap);
}

// ----- V1.0 callbacks -----

Return<void> HidlCallbackBridge::onEnrollResult(uint64_t, uint32_t faceId,
                                                int32_t /*userId*/,
                                                uint32_t remaining) {
    LOG(INFO) << "onEnrollResult faceId=" << faceId
              << " remaining=" << remaining;
    auto s = snapshot();
    if (!s.cb || s.mode != OpMode::kEnroll) return Void();
    s.cb->onEnrollmentProgress(static_cast<int32_t>(faceId),
                               static_cast<int32_t>(remaining));
    return Void();
}

Return<void> HidlCallbackBridge::onAuthenticated(
        uint64_t, uint32_t faceId, int32_t /*userId*/,
        const ::android::hardware::hidl_vec<uint8_t>& token) {
    auto s = snapshot();
    if (!s.cb || s.mode != OpMode::kAuthenticate) return Void();
    auto& cb = s.cb;
    if (faceId == 0) {
        cb->onAuthenticationFailed();
        return Void();
    }
    // Compile-time guard: the synth fallback below is only safe while
    // the sensor is registered as BIOMETRIC_CONVENIENCE.  If a future
    // change raises kSensorStrength to STRONG without revisiting the
    // truncated-token handling, accepting an empty/short HAT would let
    // a faked auth signal cross into keystore-backed crypto.
    static_assert(kSensorStrength ==
                          ::aidl::android::hardware::biometrics::common::
                                  SensorStrength::CONVENIENCE,
                  "synth-CONVENIENCE-HAT fallback assumes sensor strength is "
                  "CONVENIENCE; revisit onAuthenticated before raising it");
    HardwareAuthToken hat{};
    if (!parseHat(token, &hat)) {
        // The vendor HAL hands the V1.0 onAuthenticated callback a
        // truncated token (not a full 69-byte HardwareAuthToken),
        // which parseHat() refuses.  This sensor is registered at
        // BIOMETRIC_CONVENIENCE strength — used for keyguard
        // dismissal only, never for keystore-backed crypto — so a
        // default-initialised HAT is sufficient.  Synthesise one
        // rather than dropping the success.
        LOG(WARNING) << "onAuthenticated: token vec size=" << token.size()
                     << " — synthesising CONVENIENCE auth token";
        hat = HardwareAuthToken{};
        hat.userId = 0;
        hat.authenticatorId = 0;
        hat.authenticatorType = HardwareAuthenticatorType::NONE;
        hat.timestamp.milliSeconds = 0;
    }
    // The sensor is registered BIOMETRIC_CONVENIENCE; pin the
    // authenticator type so a vendor HAL that happens to return a
    // STRONG-typed HAT cannot be forwarded to keystore as such.
    hat.authenticatorType = HardwareAuthenticatorType::NONE;
    cb->onAuthenticationSucceeded(static_cast<int32_t>(faceId), hat);
    return Void();
}

Return<void> HidlCallbackBridge::onAcquired(uint64_t, int32_t,
                                           FaceAcquiredInfo info,
                                           int32_t vendorCode) {
    LOG(INFO) << "onAcquired info=" << static_cast<int>(info)
              << " vendor=" << vendorCode;
    return Void();
}

Return<void> HidlCallbackBridge::onError(uint64_t, int32_t, FaceError error,
                                        int32_t vendorCode) {
    auto s = snapshot();
    if (!s.cb || s.mode == OpMode::kNone) return Void();
    s.cb->onError(hidlErrorToAidl(error), vendorCode);
    return Void();
}

Return<void> HidlCallbackBridge::onRemoved(
        uint64_t, const ::android::hardware::hidl_vec<uint32_t>& removed,
        int32_t) {
    auto cb = lockCb();
    if (!cb) return Void();
    std::vector<int32_t> ids(removed.begin(), removed.end());
    cb->onEnrollmentsRemoved(ids);
    return Void();
}

Return<void> HidlCallbackBridge::onEnumerate(
        uint64_t, const ::android::hardware::hidl_vec<uint32_t>& faceIds,
        int32_t) {
    auto cb = lockCb();
    if (!cb) return Void();
    std::vector<int32_t> ids(faceIds.begin(), faceIds.end());
    cb->onEnrollmentsEnumerated(ids);
    return Void();
}

Return<void> HidlCallbackBridge::onLockoutChanged(uint64_t duration) {
    auto cb = lockCb();
    if (!cb) return Void();
    if (duration == 0) {
        cb->onLockoutCleared();
    } else if (duration == std::numeric_limits<uint64_t>::max()) {
        cb->onLockoutPermanent();
    } else {
        cb->onLockoutTimed(static_cast<int64_t>(duration));
    }
    return Void();
}

// ----- V2.0 extensions -----

Return<void> HidlCallbackBridge::sehOnPreviewUpdated(
        const ::android::hardware::hidl_vec<uint8_t>& data, int32_t confidence,
        int32_t faceId, int32_t, uint32_t) {
    // SECURITY: `data` is a vendor face-feature blob — never log its
    // contents.  Only its size is safe to surface.
    LOG(INFO) << "sehOnPreviewUpdated conf=" << confidence << " faceId="
              << faceId << " sz=" << data.size();
    return Void();
}

Return<void> HidlCallbackBridge::sehOnAuthenticated(
        uint64_t, uint32_t faceId, int32_t /*userId*/,
        const ::android::hardware::hidl_vec<uint8_t>&,
        const ::android::hardware::hidl_vec<uint8_t>& hardwareAuthToken) {
    // Samsung also emits a second, extended "auth" event with a HAT
    // in the second vec.  If the V1.0 onAuthenticated event carried
    // an empty token we can still use this one.  Prefer this when
    // both fire.
    auto cb = lockCb();
    if (!cb || faceId == 0) {
        if (cb) cb->onAuthenticationFailed();
        return Void();
    }
    HardwareAuthToken hat{};
    if (!parseHat(hardwareAuthToken, &hat)) {
        cb->onAuthenticationFailed();
        return Void();
    }
    // The sensor is registered BIOMETRIC_CONVENIENCE; pin the
    // authenticator type so a vendor HAL that happens to return a
    // STRONG-typed HAT cannot be forwarded to keystore as such.
    hat.authenticatorType = HardwareAuthenticatorType::NONE;
    cb->onAuthenticationSucceeded(static_cast<int32_t>(faceId), hat);
    return Void();
}

Return<void> HidlCallbackBridge::sehOnPreviewFrame(
        const ::android::hardware::hidl_memory& frame, int32_t w, int32_t h,
        int32_t format, uint32_t timestamp) {
    LOG(INFO) << "sehOnPreviewFrame " << w << "x" << h << " fmt=" << format
              << " size=" << frame.size();
    // Validate vendor-supplied geometry before fanning out to clients.
    // A misbehaving HAL must not be able to drive priv-app receivers
    // into oversize allocations (NegativeArraySizeException /
    // OutOfMemoryError on the Java side) or YuvImage OOB reads.
    constexpr int kMaxDim = 4096;
    constexpr uint64_t kMaxFrameBytes = 16ull * 1024 * 1024;
    if (w <= 0 || h <= 0 || w > kMaxDim || h > kMaxDim) {
        LOG(WARNING) << "sehOnPreviewFrame: bad geometry " << w << "x" << h;
        return Void();
    }
    const uint64_t size = frame.size();
    const uint64_t nv21Min = static_cast<uint64_t>(w) * h * 3 / 2;
    if (size > kMaxFrameBytes || size < nv21Min) {
        LOG(WARNING) << "sehOnPreviewFrame: bad size=" << size
                     << " for " << w << "x" << h
                     << " (nv21Min=" << nv21Min << ")";
        return Void();
    }
    std::shared_ptr<FacePreviewTap> tap;
    OpMode mode;
    {
        std::lock_guard<std::mutex> lk(mLock);
        tap = mTap;
        mode = mMode;
    }
    // Preview frames are only meaningful while an enroll or authenticate
    // is in flight.  If the vendor HAL emits one outside those windows
    // (warm-up, race after cancellation), drop it rather than fanning
    // it out to listeners.
    if (mode != OpMode::kEnroll && mode != OpMode::kAuthenticate) {
        return Void();
    }
    if (!tap) {
        LOG(WARNING) << "sehOnPreviewFrame: no tap attached — dropping frame";
        return Void();
    }
    const auto& nh = frame.handle();
    if (nh == nullptr || nh->numFds < 1) {
        LOG(WARNING) << "sehOnPreviewFrame: no fd in hidl_memory";
        return Void();
    }
    const int fd = nh->data[0];
    tap->dispatchFrame(fd, static_cast<int32_t>(size), w, h, format,
                       static_cast<int64_t>(timestamp));
    return Void();
}

}  // namespace lineageos::samsung::biometrics::face
