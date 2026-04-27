// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "SamsungFace"

#include "SamsungFace.h"

#include "HidlBackend.h"
#include "SamsungSession.h"

#include <android-base/logging.h>

namespace lineageos::samsung::biometrics::face {

using ::aidl::android::hardware::biometrics::common::ComponentInfo;
using ::aidl::android::hardware::biometrics::common::SensorStrength;
using ::aidl::android::hardware::biometrics::face::FaceSensorType;
using ::aidl::android::hardware::biometrics::face::ISession;
using ::aidl::android::hardware::biometrics::face::ISessionCallback;
using ::aidl::android::hardware::biometrics::face::SensorProps;

namespace {
// Matches the legacy `config_biometric_sensors` entry on this device
// (id=0, modality=face, strength=BIOMETRIC_CONVENIENCE).  Future
// Samsung devices that want stronger biometrics can override this via
// a per-device override header; for now the constant is fine.
constexpr int32_t kSensorId = 0;
constexpr const char* kHwVersion = "samsung-hidl-v2.0";
constexpr const char* kFwVersion = "arcsoft-adapter";
constexpr const char* kSerialNumber = "";
constexpr const char* kSwVersion = "lineageos-1";
}  // namespace

SamsungFace::SamsungFace() : mBackend(std::make_shared<HidlBackend>()) {}

SamsungFace::~SamsungFace() = default;

::ndk::ScopedAStatus SamsungFace::getSensorProps(
        std::vector<SensorProps>* out) {
    SensorProps p{};
    p.commonProps.sensorId = kSensorId;
    p.commonProps.sensorStrength = kSensorStrength;
    p.commonProps.maxEnrollmentsPerUser = 1;
    p.commonProps.componentInfo = {
            {
                /* componentId */ "faceSensor",
                /* hardwareVersion */ kHwVersion,
                /* firmwareVersion */ kFwVersion,
                /* serialNumber */ kSerialNumber,
                /* softwareVersion */ "",
            },
            {
                /* componentId */ "matchingAlgorithm",
                /* hardwareVersion */ "",
                /* firmwareVersion */ "",
                /* serialNumber */ "",
                /* softwareVersion */ kSwVersion,
            },
    };
    p.sensorType = FaceSensorType::RGB;
    p.halControlsPreview = true;  // we own the preview pipeline
    p.enrollPreviewWidth = 640;
    p.enrollPreviewHeight = 480;
    p.enrollTranslationX = 0.f;
    p.enrollTranslationY = 0.f;
    p.enrollPreviewScale = 1.f;
    out->push_back(std::move(p));
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SamsungFace::createSession(
        int32_t /* sensorId */, int32_t userId,
        const std::shared_ptr<ISessionCallback>& cb,
        std::shared_ptr<ISession>* out) {
    *out = ::ndk::SharedRefBase::make<SamsungSession>(userId, cb, mBackend);
    return ::ndk::ScopedAStatus::ok();
}

}  // namespace lineageos::samsung::biometrics::face
