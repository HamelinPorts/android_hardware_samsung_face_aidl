// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#include "FacePreviewTap.h"
#include "HidlBackend.h"
#include "HidlCallbackBridge.h"
#include "SamsungFace.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <hidl/HidlTransportSupport.h>

#include <thread>

namespace lnsf = ::lineageos::samsung::biometrics::face;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(2);

    // The process also acts as a HIDL CLIENT to the Samsung proprietary
    // face HAL (ISehBiometricsFace@2.0).  Incoming HIDL callbacks
    // (onEnrollResult, sehOnPreviewFrame, …) arrive on hwbinder and
    // require at least one HIDL worker thread to be parked in
    // joinRpcThreadpool() to dispatch them.  AIDL's
    // ABinderProcess_joinThreadPool handles /dev/binder only.  Start
    // the hwbinder pool in a background thread so the main thread
    // can block on the AIDL pool below.
    ::android::hardware::configureRpcThreadpool(1, false);
    std::thread([]() {
        ::android::hardware::joinRpcThreadpool();
    }).detach();

    // Main AIDL IFace service.
    auto face = ndk::SharedRefBase::make<lnsf::SamsungFace>();
    const std::string faceInstance =
            std::string(aidl::android::hardware::biometrics::face::IFace::descriptor) +
            "/default";
    CHECK_EQ(AServiceManager_addService(face->asBinder().get(),
                                        faceInstance.c_str()),
             STATUS_OK)
            << "failed to register " << faceInstance;

    // Vendor-side IFacePreviewTap side-channel.  Wired to the same
    // HidlCallbackBridge SamsungFace uses, so there's exactly one
    // sehSetCallback registered with the Samsung HIDL HAL.
    auto tap = ndk::SharedRefBase::make<lnsf::FacePreviewTap>();
    const std::string tapInstance =
            std::string(
                    aidl::vendor::lineageos::samsung::face::IFacePreviewTap::descriptor) +
            "/default";
    CHECK_EQ(AServiceManager_addService(tap->asBinder().get(),
                                        tapInstance.c_str()),
             STATUS_OK)
            << "failed to register " << tapInstance;

    // Hand the tap to the HIDL bridge so sehOnPreviewFrame fans out.
    face->backend()->attachPreviewTap(tap);

    LOG(INFO) << "Samsung face HAL AIDL service ready";
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
