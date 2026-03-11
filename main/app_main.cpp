#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <nvs_flash.h>

using namespace esp_matter;

namespace {
constexpr const char *kTag = "xiao-matter";

void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    if (event->Type == chip::DeviceLayer::DeviceEventType::kCommissioningComplete) {
        ESP_LOGI(kTag, "Matter commissioning complete; device joined operational fabric/thread network");
    }
}

} // namespace

extern "C" void app_main()
{
    esp_err_t nvs_init_err = nvs_flash_init();
    if (nvs_init_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_init_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "NVS init returned %s, erasing NVS and retrying", esp_err_to_name(nvs_init_err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_init_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_init_err);

    node::config_t node_config;
    node_t *node = node::create(&node_config, nullptr, nullptr);
    if (!node) {
        ESP_LOGE(kTag, "Matter node initialization failed");
        abort();
    }

    ESP_LOGI(kTag, "Starting Matter stack (BLE commissioning + Thread FTD)");
    ESP_ERROR_CHECK(esp_matter::start(app_event_cb));

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
