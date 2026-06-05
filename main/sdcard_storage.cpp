#include "sdcard_storage.h"

#include <cstdio>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace {

constexpr char kTag[] = "sdcard";
constexpr char kMountPoint[] = "/sdcard";

constexpr gpio_num_t kSdmmcPinClk = GPIO_NUM_39;
constexpr gpio_num_t kSdmmcPinCmd = GPIO_NUM_38;
constexpr gpio_num_t kSdmmcPinD0 = GPIO_NUM_40;
sdmmc_card_t* g_card = nullptr;

bool mount_sdmmc() {
    ESP_LOGI(kTag, "Mounting SD card via SDMMC on CLK=%d CMD=%d D0=%d",
             static_cast<int>(kSdmmcPinClk),
             static_cast<int>(kSdmmcPinCmd),
             static_cast<int>(kSdmmcPinD0));

    esp_vfs_fat_sdmmc_mount_config_t mount_config{};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 4;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = kSdmmcPinClk;
    slot_config.cmd = kSdmmcPinCmd;
    slot_config.d0 = kSdmmcPinD0;
    slot_config.d1 = GPIO_NUM_NC;
    slot_config.d2 = GPIO_NUM_NC;
    slot_config.d3 = GPIO_NUM_NC;
    slot_config.d4 = GPIO_NUM_NC;
    slot_config.d5 = GPIO_NUM_NC;
    slot_config.d6 = GPIO_NUM_NC;
    slot_config.d7 = GPIO_NUM_NC;
    slot_config.width = 1;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_err_t mount_err = esp_vfs_fat_sdmmc_mount(
        kMountPoint,
        &host,
        &slot_config,
        &mount_config,
        &g_card);

    if (mount_err != ESP_OK) {
        ESP_LOGE(kTag, "SDMMC mount failed: %s", esp_err_to_name(mount_err));
        return false;
    }

    ESP_LOGI(kTag, "SD card mounted at %s via SDMMC", kMountPoint);
    sdmmc_card_print_info(stdout, g_card);
    return true;
}

}  // namespace

bool init_sdcard_storage() {
    static bool mounted = false;
    if (mounted) {
        return true;
    }
    if (!mount_sdmmc()) {
        return false;
    }
    mounted = true;
    return true;
}
