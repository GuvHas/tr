# Seed Studio XIAO ESP32-C6 Matter Thread Sensor (ESP-IDF 5.5.2)

This repository contains a complete ESP-IDF project for a **Matter-over-Thread sensor device** targeting the **Seeed Studio XIAO ESP32-C6**.

The implementation uses:
- **ESP-IDF 5.5.2** project layout and build system.
- **ESP-Matter** as an IDF component (no edits to IDF libraries).
- **Bluetooth LE commissioning** (device discoverable during commissioning).
- **Thread networking** (joins an existing Thread network through Matter commissioning).
- **Runtime rename support** through Matter's writable **Node Label** attribute, persisted in NVS.
- Sensor endpoints ready for **Home Assistant Matter integration**:
  - Temperature sensor endpoint.
  - Illuminance (light level) sensor endpoint.

---

## 1) Project structure

- `CMakeLists.txt` – root ESP-IDF project file.
- `sdkconfig.defaults` – defaults for ESP32-C6 + Thread + BLE Matter commissioning.
- `main/idf_component.yml` – pins `espressif/esp_matter` dependency.
- `main/app_main.cpp` – Matter node creation, endpoints, rename persistence.
- `.vscode/settings.json` + `.vscode/extensions.json` – VS Code friendly setup.

---

## 2) Design overview

### Commissioning and networking flow

1. Device boots and starts Matter stack.
2. Device advertises for Matter commissioning over **BLE**.
3. A commissioner (Home Assistant Matter integration using a Thread Border Router) pairs with device.
4. Commissioner provisions operational credentials and Thread dataset.
5. Device joins the **existing Thread network**.

### Renaming support

- Default name is generated from MAC tail (for uniqueness): `XIAO-XXXXXX`.
- The **Matter Node Label** attribute is writable.
- Any update is saved in NVS (`app_cfg/node_label`) and restored on reboot.
- This prevents having multiple devices with the same default name.

### Home Assistant compatibility

- Uses standard Matter sensor device types/clusters.
- Home Assistant can discover:
  - Temperature entity.
  - Illuminance entity.
- Device naming can be adjusted from Matter tooling that writes Node Label.

---

## 3) Build and flash in VS Code

## Prerequisites
- ESP-IDF **v5.5.2** installed and exported.
- VS Code with **Espressif IDF extension**.

## Using VS Code (ESP-IDF extension)
1. Open this folder in VS Code.
2. Run **ESP-IDF: Set Espressif Device Target** -> `esp32c6`.
3. Run **ESP-IDF: Build your project**.
4. Run **ESP-IDF: Flash your project**.
5. Monitor logs with **ESP-IDF: Monitor device**.

## CLI equivalent
```bash
idf.py set-target esp32c6
idf.py build
idf.py -p <PORT> flash monitor
```

---

## 4) Home Assistant integration notes

1. Ensure Home Assistant has Matter + Thread configured (for example via SkyConnect or other Thread Border Router).
2. Put device in commissioning mode (default after boot).
3. Add device in Home Assistant Matter integration.
4. On successful commissioning:
   - Device moves to Thread network.
   - Temperature and illuminance entities appear.

---

## 5) Extending sensor logic

The sample currently initializes measured values statically. Replace this section in `main/app_main.cpp` with your sensor readout logic and update Matter attributes periodically.

Recommended next step:
- Use I2C/SPI/GPIO sensor drivers and call `attribute::update(...)` for each new measurement.

---

## 6) Dependency resolution notes

This project tracks a current `esp_matter` release line (`^1.4.0`) and relies on the ESP-IDF component manager to resolve matching transitive versions (including `esp_insights` and `esp_diagnostics`).

To avoid a GCC 14 + `gnu++2b` incompatibility in transitive connectedhomeip closure-control sources, the project also forces C++17 at the root CMake level.

If CI fails during dependency solving or after changing version constraints, clear resolved artifacts and rebuild:

```bash
rm -rf managed_components build dependencies.lock
idf.py reconfigure
idf.py build
```

If an older lock file pins incompatible versions, removing `dependencies.lock` is required before reconfigure/build.
