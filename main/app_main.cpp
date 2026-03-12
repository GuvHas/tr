#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <platform/internal/BLEManager.h>

using namespace esp_matter;

namespace {
constexpr const char *kTag = "xiao-matter";

void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    (void)arg; // unused; suppress -Wunused-parameter / -Werror in strict builds

    using chip::DeviceLayer::DeviceEventType;
    using chip::DeviceLayer::kConnectivity_Established;

    switch (event->Type) {
    case DeviceEventType::kCommissioningComplete:
        ESP_LOGI(kTag, "Commissioning complete; joined operational fabric");
        // Shut down the BLE stack: frees ~60-80 KB DRAM and releases the shared
        // 2.4 GHz radio arbiter on ESP32-C6 so Thread has uncontested radio access.
        // Safe to call here — the CHIP commissioning session has already closed the
        // BLE connection before firing this event.
        chip::DeviceLayer::Internal::BLEMgr().Shutdown();
        break;

    case DeviceEventType::kFabricRemoved:
        // Fired when a controller removes this device (e.g. "Remove Device" in HA).
        // Log prominently so decommissioning is visible without a logic analyser.
        ESP_LOGW(kTag, "Fabric removed — device decommissioned; re-commissioning required");
        break;

    case DeviceEventType::kServerReady:
        // All Matter endpoints and clusters are initialised and accepting commands.
        ESP_LOGI(kTag, "Matter server ready");
        break;

    case DeviceEventType::kCHIPoBLEAdvertisingChange:
        // Tracks commissioning-window open/close for status indicators or logging.
        ESP_LOGI(kTag, "BLE commissioning window %s",
                 event->CHIPoBLEAdvertisingChange.Result == kConnectivity_Established
                     ? "opened" : "closed");
        break;

    case DeviceEventType::kThreadConnectivityChange:
        // Lets the app react to Thread attach/detach without polling OpenThread APIs.
        if (event->ThreadConnectivityChange.Result == kConnectivity_Established) {
            ESP_LOGI(kTag, "Thread: attached to network");
        } else {
            ESP_LOGW(kTag, "Thread: detached from network");
        }
        break;

    default:
        break;
    }
}

} // namespace

extern "C" void app_main()
{
    // General-purpose NVS partition: safe to auto-erase on corruption since it
    // holds no credentials. Matter data is in the separate nvs_matter partition.
    esp_err_t nvs_err = nvs_flash_init_partition("nvs");
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(kTag, "General NVS unrecoverable (%s), erasing", esp_err_to_name(nvs_err));
        ESP_ERROR_CHECK(nvs_flash_erase_partition("nvs"));
        nvs_err = nvs_flash_init_partition("nvs");
    }
    ESP_ERROR_CHECK(nvs_err);

    // Matter credentials partition: fabric keys, ACL entries, Thread network data.
    // Never auto-erased — if corrupt the device must be factory-reset deliberately
    // rather than silently losing fabric membership.
    esp_err_t nvs_matter_err = nvs_flash_init_partition("nvs_matter");
    if (nvs_matter_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_matter_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(kTag, "Matter NVS unrecoverable (%s) — manual factory reset required",
                 esp_err_to_name(nvs_matter_err));
        abort();
    }
    ESP_ERROR_CHECK(nvs_matter_err);

    node::config_t node_config;
    node_t *node = node::create(&node_config, nullptr, nullptr);
    if (!node) {
        ESP_LOGE(kTag, "Matter node initialization failed");
        abort();
    }

    ESP_LOGI(kTag, "Starting Matter stack (BLE commissioning + Thread FTD)");
    ESP_ERROR_CHECK(start(app_event_cb));

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
