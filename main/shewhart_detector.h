#pragma once

#include <cstdint>

#include "graphleak_types.h"

struct ShewhartConfig {
    float threshold = 3.0f;
};

class ShewhartDetector {
public:
    ShewhartDetector(const HourlyRobustStats& stats, ShewhartConfig config = {});

    void reset();
    DetectionStep update(float volume, uint8_t hour);

private:
    const HourlyRobustStats& stats_;
    ShewhartConfig config_;
};
