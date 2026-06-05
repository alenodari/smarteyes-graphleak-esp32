#include "graphleak_stats_loader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_log.h"

namespace {

constexpr char kTag[] = "graphleak_stats";
constexpr size_t kMaxLineLength = 128;

struct Row {
    MeterId meter;
    uint8_t hour;
    float median;
    float scale;
};

bool parse_meter(const char* token, MeterId& meter) {
    if (std::strcmp(token, "N2") == 0) {
        meter = MeterId::N2;
        return true;
    }
    if (std::strcmp(token, "N8") == 0) {
        meter = MeterId::N8;
        return true;
    }
    if (std::strcmp(token, "N9") == 0) {
        meter = MeterId::N9;
        return true;
    }
    return false;
}

HourlyRobustStats& stats_for_meter(GraphLeakStatsSet& stats_set, MeterId meter) {
    switch (meter) {
        case MeterId::N2:
            return stats_set.n2;
        case MeterId::N8:
            return stats_set.n8;
        case MeterId::N9:
            return stats_set.n9;
        default:
            return stats_set.n2;
    }
}

bool parse_header(char* line) {
    return std::strcmp(line, "meter,hour,median,scale") == 0;
}

bool parse_row(char* line, Row& row) {
    char* cursor = line;
    char* tokens[4] = {nullptr, nullptr, nullptr, nullptr};

    for (int i = 0; i < 4; ++i) {
        if (cursor == nullptr) {
            return false;
        }
        tokens[i] = cursor;
        char* comma = std::strchr(cursor, ',');
        if (comma != nullptr) {
            *comma = '\0';
            cursor = comma + 1;
        } else {
            cursor = nullptr;
        }
    }

    if (!parse_meter(tokens[0], row.meter)) {
        return false;
    }

    row.hour = static_cast<uint8_t>(std::strtoul(tokens[1], nullptr, 10));
    row.median = std::strtof(tokens[2], nullptr);
    row.scale = std::strtof(tokens[3], nullptr);
    return row.hour < 24;
}

}  // namespace

const HourlyRobustStats& GraphLeakStatsSet::for_meter(MeterId meter) const {
    switch (meter) {
        case MeterId::N2:
            return n2;
        case MeterId::N8:
            return n8;
        case MeterId::N9:
            return n9;
        default:
            return n2;
    }
}

bool load_graphleak_stats_csv(const char* path, GraphLeakStatsSet& stats_set) {
    FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        ESP_LOGE(kTag, "Could not open stats CSV: %s", path);
        return false;
    }

    char line[kMaxLineLength];
    if (std::fgets(line, sizeof(line), file) == nullptr) {
        ESP_LOGE(kTag, "Stats CSV is empty: %s", path);
        std::fclose(file);
        return false;
    }

    line[strcspn(line, "\r\n")] = '\0';
    if (!parse_header(line)) {
        ESP_LOGE(kTag, "Unexpected stats CSV header. Expected meter,hour,median,scale");
        std::fclose(file);
        return false;
    }

    bool seen_n2[24] = {};
    bool seen_n8[24] = {};
    bool seen_n9[24] = {};

    while (std::fgets(line, sizeof(line), file) != nullptr) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }

        Row row{};
        if (!parse_row(line, row)) {
            ESP_LOGE(kTag, "Could not parse stats row.");
            std::fclose(file);
            return false;
        }

        HourlyRobustStats& stats = stats_for_meter(stats_set, row.meter);
        stats.median[row.hour] = row.median;
        stats.scale[row.hour] = row.scale;

        bool* seen = nullptr;
        switch (row.meter) {
            case MeterId::N2:
                seen = seen_n2;
                break;
            case MeterId::N8:
                seen = seen_n8;
                break;
            case MeterId::N9:
                seen = seen_n9;
                break;
            default:
                seen = seen_n2;
                break;
        }
        seen[row.hour] = true;
    }

    std::fclose(file);

    for (int hour = 0; hour < 24; ++hour) {
        if (!seen_n2[hour] || !seen_n8[hour] || !seen_n9[hour]) {
            ESP_LOGE(kTag, "Stats CSV is incomplete. Missing at least one meter/hour pair.");
            return false;
        }
    }

    ESP_LOGI(kTag, "Loaded hourly robust stats from %s", path);
    return true;
}
