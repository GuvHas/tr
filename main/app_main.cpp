#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#include <platform/internal/BLEManager.h>

using namespace esp_matter;

namespace {
constexpr const char *kTag = "xiao-matter";

// Shut down the BLE stack and log any error. Frees ~60-80 KB DRAM and
// releases the shared 2.4 GHz radio arbiter on ESP32-C6 so Thread has
// uncontested radio access. Safe to call from any device-layer event
// context once commissioning state is no longer needed.
void shutdownBLE()
{
    chip::DeviceLayer::Internal::BLEMgr().Shutdown();
    ESP_LOGI(kTag, "BLE stack shut down");
}

void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    (void)arg; // unused; suppress -Wunused-parameter / -Werror in strict builds

    // DeviceEventType is a namespace of integer constants in this CHIP SDK version,
    // not an enum class. Use a namespace alias — a using-declaration is illegal here.
    namespace DevEvt = chip::DeviceLayer::DeviceEventType;

    switch (event->Type) {
    case DevEvt::kCommissioningComplete:
        ESP_LOGI(kTag, "Commissioning complete; joined operational fabric");
        // BLE is no longer needed: the commissioning session has already closed
        // the BLE connection before firing this event.
        shutdownBLE();
        break;

    case DevEvt::kFabricRemoved:
        // Fired when a controller removes this device (e.g. "Remove Device" in HA).
        // Log prominently so decommissioning is visible without a logic analyser.
        ESP_LOGW(kTag, "Fabric removed — device decommissioned; re-commissioning required");
        break;

    case DevEvt::kServerReady:
        // All Matter endpoints and clusters are initialised and accepting commands.
        ESP_LOGI(kTag, "Matter server ready");
        // kCommissioningComplete only fires during the initial commissioning flow.
        // On every subsequent boot the device is already in a fabric, so that event
        // never fires and BLE would stay up permanently, consuming ~60-80 KB DRAM.
        // Shut it down here when we are already commissioned.
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() > 0) {
            shutdownBLE();
        }
        break;

    case DevEvt::kCHIPoBLEAdvertisingChange:
        // CHIPoBLEAdvertisingChange.Result is chip::DeviceLayer::ActivityChange,
        // not ConnectivityChange. kActivity_Started = window opened; otherwise closed.
        ESP_LOGI(kTag, "BLE commissioning window %s",
                 event->CHIPoBLEAdvertisingChange.Result == chip::DeviceLayer::kActivity_Started
                     ? "opened" : "closed");
        break;

    // kThreadConnectivityChange is absent from this CHIP SDK version.
    // Thread attach/detach is instead observable via the OpenThread
    // state-change callback (otSetStateChangedCallback) if needed.

    default:
        break;
    }
}

} // namespace

extern "C" void app_main()
{
    // General-purpose NVS partition: auto-erased on unrecoverable corruption.
    // Holds only non-Matter app state; safe to wipe because all Matter
    // credentials live on the protected nvs_matter partition.
    esp_err_t nvs_err = nvs_flash_init_partition("nvs");
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(kTag, "General NVS unrecoverable (%s), erasing", esp_err_to_name(nvs_err));
        ESP_ERROR_CHECK(nvs_flash_erase_partition("nvs"));
        nvs_err = nvs_flash_init_partition("nvs");
    }
    ESP_ERROR_CHECK(nvs_err);

    // Matter credentials partition: factory data (discriminator, PAKE verifier,
    // attestation material), fabric table (Root CA, NOC, operational keys),
    // ACL entries, and group keys. Never auto-erased — all three CHIP NVS
    // namespaces are routed here so that the NVS recovery path above (erase nvs)
    // can never silently wipe commissioning state.
    esp_err_t nvs_matter_err = nvs_flash_init_partition("nvs_matter");
    if (nvs_matter_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_matter_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(kTag, "Matter NVS unrecoverable (%s) — manual factory reset required",
                 esp_err_to_name(nvs_matter_err));
        abort();
    }
    ESP_ERROR_CHECK(nvs_matter_err);

    node::config_t node_config{};
    node_t *node = node::create(&node_config, nullptr, nullptr);
    if (!node) {
        ESP_LOGE(kTag, "Matter node initialization failed");
        abort();
    }

    // openthread_init_stack() asserts s_platform_config != nullptr, so the
    // platform config MUST be set before esp_matter::start() initialises the
    // Thread stack.  On ESP32-C6 the 802.15.4 radio is built-in (NATIVE mode);
    // no external co-processor or UART host connection is required.
    static const esp_openthread_platform_config_t ot_platform_config = {
        .radio_config = {
            .radio_mode = RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = HOST_CONNECTION_MODE_NONE,
        },
        .port_config = {
            .storage_partition_name = "nvs",
            .netif_queue_size       = 10,
            .task_queue_size        = 10,
        },
    };
    esp_openthread_platform_config_set(&ot_platform_config);

    ESP_LOGI(kTag, "Starting Matter stack (BLE commissioning + Thread FTD)");
    ESP_ERROR_CHECK(start(app_event_cb));

    // app_main's task is no longer needed — the Matter stack owns its own tasks.
    // Deleting here reclaims the ~8 KB default stack and TCB immediately.
    vTaskDelete(nullptr);
}
