#pragma once

#include <cstdint>

#include "graphleak_types.h"

struct PageHinkleyConfig {
    float delta = 0.5f;
    float threshold = 20.0f;
};

class PageHinkleyDetector {
public:
    PageHinkleyDetector(const HourlyRobustStats& stats, PageHinkleyConfig config = {});

    void reset();
    DetectionStep update(float volume, uint8_t hour);

private:
    const HourlyRobustStats& stats_;
    PageHinkleyConfig config_;
    float running_mean_ = 0.0f;
    float cumulative_ = 0.0f;
    float cumulative_min_ = 0.0f;
    uint32_t sample_count_ = 0;
};
