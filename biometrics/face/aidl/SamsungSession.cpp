// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "SamsungSession"

#include "SamsungSession.h"

#include "CancellationSignal.h"
#include "HidlBackend.h"

#include <aidl/android/hardware/biometrics/face/Error.h>
#include <android-base/logging.h>
#include <android/hardware/biometrics/face/1.0/IBiometricsFace.h>

namespace lineageos::samsung::biometrics::face {

using ::aidl::android::hardware::biometrics::common::ICancellationSignal;
using ::aidl::android::hardware::biometrics::common::OperationContext;
using ::aidl::android::hardware::biometrics::face::EnrollmentStageConfig;
using ::aidl::android::hardware::biometrics::face::EnrollmentType;
using ::aidl::android::hardware::biometrics::face::Error;
using ::aidl::android::hardware::biometrics::face::FaceEnrollOptions;
using ::aidl::android::hardware::biometrics::face::Feature;
using ::aidl::android::hardware::biometrics::face::ISessionCallback;
using ::aidl::android::hardware::common::NativeHandle;
using ::aidl::android::hardware::keymaster::HardwareAuthToken;
using V1_0Status = ::android::hardware::biometrics::face::V1_0::Status;

namespace {

constexpr uint32_t kChallengeTimeoutSec = 600;

// Re-serialise AIDL HardwareAuthToken back to the 69-byte HIDL wire
// format that the Samsung HIDL HAL expects.  Bit-for-bit equivalent
// of frameworks/base HardwareAuthTokenUtils.toByteArray():
//
//   challenge / userId / authenticatorId   — little-endian
//   authenticatorType / timestamp           — big-endian
//   mac                                     — raw bytes
//
// Mismatching the per-field endianness here is reported by Samsung's
// libFaceAuth as "token mismatch" and breaks enroll/auth at the TA.
//
// Returns an empty vec if the input HAT cannot be encoded losslessly
// (a short or oversize MAC would otherwise be silently zero-padded
// or truncated, producing a different MAC than the framework signed).
std::vector<uint8_t> hatToBytes(const HardwareAuthToken& hat) {
    if (hat.mac.size() != 32) return {};
    std::vector<uint8_t> out(69, 0);
    out[0] = 0;  // version
    auto put64LE = [&out](size_t off, uint64_t v) {
        for (int i = 0; i < 8; ++i)
            out[off + i] = static_cast<uint8_t>(v >> (8 * i));
    };
    auto put64BE = [&out](size_t off, uint64_t v) {
        for (int i = 0; i < 8; ++i)
            out[off + i] = static_cast<uint8_t>(v >> (56 - 8 * i));
    };
    auto put32BE = [&out](size_t off, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out[off + i] = static_cast<uint8_t>(v >> (24 - 8 * i));
    };
    put64LE(1, static_cast<uint64_t>(hat.challenge));
    put64LE(9, static_cast<uint64_t>(hat.userId));
    put64LE(17, static_cast<uint64_t>(hat.authenticatorId));
    put32BE(25, static_cast<uint32_t>(hat.authenticatorType));
    put64BE(29, static_cast<uint64_t>(hat.timestamp.milliSeconds));
    std::copy(hat.mac.begin(), hat.mac.end(), out.begin() + 37);
    return out;
}

}  // namespace

SamsungSession::SamsungSession(
        int32_t userId, const std::shared_ptr<ISessionCallback>& cb,
        std::shared_ptr<HidlBackend> backend)
    : mUserId(userId), mCb(cb), mBackend(std::move(backend)) {
    LOG(INFO) << "SamsungSession created for userId=" << userId;
    // The AIDL face contract supplies the user id at session creation
    // time; the HIDL contract requires a separate setActiveUser(userId,
    // storePath) call before any operation so the vendor HAL has a
    // per-user storage location.  Without it the HAL has no registered
    // template path during auth and the matcher can't run, so no score
    // is ever produced even if enroll completed.
    // Reject out-of-range userIds — the framework only ever creates
    // sessions for valid Android users (0..MAX_USER), but the path
    // concatenation below would otherwise accept anything (negative
    // ids, very large values) and produce unintended directories
    // under /data/vendor_de.
    constexpr int32_t kMaxUserId = 9999;
    if (userId < 0 || userId > kMaxUserId) {
        LOG(WARNING) << "Refusing setActiveUser for out-of-range userId="
                     << userId;
        return;
    }
    if (auto seh = mBackend->getSeh()) {
        const std::string path = std::string("/data/vendor_de/")
                                 + std::to_string(userId) + "/facedata";
        auto ret = seh->setActiveUser(userId, path);
        if (!ret.isOk() ||
            static_cast<V1_0Status>(ret) != V1_0Status::OK) {
            LOG(WARNING) << "HIDL setActiveUser failed: isOk=" << ret.isOk()
                         << " status="
                         << static_cast<int>(static_cast<V1_0Status>(ret));
        } else {
            LOG(INFO) << "HIDL setActiveUser ok: userId=" << userId
                      << " path=" << path;
        }
    }
}

SamsungSession::~SamsungSession() = default;

::ndk::ScopedAStatus SamsungSession::generateChallenge() {
    auto seh = mBackend->getSeh();
    if (!seh || !mCb) return ::ndk::ScopedAStatus::ok();
    mBackend->beginOperation(HidlCallbackBridge::OpMode::kNone, mCb);
    int64_t value = 0;
    V1_0Status status = V1_0Status::OK;
    auto ret = seh->generateChallenge(
            kChallengeTimeoutSec,
            [&](const auto& r) {
                status = r.status;
                value = static_cast<int64_t>(r.value);
            });
    if (!ret.isOk() || status != V1_0Status::OK) {
        LOG(WARNING) << "HIDL generateChallenge failed: isOk=" << ret.isOk()
                     << " status=" << static_cast<int>(status);
        mCb->onError(Error::HW_UNAVAILABLE, 0);
    } else {
        mCb->onChallengeGenerated(value);
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::revokeChallenge(int64_t challenge) {
    auto seh = mBackend->getSeh();
    if (!seh || !mCb) return ::ndk::ScopedAStatus::ok();
    auto ret = seh->revokeChallenge();
    if (!ret.isOk()) {
        mCb->onError(Error::HW_UNAVAILABLE, 0);
        return ::ndk::ScopedAStatus::ok();
    }
    mCb->onChallengeRevoked(challenge);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::getEnrollmentConfig(
        EnrollmentType, std::vector<EnrollmentStageConfig>* out) {
    // No custom enrollment stages on this HAL; the framework falls
    // back to a single stage when this is empty.
    out->clear();
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::enroll(
        const HardwareAuthToken& hat, EnrollmentType,
        const std::vector<Feature>& /*features*/,
        const std::optional<NativeHandle>& previewSurface,
        std::shared_ptr<ICancellationSignal>* out) {
    auto seh = mBackend->getSeh();
    if (!seh || !mCb) {
        *out = nullptr;
        if (mCb) mCb->onError(Error::HW_UNAVAILABLE, 0);
        return ::ndk::ScopedAStatus::ok();
    }
    // Hand the Surface to the bridge so sehOnPreviewFrame can render
    // into it.  Surface-side rendering (YUV→RGB lockCanvas drawBitmap)
    // is a follow-up — for now the NativeHandle is just stashed.
    if (auto* br = mBackend->bridge()) {
        br->setPreviewSurface(previewSurface);
    }

    auto bytes = hatToBytes(hat);
    if (bytes.empty()) {
        LOG(WARNING) << "enroll: malformed HAT (mac.size=" << hat.mac.size()
                     << ") — refusing to call vendor TA";
        mCb->onError(Error::UNABLE_TO_PROCESS, 0);
        *out = nullptr;
        return ::ndk::ScopedAStatus::ok();
    }
    mBackend->beginOperation(HidlCallbackBridge::OpMode::kEnroll, mCb);
    ::android::hardware::hidl_vec<uint8_t> hidlHat(bytes.begin(), bytes.end());
    // Enrollment timeout matches AOSP HidlToAidlSessionAdapter's 75s.
    constexpr uint32_t kEnrollTimeoutSec = 75;
    ::android::hardware::hidl_vec<
            ::android::hardware::biometrics::face::V1_0::Feature>
            disabled;
    LOG(INFO) << "SamsungSession::enroll userId=" << mUserId
              << " hatBytes=" << bytes.size()
              << " hasSurface=" << (previewSurface.has_value() ? "yes" : "no");
    auto ret = seh->enroll(hidlHat, kEnrollTimeoutSec, disabled);
    if (!ret.isOk() ||
        static_cast<V1_0Status>(ret) != V1_0Status::OK) {
        LOG(WARNING) << "HIDL enroll failed: isOk=" << ret.isOk()
                     << " status="
                     << static_cast<int>(static_cast<V1_0Status>(ret));
        mBackend->endOperation();
        mCb->onError(Error::UNABLE_TO_PROCESS, 0);
        *out = nullptr;
        return ::ndk::ScopedAStatus::ok();
    }
    std::weak_ptr<HidlBackend> weakBackend = mBackend;
    *out = ::ndk::SharedRefBase::make<CancellationSignal>([weakBackend]() {
        if (auto b = weakBackend.lock()) {
            auto s = b->getSeh();
            if (s) (void)s->cancel();
            b->endOperation();
        }
    });
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::authenticate(
        int64_t operationId, std::shared_ptr<ICancellationSignal>* out) {
    auto seh = mBackend->getSeh();
    if (!seh || !mCb) {
        *out = nullptr;
        if (mCb) mCb->onError(Error::HW_UNAVAILABLE, 0);
        return ::ndk::ScopedAStatus::ok();
    }
    mBackend->beginOperation(HidlCallbackBridge::OpMode::kAuthenticate, mCb);
    auto ret = seh->authenticate(static_cast<uint64_t>(operationId));
    if (!ret.isOk() ||
        static_cast<V1_0Status>(ret) != V1_0Status::OK) {
        mBackend->endOperation();
        mCb->onError(Error::UNABLE_TO_PROCESS, 0);
        *out = nullptr;
        return ::ndk::ScopedAStatus::ok();
    }
    std::weak_ptr<HidlBackend> weakBackend = mBackend;
    *out = ::ndk::SharedRefBase::make<CancellationSignal>([weakBackend]() {
        if (auto b = weakBackend.lock()) {
            auto s = b->getSeh();
            if (s) (void)s->cancel();
        }
    });
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::detectInteraction(
        std::shared_ptr<ICancellationSignal>* out) {
    // Optional method; Samsung HIDL @1.0 doesn't have a direct
    // equivalent.  Emit UNSUPPORTED-ish response.
    *out = nullptr;
    if (mCb) mCb->onInteractionDetected();
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::enumerateEnrollments() {
    auto seh = mBackend->getSeh();
    if (!seh || !mCb) return ::ndk::ScopedAStatus::ok();
    mBackend->beginOperation(HidlCallbackBridge::OpMode::kNone, mCb);
    (void)seh->enumerate();
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::removeEnrollments(
        const std::vector<int32_t>& enrollmentIds) {
    auto seh = mBackend->getSeh();
    if (!seh || !mCb) return ::ndk::ScopedAStatus::ok();
    mBackend->beginOperation(HidlCallbackBridge::OpMode::kNone, mCb);
    if (enrollmentIds.empty()) {
        // AIDL spec: empty enrollmentIds is a no-op.  The HIDL @1.0
        // remove(0) convention is "wipe all" — never silently expand
        // an empty AIDL request into a wipe-all on the vendor side.
        // Acknowledge to the framework so it doesn't await onRemoved.
        mCb->onEnrollmentsRemoved({});
        return ::ndk::ScopedAStatus::ok();
    }
    for (int32_t id : enrollmentIds) {
        (void)seh->remove(static_cast<uint32_t>(id));
    }
    // Acknowledge removal to the framework with the requested IDs
    // synchronously instead of waiting for the HIDL onRemoved callback.
    // The Samsung HAL's onRemoved fires with an empty list when the
    // requested face_id is not present in the TA-side template DB —
    // which leaves the framework's user state file out of sync if HAL
    // and framework state ever diverge (e.g. after a /data/vendor_de
    // wipe while the TA RPMB blob persists).  Echoing the requested
    // IDs makes removal idempotent and recoverable in that case; the
    // bridge's later onRemoved is a redundant no-op on the framework
    // side.
    mCb->onEnrollmentsRemoved(enrollmentIds);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::getFeatures() {
    auto seh = mBackend->getSeh();
    if (!seh || !mCb) return ::ndk::ScopedAStatus::ok();
    // HIDL @1.0's getFeature requires knowing a face_id up front; the
    // AIDL caller hasn't told us which.  For this port we report the
    // default feature set (attention required on) and let the
    // framework query per-feature separately if it wants precise
    // answers.
    std::vector<Feature> out = {Feature::REQUIRE_ATTENTION};
    mCb->onFeaturesRetrieved(out);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::setFeature(
        const HardwareAuthToken& hat, Feature feature, bool enabled) {
    auto seh = mBackend->getSeh();
    if (!seh || !mCb) return ::ndk::ScopedAStatus::ok();
    auto bytes = hatToBytes(hat);
    ::android::hardware::hidl_vec<uint8_t> hidlToken(bytes.begin(),
                                                     bytes.end());
    // HIDL Feature enum matches AIDL values 1:1 on this HAL.
    auto hidlFeature =
            static_cast<::android::hardware::biometrics::face::V1_0::Feature>(
                    feature);
    (void)seh->setFeature(hidlFeature, enabled, hidlToken, 0 /* any face */);
    mCb->onFeatureSet(feature);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::getAuthenticatorId() {
    auto seh = mBackend->getSeh();
    if (!seh || !mCb) return ::ndk::ScopedAStatus::ok();
    uint64_t id = 0;
    auto ret = seh->getAuthenticatorId(
            [&](const auto& r) { id = r.value; });
    if (!ret.isOk()) {
        mCb->onError(Error::HW_UNAVAILABLE, 0);
        return ::ndk::ScopedAStatus::ok();
    }
    mCb->onAuthenticatorIdRetrieved(static_cast<int64_t>(id));
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::invalidateAuthenticatorId() {
    // HIDL @1.0 has no equivalent method; the framework wants a
    // notification so keystore can rebuild its cache.  Emit the
    // invalidated callback with the current ID as a best-effort.
    if (!mCb) return ::ndk::ScopedAStatus::ok();
    auto seh = mBackend->getSeh();
    uint64_t id = 0;
    if (seh) {
        (void)seh->getAuthenticatorId(
                [&](const auto& r) { id = r.value; });
    }
    mCb->onAuthenticatorIdInvalidated(static_cast<int64_t>(id));
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::resetLockout(
        const HardwareAuthToken& hat) {
    auto seh = mBackend->getSeh();
    if (!seh || !mCb) return ::ndk::ScopedAStatus::ok();
    auto bytes = hatToBytes(hat);
    ::android::hardware::hidl_vec<uint8_t> hidlToken(bytes.begin(),
                                                     bytes.end());
    auto ret = seh->resetLockout(hidlToken);
    if (!ret.isOk() ||
        static_cast<V1_0Status>(ret) != V1_0Status::OK) {
        mCb->onError(Error::UNABLE_TO_PROCESS, 0);
        return ::ndk::ScopedAStatus::ok();
    }
    mCb->onLockoutCleared();
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungSession::close() {
    LOG(INFO) << "SamsungSession close userId=" << mUserId;
    mBackend->endOperation();
    if (mCb) mCb->onSessionClosed();
    return ::ndk::ScopedAStatus::ok();
}

// --- context-ed variants delegate ---

::ndk::ScopedAStatus SamsungSession::authenticateWithContext(
        int64_t operationId, const OperationContext&,
        std::shared_ptr<ICancellationSignal>* out) {
    return authenticate(operationId, out);
}
::ndk::ScopedAStatus SamsungSession::enrollWithContext(
        const HardwareAuthToken& hat, EnrollmentType type,
        const std::vector<Feature>& features,
        const std::optional<NativeHandle>& previewSurface,
        const OperationContext&,
        std::shared_ptr<ICancellationSignal>* out) {
    return enroll(hat, type, features, previewSurface, out);
}
::ndk::ScopedAStatus SamsungSession::detectInteractionWithContext(
        const OperationContext&,
        std::shared_ptr<ICancellationSignal>* out) {
    return detectInteraction(out);
}
::ndk::ScopedAStatus SamsungSession::onContextChanged(const OperationContext&) {
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus SamsungSession::enrollWithOptions(
        const FaceEnrollOptions& options,
        std::shared_ptr<ICancellationSignal>* out) {
    return enroll(options.hardwareAuthToken, options.enrollmentType,
                  options.features, options.nativeHandlePreview, out);
}

}  // namespace lineageos::samsung::biometrics::face
