#include "shewhart_detector.h"

ShewhartDetector::ShewhartDetector(const HourlyRobustStats& stats, ShewhartConfig config)
    : stats_(stats), config_(config) {}

void ShewhartDetector::reset() {}

DetectionStep ShewhartDetector::update(float volume, uint8_t hour) {
    const uint8_t hour_idx = static_cast<uint8_t>(hour % 24U);
    const float median = stats_.median[hour_idx];
    const float scale = stats_.scale[hour_idx];
    const float z_score = (volume - median) / scale;

    DetectionStep step;
    step.z_score = z_score;
    step.state_primary = z_score;
    step.state_secondary = 0.0f;
    step.score = z_score;
    step.alarm = z_score > config_.threshold;
    return step;
}
