#include <algorithm>
#include <cstring>

#include <app/server/Server.h>
#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_attribute_utils.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_mac.h>

using namespace esp_matter;
using namespace esp_matter::cluster;

namespace {
constexpr const char *kTag = "xiao-matter";
constexpr const char *kNvsNamespace = "app_cfg";
constexpr const char *kNvsNameKey = "node_label";
constexpr size_t kMaxNameLen = 32;
constexpr uint16_t kSensorMinValue = 0;
constexpr uint16_t kSensorMaxValue = 5000;
constexpr uint16_t kSensorTolerance = 100;

char g_node_label[kMaxNameLen + 1] = "Seed-XIAO-Sensor";

void set_default_name_from_mac()
{
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY) == ESP_OK) {
        snprintf(g_node_label, sizeof(g_node_label), "XIAO-%02X%02X%02X", mac[3], mac[4], mac[5]);
    }
}

void load_name_from_nvs()
{
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    size_t required = sizeof(g_node_label);
    if (nvs_get_str(handle, kNvsNameKey, g_node_label, &required) != ESP_OK) {
        nvs_close(handle);
        return;
    }

    g_node_label[kMaxNameLen] = '\0';
    nvs_close(handle);
}

esp_err_t persist_name_to_nvs(const char *name)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(kNvsNamespace, NVS_READWRITE, &handle), kTag, "Failed to open NVS");
    ESP_RETURN_ON_ERROR(nvs_set_str(handle, kNvsNameKey, name), kTag, "Failed to write node label");
    ESP_RETURN_ON_ERROR(nvs_commit(handle), kTag, "Failed to commit node label");
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t app_attribute_update_cb(callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                  uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    if (type != PRE_UPDATE) {
        return ESP_OK;
    }

    if (cluster_id == basic_information::id && attribute_id == basic_information::attribute::node_label::id) {
        const size_t new_size = std::min(static_cast<size_t>(val->val.a.s), kMaxNameLen);
        char new_name[kMaxNameLen + 1] = {};
        memcpy(new_name, val->val.a.b, new_size);
        new_name[new_size] = '\0';

        ESP_LOGI(kTag, "Renaming node to: %s", new_name);
        strncpy(g_node_label, new_name, sizeof(g_node_label));
        g_node_label[kMaxNameLen] = '\0';
        ESP_RETURN_ON_ERROR(persist_name_to_nvs(g_node_label), kTag, "Failed to persist name");
    }

    return ESP_OK;
}

void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    if (event->Type == chip::DeviceLayer::DeviceEventType::kCommissioningComplete) {
        ESP_LOGI(kTag, "Commissioning complete. Device name: %s", g_node_label);
    }
}

} // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());

    set_default_name_from_mac();
    load_name_from_nvs();

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, nullptr);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(kTag, "Matter node initialization failed"));

    endpoint_t *root_endpoint = endpoint::root_node::get(node);
    cluster_t *basic_cluster = cluster::get(root_endpoint, basic_information::id);
    ABORT_APP_ON_FAILURE(basic_cluster != nullptr, ESP_LOGE(kTag, "Missing basic information cluster"));
    esp_matter_attr_val_t node_label_val = esp_matter_char_str(g_node_label, strlen(g_node_label));
    ABORT_APP_ON_FAILURE(attribute::update(endpoint::get_id(root_endpoint), basic_information::id,
                                           basic_information::attribute::node_label::id, &node_label_val) == ESP_OK,
                         ESP_LOGE(kTag, "Could not initialize node label"));

    // Endpoint 1: Temperature Sensor (Matter cluster 0x0402)
    endpoint::temperature_sensor::config_t temp_config;
    temp_config.temperature_measurement.measured_value = 2150; // 21.50C in centi-degrees
    temp_config.temperature_measurement.min_measured_value = -4000;
    temp_config.temperature_measurement.max_measured_value = 12500;
    endpoint_t *temp_ep = endpoint::temperature_sensor::create(node, &temp_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(temp_ep != nullptr, ESP_LOGE(kTag, "Failed to create temperature endpoint"));

    // Endpoint 2: Illuminance Sensor (Matter cluster 0x0400)
    endpoint::illuminance_sensor::config_t light_config;
    light_config.illuminance_measurement.measured_value = 1200;
    light_config.illuminance_measurement.min_measured_value = kSensorMinValue;
    light_config.illuminance_measurement.max_measured_value = kSensorMaxValue;
    light_config.illuminance_measurement.tolerance = kSensorTolerance;
    endpoint_t *light_ep = endpoint::illuminance_sensor::create(node, &light_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(light_ep != nullptr, ESP_LOGE(kTag, "Failed to create illuminance endpoint"));

    ESP_LOGI(kTag, "Node label: %s", g_node_label);
    ESP_LOGI(kTag, "Temperature endpoint id: %d", endpoint::get_id(temp_ep));
    ESP_LOGI(kTag, "Illuminance endpoint id: %d", endpoint::get_id(light_ep));

    ESP_ERROR_CHECK(esp_matter::start(app_event_cb));

    // Keep the task alive. Real sensor updates can be added to this loop.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
