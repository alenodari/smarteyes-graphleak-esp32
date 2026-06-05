#pragma once

#include <cstdint>

#include "graphleak_types.h"

struct EwmaCusumConfig {
    float alpha = 0.25f;
    float drift = 0.75f;
    float threshold = 50.0f;
};

class EwmaCusumDetector {
public:
    EwmaCusumDetector(const HourlyRobustStats& stats, EwmaCusumConfig config = {});

    void reset();
    DetectionStep update(float volume, uint8_t hour);

private:
    const HourlyRobustStats& stats_;
    EwmaCusumConfig config_;
    float ewma_ = 0.0f;
    float cusum_ = 0.0f;
};
