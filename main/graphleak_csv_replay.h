#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "graphleak_types.h"

struct ScenarioSample {
    uint32_t time_s = 0;
    uint8_t hour = 0;
    float v_n2 = 0.0f;
    float v_n8 = 0.0f;
    float v_n9 = 0.0f;
    uint8_t y_n2 = 0;
    uint8_t y_n8 = 0;
    uint8_t y_n9 = 0;
};

struct ScenarioBuffer {
    uint16_t scenario_id = 0;
    std::string source_file;
    std::string scenario_type;
    uint8_t last_label = 0;
    std::vector<ScenarioSample> samples;
};

using ScenarioCallback = std::function<bool(const ScenarioBuffer&)>;

bool for_each_graphleak_scenario_csv(const char* path, const ScenarioCallback& callback);
