#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/DeviceInstanceInfoProvider.h>
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

// Stuff numBits bits of val (LSB first) into buf starting at bit offset bitOff.
static void stuffBits(uint8_t *buf, int bitOff, uint32_t val, int numBits)
{
    for (int i = 0; i < numBits; ++i) {
        if ((val >> i) & 1u)
            buf[(bitOff + i) / 8] |= (uint8_t)(1u << ((bitOff + i) % 8));
    }
}

// Base38-encode src (srcLen bytes) into dst.
// 3-byte chunks → 5 chars; a trailing 2-byte chunk → 3 chars.
static constexpr char kB38[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.";
static size_t base38Encode(const uint8_t *src, size_t srcLen, char *dst, size_t dstCap)
{
    size_t di = 0;
    for (size_t si = 0; si < srcLen;) {
        size_t nb = (si + 3 <= srcLen) ? 3u : (srcLen - si);
        size_t nc = (nb == 3) ? 5u : 3u;
        uint32_t v = 0;
        for (size_t k = 0; k < nb; ++k) v |= (uint32_t)src[si + k] << (8 * k);
        si += nb;
        for (size_t k = 0; k < nc && di < dstCap; ++k, v /= 38) dst[di++] = kB38[v % 38];
    }
    return di;
}

// Verhoeff10 checksum — same algorithm as CHIP SDK Verhoeff10::ComputeCheckChar().
// Iterates the digit string right-to-left; uses the standard dihedral-group-5
// multiply table, the permutation table, and the inverse table.
static const uint8_t kVD[10][10] = {
    {0,1,2,3,4,5,6,7,8,9},
    {1,2,3,4,0,6,7,8,9,5},
    {2,3,4,0,1,7,8,9,5,6},
    {3,4,0,1,2,8,9,5,6,7},
    {4,0,1,2,3,9,5,6,7,8},
    {5,9,8,7,6,0,4,3,2,1},
    {6,5,9,8,7,1,0,4,3,2},
    {7,6,5,9,8,2,1,0,4,3},
    {8,7,6,5,9,3,2,1,0,4},
    {9,8,7,6,5,4,3,2,1,0},
};
static const uint8_t kVP[8][10] = {
    {0,1,2,3,4,5,6,7,8,9},
    {1,5,7,6,2,8,3,0,9,4},
    {5,8,0,3,7,9,6,1,4,2},
    {8,9,1,6,0,4,3,5,2,7},
    {9,4,5,3,1,2,6,8,7,0},
    {4,2,8,6,5,7,3,9,0,1},
    {2,7,9,3,8,0,6,4,1,5},
    {7,0,4,6,9,1,3,2,5,8},
};
static const uint8_t kVInv[10] = {0,4,3,2,1,5,6,7,8,9};

static uint32_t verhoeff10Check(const char *s, size_t len)
{
    int c = 0;
    for (size_t i = len; i > 0; --i) {
        int p = kVP[(len - i + 1) % 8][(uint8_t)(s[i - 1] - '0')];
        c = kVD[c][p];
    }
    return kVInv[c];
}

// Print the commissioning QR code (MT:...) and raw discriminator/passcode.
// The QR code encodes the 88-bit Matter setup payload (spec §5.1.3) and can
// be scanned directly with Apple Home, Google Home, or any Matter controller.
void printCommissioningCodes()
{
    uint32_t passcode      = 0;
    uint16_t discriminator = 0, vid = 0, pid = 0;

    auto *cdp = chip::DeviceLayer::GetCommissionableDataProvider();
    if (!cdp ||
        cdp->GetSetupPasscode(passcode)      != CHIP_NO_ERROR ||
        cdp->GetSetupDiscriminator(discriminator) != CHIP_NO_ERROR) {
        ESP_LOGE(kTag, "Cannot read setup payload from NVS");
        return;
    }
    // VID/PID default to 0 for test/development builds — ignore errors.
    auto *diip = chip::DeviceLayer::GetDeviceInstanceInfoProvider();
    if (diip) {
        diip->GetVendorId(vid);
        diip->GetProductId(pid);
    }

    // 88-bit QR payload layout (Matter spec §5.1.3.1):
    //   version(3) VID(16) PID(16) flow(2) rendezvous(8) discriminator(12) passcode(27) pad(4)
    uint8_t payload[11] = {};
    stuffBits(payload,  0, 0,             3);  // version = 0
    stuffBits(payload,  3, vid,          16);
    stuffBits(payload, 19, pid,          16);
    stuffBits(payload, 35, 0,             2);  // standard commissioning flow
    stuffBits(payload, 37, 0x02,          8);  // BLE rendezvous flag
    stuffBits(payload, 45, discriminator, 12);
    stuffBits(payload, 57, passcode,      27);
    // bits [84:87]: padding = 0 (already zero-initialised)

    // 11 bytes → 18 base38 chars; "MT:" prefix → 21-char QR string
    char qr[24] = "MT:";
    size_t n = base38Encode(payload, sizeof(payload), qr + 3, sizeof(qr) - 4);
    qr[3 + n] = '\0';

    ESP_LOGI(kTag, "SetupQRCode: [%s]", qr);

    // 11-digit manual pairing code (Matter spec §5.1.4.1, standard flow, no VID/PID).
    // Uses the short discriminator (top 4 bits of the 12-bit long discriminator).
    // Layout: chunk1(1) chunk2(5) chunk3(4) checkDigit(1)
    uint8_t  sd = (uint8_t)((discriminator >> 8) & 0xF); // short discriminator (top 4 bits)
    uint32_t c1 = (uint32_t)(sd >> 2);                               // top 2 bits of short disc
    uint32_t c2 = ((uint32_t)(sd & 0x3) << 14) | (passcode & 0x3FFFu); // low 2 bits of sd + lower 14 bits of passcode
    uint32_t c3 = passcode >> 14;                                        // upper 13 bits of passcode

    // Buffer is 16 bytes; actual output is always exactly 10 digits (c1≤3, c2≤65535, c3≤8191),
    // but the wider buffer silences -Werror=format-truncation which can't prove the bound.
    char tenDigits[16];
    snprintf(tenDigits, sizeof(tenDigits), "%01u%05u%04u", (unsigned)c1, (unsigned)c2, (unsigned)c3);

    // Verhoeff10 check digit — matches CHIP SDK Verhoeff10::ComputeCheckChar()
    uint32_t checkDigit = verhoeff10Check(tenDigits, 10);

    ESP_LOGI(kTag, "ManualPairingCode: [%s%u]", tenDigits, (unsigned)checkDigit);
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

    case DevEvt::kCHIPoBLEAdvertisingChange:
        if (event->CHIPoBLEAdvertisingChange.Result == chip::DeviceLayer::kActivity_Started) {
            ESP_LOGI(kTag, "BLE commissioning window opened");
            // Print codes the first time advertising starts so the user has them
            // immediately when the phone discovers the device.
            printCommissioningCodes();
        } else {
            ESP_LOGI(kTag, "BLE commissioning window closed");
        }
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
    static esp_openthread_platform_config_t ot_platform_config = {
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
    ESP_ERROR_CHECK(set_openthread_platform_config(&ot_platform_config));

    ESP_LOGI(kTag, "Starting Matter stack (BLE commissioning + Thread FTD)");
    ESP_ERROR_CHECK(start(app_event_cb));

    // app_main's task is no longer needed — the Matter stack owns its own tasks.
    // Deleting here reclaims the ~8 KB default stack and TCB immediately.
    vTaskDelete(nullptr);
}
