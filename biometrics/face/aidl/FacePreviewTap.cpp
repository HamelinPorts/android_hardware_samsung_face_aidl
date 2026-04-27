// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "FacePreviewTap"

#include "FacePreviewTap.h"

#include <android-base/logging.h>
#include <android/binder_auto_utils.h>

#include <unistd.h>

namespace lineageos::samsung::biometrics::face {

using ::aidl::vendor::lineageos::samsung::face::IFacePreviewListener;

FacePreviewTap::FacePreviewTap()
    : mDeathRecipient(AIBinder_DeathRecipient_new(&FacePreviewTap::onListenerDied)) {}

FacePreviewTap::~FacePreviewTap() = default;

::ndk::ScopedAStatus FacePreviewTap::registerListener(
        const std::shared_ptr<IFacePreviewListener>& listener) {
    if (listener == nullptr) {
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    ::ndk::SpAIBinder binder = listener->asBinder();
    std::lock_guard<std::mutex> lk(mLock);
    for (const auto& e : mListeners) {
        if (e.listener->asBinder().get() == binder.get()) {
            return ::ndk::ScopedAStatus::ok();  // already registered
        }
    }
    auto* cookie = new DeathCookie{this, binder};
    AIBinder_linkToDeath(binder.get(), mDeathRecipient.get(), cookie);
    mListeners.push_back({listener, cookie});
    LOG(INFO) << "preview listener registered (n=" << mListeners.size() << ")";
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus FacePreviewTap::unregisterListener(
        const std::shared_ptr<IFacePreviewListener>& listener) {
    if (listener == nullptr) {
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    removeByBinder(listener->asBinder());
    return ::ndk::ScopedAStatus::ok();
}

void FacePreviewTap::removeByBinder(const ::ndk::SpAIBinder& binder) {
    std::lock_guard<std::mutex> lk(mLock);
    auto it = mListeners.begin();
    while (it != mListeners.end()) {
        if (it->listener->asBinder().get() == binder.get()) {
            AIBinder_unlinkToDeath(binder.get(), mDeathRecipient.get(),
                                   it->cookie);
            delete it->cookie;
            it = mListeners.erase(it);
        } else {
            ++it;
        }
    }
}

void FacePreviewTap::onListenerDied(void* cookie) {
    auto* c = static_cast<DeathCookie*>(cookie);
    if (c == nullptr || c->tap == nullptr) return;
    LOG(INFO) << "preview listener binder died — removing";
    c->tap->removeByBinder(c->binder);
}

void FacePreviewTap::dispatchFrame(int sourceFd, int32_t size, int32_t width,
                                   int32_t height, int32_t format,
                                   int64_t timestampNs) {
    std::vector<std::shared_ptr<IFacePreviewListener>> snapshot;
    {
        std::lock_guard<std::mutex> lk(mLock);
        snapshot.reserve(mListeners.size());
        for (const auto& e : mListeners) snapshot.push_back(e.listener);
    }
    if (snapshot.empty()) return;
    std::vector<::ndk::SpAIBinder> dead;
    for (const auto& l : snapshot) {
        int dupFd = dup(sourceFd);
        if (dupFd < 0) {
            PLOG(WARNING) << "dup for preview frame";
            continue;
        }
        ::ndk::ScopedFileDescriptor sfd(dupFd);
        auto st = l->onFrame(sfd, size, width, height, format, timestampNs);
        if (!st.isOk()) {
            LOG(WARNING) << "onFrame transport error: " << st.getDescription()
                         << " — scheduling listener removal";
            dead.push_back(l->asBinder());
        }
    }
    for (const auto& b : dead) removeByBinder(b);
}

}  // namespace lineageos::samsung::biometrics::face
