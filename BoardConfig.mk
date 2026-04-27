# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

# Board-side bundle for hardware/samsung-face-aidl.
#
# Include from a device's BoardConfig.mk with:
#   include hardware/samsung-face-aidl/BoardConfig.mk

BOARD_VENDOR_SEPOLICY_DIRS += \
    hardware/samsung-face-aidl/biometrics/face/aidl/sepolicy
