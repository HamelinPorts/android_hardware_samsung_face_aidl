// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "SamsungFaceBackend"

#include "HidlBackend.h"

#include "FacePreviewTap.h"
#include "HidlCallbackBridge.h"

#include <aidl/android/hardware/biometrics/face/ISessionCallback.h>
#include <android-base/logging.h>

namespace lineageos::samsung::biometrics::face {

HidlBackend::HidlBackend() = default;
HidlBackend::~HidlBackend() = default;

void HidlBackend::ensureBoundLocked() {
    if (mSeh != nullptr) return;
    mSeh = hidl_face_v2::ISehBiometricsFace::getService();
    if (mSeh == nullptr) {
        LOG(ERROR) << "ISehBiometricsFace@2.0/default unavailable";
        return;
    }
    mBase = mSeh;  // @2.0 extends @1.0
    if (!mBridge) {
        mBridge = ::android::sp<HidlCallbackBridge>::make();
    }

    // The vendor HAL has a single client-callback slot shared between
    // the @1.0 setCallback and the @2.0 sehSetCallback paths.  We
    // register only via sehSetCallback; the bridge implements both
    // V1.0 and V2.0 callback interfaces so every event kind is
    // routed to it.  Registering on the @1.0 slot in addition causes
    // the vendor HAL to clear authenticator-id / challenge state and
    // breaks subsequent enroll() with "token mismatch" — so we don't.
    ::android::hardware::biometrics::face::V1_0::OptionalUint64 sehResult{};
    auto sehRet = mSeh->sehSetCallback(
            mBridge,
            [&sehResult](const auto& r) { sehResult = r; });
    if (!sehRet.isOk()) {
        LOG(ERROR) << "sehSetCallback transport error: "
                   << sehRet.description();
        // Tear down so the next ensureBoundLocked() retries from
        // scratch — otherwise mSeh stays bound but uncallback'd and
        // every subsequent vendor event is silently dropped.
        mSeh = nullptr;
        mBase = nullptr;
        return;
    }
    mCallbackRegistered = true;
    LOG(INFO) << "Samsung HIDL face HAL bound; sehSetCallback installed "
                 "status="
              << static_cast<int>(sehResult.status)
              << " deviceId=" << sehResult.value;
}

::android::sp<hidl_face_v2::ISehBiometricsFace> HidlBackend::getSeh() {
    std::lock_guard<std::mutex> lk(mLock);
    ensureBoundLocked();
    return mSeh;
}

::android::sp<hidl_face_v1::IBiometricsFace> HidlBackend::getBase() {
    std::lock_guard<std::mutex> lk(mLock);
    ensureBoundLocked();
    return mBase;
}

void HidlBackend::beginOperation(
        HidlCallbackBridge::OpMode mode,
        const std::shared_ptr<
                aidl::android::hardware::biometrics::face::ISessionCallback>&
                cb) {
    std::lock_guard<std::mutex> lk(mLock);
    ensureBoundLocked();
    if (mBridge) mBridge->beginOperation(mode, cb);
}

void HidlBackend::endOperation() {
    std::lock_guard<std::mutex> lk(mLock);
    if (mBridge) mBridge->endOperation();
}

HidlCallbackBridge* HidlBackend::bridge() {
    std::lock_guard<std::mutex> lk(mLock);
    ensureBoundLocked();
    return mBridge.get();
}

void HidlBackend::attachPreviewTap(std::shared_ptr<FacePreviewTap> tap) {
    std::lock_guard<std::mutex> lk(mLock);
    ensureBoundLocked();
    if (mBridge) mBridge->setPreviewTap(std::move(tap));
}

}  // namespace lineageos::samsung::biometrics::face
