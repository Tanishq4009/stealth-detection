# Stealth Intrusion Detection using TinyML on ESP32

An inexpensive intrusion detector that listens for quiet, careful movement. A TinyML sound classifier runs on the ESP32, and only after it confirms an intrusion does the device switch on the camera, upload one photo and send a push alert to a Flutter app.

**Tanishq Yadav (24U020070)** · B.Tech CSE, IIIT Bhopal · Supervisor: Dr. Priyankar Choudhary

## How it works

MAX9814 mic → ESP32 → Edge Impulse model (MFE + 1D CNN) → confirmation (score > 0.70, 3 hits) → OV2640 photo → Cloudinary → OneSignal → Flutter app

No audio leaves the device.

## Results

- Validation accuracy: 93.8% (int8)
- Test accuracy: 90.09% (float32), AUC 0.91
- Inference: about 1 ms, 10.8 kB RAM, 40.0 kB flash

## Structure

- `firmware/` Arduino sketch for the ESP32
- `mobile-app/` Flutter app
- `docs/` presentation and report

## Setup

1. **Firmware:** install the Edge Impulse Arduino library, copy `secrets.example.h` to `secrets.h`, fill in your Wi-Fi, Cloudinary and OneSignal values, then upload to an ESP32 Wrover (Huge APP partition, PSRAM on).
2. **App:** copy `.env.example` to `.env`, set your OneSignal App ID, then run `flutter pub get` and `flutter run`.

