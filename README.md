# android_hardware_samsung_face_aidl

AIDL face biometrics HAL for Samsung devices that ship the proprietary
`vendor.samsung.hardware.biometrics.face@2.0` HIDL service.

## Contents

- `interfaces/biometrics/face/2.0/` — vendor HIDL interface
  (`vendor.samsung.hardware.biometrics.face@2.0`) covering the
  ISehBiometricsFace + ISehBiometricsFaceClientCallback extensions
  Samsung adds on top of `android.hardware.biometrics.face@1.0`.
  Anchored under a `hidl_package_root vendor.samsung.hardware.biometrics`
  declared in `interfaces/biometrics/Android.bp` so it sits beside —
  rather than overlapping with — the broader `vendor.samsung.hardware`
  root that LineageOS/android_hardware_samsung uses for radio @1.2.
- `biometrics/face/aidl/` — `android.hardware.biometrics.face-service.samsung`,
  an AIDL HAL service that wraps the HIDL @2.0 service and exposes
  `android.hardware.biometrics.face.IFace/default`.  Includes a
  `vendor.lineageos.samsung.face.IFacePreviewTap` AIDL service for
  fanning live camera preview frames out to a privileged UI client.
- `biometrics/face/client/` — `SamsungFacePreviewClient`, a thin Java
  library (`FacePreviewView`) that priv-apps drop into their face
  enroll layout to render the preview frames.

## Adopt

In your device's `device.mk`:

```
$(call inherit-product, hardware/samsung-face-aidl/samsung-face-aidl.mk)
```

In your device's `BoardConfig.mk`:

```
include hardware/samsung-face-aidl/BoardConfig.mk
```

Plus a `lineage.dependencies` entry pointing at this repo.

The HIDL @2.0 service must already be registered as
`vendor.samsung.hardware.biometrics.face@2.0::ISehBiometricsFace/default`
by some other vendor service on your device.

This repo declares no `soong_namespace`, so its modules live in the
default namespace and are visible to every device tree without any
explicit import.

## Sensor strength

The HAL registers the sensor as `BIOMETRIC_CONVENIENCE` (face unlock
for keyguard dismissal only; never reaches keystore-backed crypto).
Several code paths assume this strength — see the `static_assert` in
`HidlCallbackBridge::onAuthenticated` before raising it.
