// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/biometrics/common/SensorStrength.h>
#include <aidl/android/hardware/biometrics/face/BnFace.h>

#include <memory>

namespace lineageos::samsung::biometrics::face {

class HidlBackend;

// Sensor strength reported via getSensorProps.  Defined here so any
// code that reasons about the sensor's strength (e.g. the auth-token
// handling in HidlCallbackBridge) can reference this constant rather
// than hard-coding CONVENIENCE in two places.
inline constexpr ::aidl::android::hardware::biometrics::common::SensorStrength
        kSensorStrength = ::aidl::android::hardware::biometrics::common::
                SensorStrength::CONVENIENCE;

/**
 * AIDL IFace implementation for Samsung devices with the proprietary
 * ISehBiometricsFace@2.0 HIDL HAL.  Reports a single
 * BIOMETRIC_CONVENIENCE-strength face sensor and opens one
 * SamsungSession per framework client.
 */
class SamsungFace : public aidl::android::hardware::biometrics::face::BnFace {
   public:
    SamsungFace();
    ~SamsungFace() override;

    ::ndk::ScopedAStatus getSensorProps(
            std::vector<aidl::android::hardware::biometrics::face::SensorProps>*
                    out) override;

    ::ndk::ScopedAStatus createSession(
            int32_t sensorId, int32_t userId,
            const std::shared_ptr<
                    aidl::android::hardware::biometrics::face::ISessionCallback>&
                    cb,
            std::shared_ptr<aidl::android::hardware::biometrics::face::ISession>*
                    out) override;

    /** Access to the HIDL backend for main.cpp's startup wiring. */
    std::shared_ptr<HidlBackend> backend() { return mBackend; }

   private:
    std::shared_ptr<HidlBackend> mBackend;
};

}  // namespace lineageos::samsung::biometrics::face
