// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/vendor/lineageos/samsung/face/BnFacePreviewTap.h>
#include <aidl/vendor/lineageos/samsung/face/IFacePreviewListener.h>

#include <memory>
#include <mutex>
#include <vector>

namespace lineageos::samsung::biometrics::face {

class HidlCallbackBridge;

/**
 * Vendor-side IFacePreviewTap implementation.  Maintains a list of
 * subscribed listeners and is called from HidlCallbackBridge on each
 * sehOnPreviewFrame delivery to fan the frame out.
 *
 * Listeners are held as shared_ptr; unregister is by SpAIBinder
 * identity comparison.  A DeathRecipient cleans up listeners whose
 * clients have died.
 */
class FacePreviewTap
    : public ::aidl::vendor::lineageos::samsung::face::BnFacePreviewTap {
   public:
    FacePreviewTap();
    ~FacePreviewTap() override;

    ::ndk::ScopedAStatus registerListener(
            const std::shared_ptr<
                    ::aidl::vendor::lineageos::samsung::face::IFacePreviewListener>&
                    listener) override;
    ::ndk::ScopedAStatus unregisterListener(
            const std::shared_ptr<
                    ::aidl::vendor::lineageos::samsung::face::IFacePreviewListener>&
                    listener) override;

    /**
     * Fan-out entry point from HidlCallbackBridge.  Duplicates the
     * source fd once per listener and fires a oneway onFrame on
     * each.  Cheap — binder dups on the wire.
     */
    void dispatchFrame(int sourceFd, int32_t size, int32_t width,
                       int32_t height, int32_t format, int64_t timestampNs);

   private:
    // One per registered listener; lifetime-managed by mListeners.
    // The kernel binder driver holds the pointer, so it must outlive
    // the link.  Freed when the listener is removed (registered →
    // removeByBinder) or when onListenerDied fires for it.
    struct DeathCookie {
        FacePreviewTap* tap;
        ::ndk::SpAIBinder binder;
    };

    static void onListenerDied(void* cookie);
    void removeByBinder(const ::ndk::SpAIBinder& binder);

    std::mutex mLock;
    struct Entry {
        std::shared_ptr<
                ::aidl::vendor::lineageos::samsung::face::IFacePreviewListener>
                listener;
        DeathCookie* cookie;
    };
    std::vector<Entry> mListeners;
    ::ndk::ScopedAIBinder_DeathRecipient mDeathRecipient;
};

}  // namespace lineageos::samsung::biometrics::face
