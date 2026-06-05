#include "page_hinkley_detector.h"

#include <algorithm>

PageHinkleyDetector::PageHinkleyDetector(const HourlyRobustStats& stats, PageHinkleyConfig config)
    : stats_(stats), config_(config) {}

void PageHinkleyDetector::reset() {
    running_mean_ = 0.0f;
    cumulative_ = 0.0f;
    cumulative_min_ = 0.0f;
    sample_count_ = 0;
}

DetectionStep PageHinkleyDetector::update(float volume, uint8_t hour) {
    const uint8_t hour_idx = static_cast<uint8_t>(hour % 24U);
    const float median = stats_.median[hour_idx];
    const float scale = stats_.scale[hour_idx];

    const float z_score = (volume - median) / scale;
    ++sample_count_;
    running_mean_ += (z_score - running_mean_) / static_cast<float>(sample_count_);
    cumulative_ += z_score - running_mean_ - config_.delta;
    cumulative_min_ = std::min(cumulative_min_, cumulative_);
    const float score = cumulative_ - cumulative_min_;

    DetectionStep step;
    step.z_score = z_score;
    step.state_primary = running_mean_;
    step.state_secondary = cumulative_;
    step.score = score;
    step.alarm = score > config_.threshold;
    return step;
}
