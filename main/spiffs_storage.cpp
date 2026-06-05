#include "spiffs_storage.h"

#include "esp_log.h"
#include "esp_spiffs.h"

namespace {

constexpr char kTag[] = "spiffs";
constexpr char kBasePath[] = "/spiffs";
constexpr char kPartitionLabel[] = "storage";

}  // namespace

bool init_spiffs_storage() {
    static bool mounted = false;
    if (mounted) {
        return true;
    }

    esp_vfs_spiffs_conf_t conf{};
    conf.base_path = kBasePath;
    conf.partition_label = kPartitionLabel;
    conf.max_files = 4;
    conf.format_if_mount_failed = true;

    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to mount SPIFFS: %s", esp_err_to_name(err));
        return false;
    }

    size_t total = 0;
    size_t used = 0;
    if (esp_spiffs_info(kPartitionLabel, &total, &used) == ESP_OK) {
        ESP_LOGI(kTag, "SPIFFS mounted at %s total=%u used=%u",
                 kBasePath,
                 static_cast<unsigned>(total),
                 static_cast<unsigned>(used));
    }

    mounted = true;
    return true;
}
