// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/biometrics/common/BnCancellationSignal.h>

#include <functional>

namespace lineageos::samsung::biometrics::face {

/**
 * AIDL ICancellationSignal backed by a caller-supplied cancel lambda.
 * The lambda typically forwards to the HIDL HAL's `cancel()` method.
 */
class CancellationSignal : public ::aidl::android::hardware::biometrics::
                                   common::BnCancellationSignal {
   public:
    explicit CancellationSignal(std::function<void()> onCancel)
        : mOnCancel(std::move(onCancel)) {}

    ::ndk::ScopedAStatus cancel() override {
        if (mOnCancel) {
            auto fn = std::move(mOnCancel);
            fn();
        }
        return ::ndk::ScopedAStatus::ok();
    }

   private:
    std::function<void()> mOnCancel;
};

}  // namespace lineageos::samsung::biometrics::face
