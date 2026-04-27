// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "HidlCallbackBridge.h"

#include <android/hardware/biometrics/face/1.0/IBiometricsFace.h>
#include <vendor/samsung/hardware/biometrics/face/2.0/ISehBiometricsFace.h>
#include <vendor/samsung/hardware/biometrics/face/2.0/ISehBiometricsFaceClientCallback.h>

#include <memory>
#include <mutex>

namespace aidl::android::hardware::biometrics::face {
class ISessionCallback;
}

namespace lineageos::samsung::biometrics::face {

namespace hidl_face_v1 = ::android::hardware::biometrics::face::V1_0;
namespace hidl_face_v2 = ::vendor::samsung::hardware::biometrics::face::V2_0;

/**
 * Owns the connection to the Samsung HIDL face HAL.  Lazily binds
 * ISehBiometricsFace@2.0/default on first use and registers a single
 * process-wide HidlCallbackBridge via sehSetCallback.  Each
 * SamsungSession sets itself as the current bridge target so HIDL
 * events get routed back to the right AIDL client session.
 *
 * Only one session is expected to be active at a time (mirrors the
 * Samsung HIDL HAL's single-client model).
 */
class HidlBackend {
   public:
    HidlBackend();
    ~HidlBackend();

    /** Returns nullptr if the HIDL service is unreachable. */
    ::android::sp<hidl_face_v2::ISehBiometricsFace> getSeh();
    ::android::sp<hidl_face_v1::IBiometricsFace> getBase();

    /** Hands an AIDL session-callback to the bridge for the duration of
     *  an enroll / authenticate / detectInteraction op. */
    void beginOperation(
            HidlCallbackBridge::OpMode mode,
            const std::shared_ptr<
                    aidl::android::hardware::biometrics::face::ISessionCallback>&
                    cb);
    void endOperation();

    /** Raw bridge handle for SamsungSession to set the preview surface. */
    HidlCallbackBridge* bridge();

    /** Wired from main.cpp at startup.  Every sehOnPreviewFrame gets
     *  fanned out through this tap. */
    void attachPreviewTap(std::shared_ptr<class FacePreviewTap> tap);

   private:
    void ensureBoundLocked();

    std::mutex mLock;
    ::android::sp<hidl_face_v2::ISehBiometricsFace> mSeh;
    ::android::sp<hidl_face_v1::IBiometricsFace> mBase;
    ::android::sp<HidlCallbackBridge> mBridge;
    bool mCallbackRegistered = false;
};

}  // namespace lineageos::samsung::biometrics::face
