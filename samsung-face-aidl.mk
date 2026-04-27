# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

# Product-side bundle for hardware/samsung-face-aidl.
#
# Adopt from a device's device.mk with:
#   $(call inherit-product, hardware/samsung-face-aidl/samsung-face-aidl.mk)
#
# Boards must also do, from BoardConfig.mk:
#   include hardware/samsung-face-aidl/BoardConfig.mk

PRODUCT_PACKAGES += \
    android.hardware.biometrics.face-service.samsung
