# Seed Studio XIAO ESP32-C6 Matter Thread FTD Starter (ESP-IDF 5.5.3)

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
- `partitions.csv` – custom partition table: two NVS partitions (`nvs` 24 KB general state, `nvs_matter` 24 KB Matter/Thread credentials), `otadata` removed (no OTA slots), 3 MB `factory` app partition.
- `.vscode/settings.json` + `.vscode/extensions.json` – VS Code helper config.

---

## 2) Runtime design

1. Initialize two NVS partitions: `nvs` (auto-erasable on corruption) and `nvs_matter` (Matter/Thread credentials — never auto-erased; `abort()` on corruption to force deliberate factory reset).
2. Create a minimal Matter node.
3. Start Matter stack.
4. Device advertises for BLE commissioning and receives Thread credentials from commissioner.
5. Device joins Thread network as commissioned Matter node.

---

## 3) Build and flash

### Prerequisites
- ESP-IDF **v5.5.3+** exported.
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


If you see `app partition is too small for binary`, this repo already uses a custom `partitions.csv` with a larger `factory` app partition (3 MB).

`CONFIG_OPENTHREAD_NVS_PERSIST=y` is set in `sdkconfig.defaults` to persist Thread network credentials (network key, PAN ID, channel) across reboots. Without it the device must be re-commissioned after every power cycle.

Matter fabric credentials, Thread network data, and ACL entries are stored in the dedicated `nvs_matter` partition (routed via `CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL`, `CONFIG_CHIP_CONFIG_NAMESPACE_PARTITION_LABEL`, and `CONFIG_ESP_MATTER_NVS_PART_NAME`). General app state uses the separate `nvs` partition which is auto-erased on corruption. If `nvs_matter` becomes unreadable the firmware calls `abort()` — reflash and recommission deliberately rather than silently losing fabric membership.

If CI reports partition generation with `--flash-size 2MB`, this project expects **4MB flash** (`CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` in `sdkconfig.defaults`) to match the custom partition table.

If dependency/build state gets stale in CI:

```bash
rm -rf managed_components build dependencies.lock
idf.py reconfigure
idf.py build
```
