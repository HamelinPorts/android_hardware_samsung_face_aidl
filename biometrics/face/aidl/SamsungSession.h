// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/biometrics/face/BnSession.h>
#include <aidl/android/hardware/biometrics/face/ISessionCallback.h>

#include <memory>

namespace lineageos::samsung::biometrics::face {

class HidlBackend;

/**
 * Per-client ISession for the Samsung HIDL-backed face HAL.  Each
 * framework-side `FaceService.createSession()` produces one of these;
 * its lifetime is tied to the framework-side session object.
 *
 * Methods are mostly thin wrappers that translate the AIDL call into
 * a Samsung HIDL @1.0 / @2.0 equivalent, and propagate callbacks in
 * the other direction via the HidlCallbackBridge registered by
 * HidlBackend.
 */
class SamsungSession
    : public aidl::android::hardware::biometrics::face::BnSession {
   public:
    SamsungSession(
            int32_t userId,
            const std::shared_ptr<
                    aidl::android::hardware::biometrics::face::ISessionCallback>&
                    cb,
            std::shared_ptr<HidlBackend> backend);
    ~SamsungSession() override;

    // ISession methods — all stubbed in the skeleton; filled in by
    // subsequent tasks (#11 .. #15).
    ::ndk::ScopedAStatus generateChallenge() override;
    ::ndk::ScopedAStatus revokeChallenge(int64_t challenge) override;
    ::ndk::ScopedAStatus getEnrollmentConfig(
            aidl::android::hardware::biometrics::face::EnrollmentType type,
            std::vector<
                    aidl::android::hardware::biometrics::face::EnrollmentStageConfig>*
                    out) override;
    ::ndk::ScopedAStatus enroll(
            const aidl::android::hardware::keymaster::HardwareAuthToken& hat,
            aidl::android::hardware::biometrics::face::EnrollmentType type,
            const std::vector<aidl::android::hardware::biometrics::face::Feature>&
                    features,
            const std::optional<aidl::android::hardware::common::NativeHandle>&
                    previewSurface,
            std::shared_ptr<
                    aidl::android::hardware::biometrics::common::ICancellationSignal>*
                    out) override;
    ::ndk::ScopedAStatus authenticate(
            int64_t operationId,
            std::shared_ptr<
                    aidl::android::hardware::biometrics::common::ICancellationSignal>*
                    out) override;
    ::ndk::ScopedAStatus detectInteraction(
            std::shared_ptr<
                    aidl::android::hardware::biometrics::common::ICancellationSignal>*
                    out) override;
    ::ndk::ScopedAStatus enumerateEnrollments() override;
    ::ndk::ScopedAStatus removeEnrollments(
            const std::vector<int32_t>& enrollmentIds) override;
    ::ndk::ScopedAStatus getFeatures() override;
    ::ndk::ScopedAStatus setFeature(
            const aidl::android::hardware::keymaster::HardwareAuthToken& hat,
            aidl::android::hardware::biometrics::face::Feature feature,
            bool enabled) override;
    ::ndk::ScopedAStatus getAuthenticatorId() override;
    ::ndk::ScopedAStatus invalidateAuthenticatorId() override;
    ::ndk::ScopedAStatus resetLockout(
            const aidl::android::hardware::keymaster::HardwareAuthToken& hat)
            override;
    ::ndk::ScopedAStatus close() override;

    ::ndk::ScopedAStatus authenticateWithContext(
            int64_t operationId,
            const aidl::android::hardware::biometrics::common::OperationContext&
                    ctx,
            std::shared_ptr<
                    aidl::android::hardware::biometrics::common::ICancellationSignal>*
                    out) override;
    ::ndk::ScopedAStatus enrollWithContext(
            const aidl::android::hardware::keymaster::HardwareAuthToken& hat,
            aidl::android::hardware::biometrics::face::EnrollmentType type,
            const std::vector<aidl::android::hardware::biometrics::face::Feature>&
                    features,
            const std::optional<aidl::android::hardware::common::NativeHandle>&
                    previewSurface,
            const aidl::android::hardware::biometrics::common::OperationContext&
                    ctx,
            std::shared_ptr<
                    aidl::android::hardware::biometrics::common::ICancellationSignal>*
                    out) override;
    ::ndk::ScopedAStatus detectInteractionWithContext(
            const aidl::android::hardware::biometrics::common::OperationContext&
                    ctx,
            std::shared_ptr<
                    aidl::android::hardware::biometrics::common::ICancellationSignal>*
                    out) override;
    ::ndk::ScopedAStatus onContextChanged(
            const aidl::android::hardware::biometrics::common::OperationContext&
                    ctx) override;
    ::ndk::ScopedAStatus enrollWithOptions(
            const aidl::android::hardware::biometrics::face::FaceEnrollOptions&
                    options,
            std::shared_ptr<
                    aidl::android::hardware::biometrics::common::ICancellationSignal>*
                    out) override;

   private:
    int32_t mUserId;
    std::shared_ptr<
            aidl::android::hardware::biometrics::face::ISessionCallback>
            mCb;
    std::shared_ptr<HidlBackend> mBackend;
};

}  // namespace lineageos::samsung::biometrics::face
