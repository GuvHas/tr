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
    if (event->Type == chip::DeviceLayer::DeviceEventType::kCommissioningComplete) {
        ESP_LOGI(kTag, "Matter commissioning complete; device joined operational fabric/thread network");
        // Shut down the BLE stack: frees ~60-80 KB DRAM and releases the shared
        // 2.4 GHz radio arbiter on ESP32-C6 so Thread has uncontested radio access.
        // Safe to call here — the CHIP commissioning session has already closed the
        // BLE connection before firing this event.
        chip::DeviceLayer::Internal::BLEMgr().Shutdown();
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
