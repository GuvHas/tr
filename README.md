# Seed Studio XIAO ESP32-C6 Matter Thread FTD Starter (ESP-IDF 5.5.2)

This repository contains a minimal ESP-IDF project for a **Matter-over-Thread Full Thread Device (FTD)** targeting the **Seeed Studio XIAO ESP32-C6**.

The current focus is to provide a stable, compile-friendly baseline that:
- boots Matter,
- is discoverable for BLE commissioning,
- joins an existing Thread network during commissioning,
- and runs cleanly with current ESP-IDF/esp_matter toolchains.

> Sensor endpoints and custom cluster logic were intentionally removed in this revision to prioritize successful build/runtime of the FTD device baseline.

---

## 1) Project structure

- `CMakeLists.txt` – root ESP-IDF project file + C++ standard compatibility handling.
- `sdkconfig.defaults` – defaults for ESP32-C6 + OpenThread FTD + BLE commissioning.
- `main/idf_component.yml` – `espressif/esp_matter` dependency.
- `main/app_main.cpp` – minimal Matter node bootstrap and stack start.
- `.vscode/settings.json` + `.vscode/extensions.json` – VS Code helper config.

---

## 2) Runtime design

1. Initialize NVS (with recoverable erase/retry flow).
2. Create a minimal Matter node.
3. Start Matter stack.
4. Device advertises for BLE commissioning and receives Thread credentials from commissioner.
5. Device joins Thread network as commissioned Matter node.

---

## 3) Build and flash

### Prerequisites
- ESP-IDF **v5.5.2+** exported.
- VS Code + Espressif extension, or CLI `idf.py`.

### CLI
```bash
idf.py set-target esp32c6
idf.py reconfigure
idf.py build
idf.py -p <PORT> flash monitor
```

---

## 4) Home Assistant commissioning

1. Ensure HA Matter + Thread are configured.
2. Add new Matter device in Home Assistant.
3. Commission over BLE (QR/manual code as provided by your onboarding flow).
4. HA provisions Thread credentials; device joins Thread network.

---

## 5) Dependency / toolchain note

This project tracks `espressif/esp_matter:^1.4.0`.

For GCC14-based environments where `esp_matter` may inject `-std=gnu++2b` for transitive connectedhomeip sources, root `CMakeLists.txt` forces and normalizes `esp_matter` target compile standard back to `gnu++17`.


If you see a linker error like `undefined reference to mbedtls_hkdf`, ensure `CONFIG_MBEDTLS_HKDF_C=y` is present (it is enabled in `sdkconfig.defaults` in this repo).

If dependency/build state gets stale in CI:

```bash
rm -rf managed_components build dependencies.lock
idf.py reconfigure
idf.py build
```
