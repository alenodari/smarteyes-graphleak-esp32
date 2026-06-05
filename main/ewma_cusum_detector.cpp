#include "ewma_cusum_detector.h"

#include <algorithm>

EwmaCusumDetector::EwmaCusumDetector(const HourlyRobustStats& stats, EwmaCusumConfig config)
    : stats_(stats), config_(config) {}

void EwmaCusumDetector::reset() {
    ewma_ = 0.0f;
    cusum_ = 0.0f;
}

DetectionStep EwmaCusumDetector::update(float volume, uint8_t hour) {
    const uint8_t hour_idx = static_cast<uint8_t>(hour % 24U);
    const float median = stats_.median[hour_idx];
    const float scale = stats_.scale[hour_idx];

    const float z_score = (volume - median) / scale;
    ewma_ = config_.alpha * z_score + (1.0f - config_.alpha) * ewma_;
    cusum_ = std::max(0.0f, cusum_ + ewma_ - config_.drift);

    DetectionStep step;
    step.z_score = z_score;
    step.ewma = ewma_;
    step.cusum = cusum_;
    step.alarm = cusum_ > config_.threshold;
    return step;
}
