#include "graphleak_csv_replay.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "esp_log.h"

namespace {

constexpr char kTag[] = "graphleak_csv";
constexpr size_t kMaxLineLength = 512;

struct CsvColumnMap {
    int scenario_id = -1;
    int source_file = -1;
    int scenario_type = -1;
    int time_s = -1;
    int hour = -1;
    int v_n2 = -1;
    int v_n8 = -1;
    int v_n9 = -1;
    int label = -1;
    int y_n2 = -1;
    int y_n8 = -1;
    int y_n9 = -1;
};

char* next_token(char** cursor) {
    if (*cursor == nullptr) {
        return nullptr;
    }

    char* token = *cursor;
    char* comma = std::strchr(token, ',');
    if (comma != nullptr) {
        *comma = '\0';
        *cursor = comma + 1;
    } else {
        *cursor = nullptr;
    }
    return token;
}

bool parse_header(char* line, CsvColumnMap& map) {
    char* cursor = line;
    int index = 0;

    while (char* token = next_token(&cursor)) {
        if (std::strcmp(token, "scenario_id") == 0) {
            map.scenario_id = index;
        } else if (std::strcmp(token, "source_file") == 0) {
            map.source_file = index;
        } else if (std::strcmp(token, "scenario_type") == 0) {
            map.scenario_type = index;
        } else if (std::strcmp(token, "time_s") == 0) {
            map.time_s = index;
        } else if (std::strcmp(token, "hour") == 0) {
            map.hour = index;
        } else if (std::strcmp(token, "V_N2") == 0) {
            map.v_n2 = index;
        } else if (std::strcmp(token, "V_N8") == 0) {
            map.v_n8 = index;
        } else if (std::strcmp(token, "V_N9") == 0) {
            map.v_n9 = index;
        } else if (std::strcmp(token, "label") == 0) {
            map.label = index;
        } else if (std::strcmp(token, "leak_downstream_N2") == 0) {
            map.y_n2 = index;
        } else if (std::strcmp(token, "leak_downstream_N8") == 0) {
            map.y_n8 = index;
        } else if (std::strcmp(token, "leak_downstream_N9") == 0) {
            map.y_n9 = index;
        }
        ++index;
    }

    return map.scenario_id >= 0 && map.source_file >= 0 && map.scenario_type >= 0 &&
           map.time_s >= 0 && map.hour >= 0 && map.v_n2 >= 0 && map.v_n8 >= 0 &&
           map.v_n9 >= 0 && map.label >= 0 && map.y_n2 >= 0 && map.y_n8 >= 0 &&
           map.y_n9 >= 0;
}

bool parse_row(
    char* line,
    const CsvColumnMap& map,
    ScenarioBuffer& parsed_row,
    uint16_t& parsed_scenario_id)
{
    char* cursor = line;
    int index = 0;
    ScenarioSample sample;
    std::string source_file;
    std::string scenario_type;
    uint8_t label = 0;

    while (char* token = next_token(&cursor)) {
        if (index == map.scenario_id) {
            parsed_scenario_id = static_cast<uint16_t>(std::strtoul(token, nullptr, 10));
        } else if (index == map.source_file) {
            source_file = token;
        } else if (index == map.scenario_type) {
            scenario_type = token;
        } else if (index == map.time_s) {
            sample.time_s = static_cast<uint32_t>(std::strtoul(token, nullptr, 10));
        } else if (index == map.hour) {
            sample.hour = static_cast<uint8_t>(std::strtoul(token, nullptr, 10));
        } else if (index == map.v_n2) {
            sample.v_n2 = std::strtof(token, nullptr);
        } else if (index == map.v_n8) {
            sample.v_n8 = std::strtof(token, nullptr);
        } else if (index == map.v_n9) {
            sample.v_n9 = std::strtof(token, nullptr);
        } else if (index == map.label) {
            label = static_cast<uint8_t>(std::strtoul(token, nullptr, 10));
        } else if (index == map.y_n2) {
            sample.y_n2 = static_cast<uint8_t>(std::strtoul(token, nullptr, 10));
        } else if (index == map.y_n8) {
            sample.y_n8 = static_cast<uint8_t>(std::strtoul(token, nullptr, 10));
        } else if (index == map.y_n9) {
            sample.y_n9 = static_cast<uint8_t>(std::strtoul(token, nullptr, 10));
        }
        ++index;
    }

    parsed_row.scenario_id = parsed_scenario_id;
    parsed_row.source_file = source_file;
    parsed_row.scenario_type = scenario_type;
    parsed_row.last_label = label;
    parsed_row.samples.push_back(sample);
    return true;
}

}  // namespace

bool for_each_graphleak_scenario_csv(const char* path, const ScenarioCallback& callback) {
    FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        ESP_LOGE(kTag, "Could not open dataset CSV: %s", path);
        return false;
    }

    char line[kMaxLineLength];
    if (std::fgets(line, sizeof(line), file) == nullptr) {
        ESP_LOGE(kTag, "Dataset CSV is empty: %s", path);
        std::fclose(file);
        return false;
    }

    line[strcspn(line, "\r\n")] = '\0';
    CsvColumnMap map;
    if (!parse_header(line, map)) {
        ESP_LOGE(kTag, "Dataset CSV header is missing required columns.");
        std::fclose(file);
        return false;
    }

    ScenarioBuffer current;
    bool has_current = false;

    while (std::fgets(line, sizeof(line), file) != nullptr) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }

        ScenarioBuffer parsed_row;
        uint16_t parsed_scenario_id = 0;
        if (!parse_row(line, map, parsed_row, parsed_scenario_id)) {
            ESP_LOGE(kTag, "Failed to parse dataset row.");
            std::fclose(file);
            return false;
        }

        if (!has_current) {
            current = std::move(parsed_row);
            has_current = true;
            continue;
        }

        if (parsed_scenario_id != current.scenario_id) {
            if (!callback(current)) {
                std::fclose(file);
                return false;
            }
            current = std::move(parsed_row);
            continue;
        }

        current.last_label = parsed_row.last_label;
        current.samples.push_back(parsed_row.samples[0]);
    }

    std::fclose(file);

    if (has_current && !callback(current)) {
        return false;
    }

    return true;
}
