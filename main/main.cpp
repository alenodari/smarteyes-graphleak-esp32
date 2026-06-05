#include <cstdio>
#include <optional>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ewma_cusum_detector.h"
#include "graphleak_csv_replay.h"
#include "graphleak_stats_loader.h"
#include "page_hinkley_detector.h"
#include "prediction_csv_sink.h"
#include "sdcard_storage.h"
#include "shewhart_detector.h"

namespace {

constexpr char kTag[] = "graphleak";
constexpr char kInputPath[] = "/sdcard/graphleak_volume_experiments.csv";
constexpr char kStatsPath[] = "/sdcard/graphleak_reference_stats.csv";
constexpr EwmaCusumConfig kEwmaConfig{};
constexpr PageHinkleyConfig kPageHinkleyConfig{};
constexpr ShewhartConfig kShewhartConfig{};

enum class DetectorKind : uint8_t {
    EwmaCusum = 0,
    PageHinkley = 1,
    Shewhart = 2,
};

struct MeterReplayConfig {
    MeterId meter;
    const char* config_name;
    const char* column_name;
};

struct DetectorReplayConfig {
    DetectorKind kind;
    const char* detector_name;
    const char* output_path;
    float alpha;
    float drift;
    float delta;
    float threshold;
};

constexpr MeterReplayConfig kMeterConfigs[] = {
    {MeterId::N2, "local_N2", "V_N2"},
    {MeterId::N8, "local_N8_downstream_only", "V_N8"},
    {MeterId::N9, "local_N9_downstream_only", "V_N9"},
};

constexpr DetectorReplayConfig kDetectorConfigs[] = {
    {DetectorKind::EwmaCusum, "ewma_cusum", "/sdcard/ewma_cusum_preds.csv", kEwmaConfig.alpha, kEwmaConfig.drift, 0.0f, kEwmaConfig.threshold},
    {DetectorKind::PageHinkley, "page_hinkley", "/sdcard/page_hinkley_preds.csv", 0.0f, 0.0f, kPageHinkleyConfig.delta, kPageHinkleyConfig.threshold},
    {DetectorKind::Shewhart, "shewhart", "/sdcard/shewhart_preds.csv", 0.0f, 0.0f, 0.0f, kShewhartConfig.threshold},
};

struct ReplaySummary {
    size_t sample_count = 0;
    size_t tp = 0;
    size_t fp = 0;
    size_t fn = 0;
    size_t tn = 0;
    bool detected_any = false;
    bool valid_detection = false;
    bool clean_valid_detection = false;
    std::optional<uint32_t> first_alert_time_s;
    std::optional<uint32_t> first_valid_alert_time_s;
    std::optional<int> delay_min;
    size_t fp_before_onset = 0;
    float max_score = 0.0f;
};

struct DetectorTiming {
    int64_t elapsed_us = 0;
    size_t scenario_count = 0;
    size_t meter_replay_count = 0;
    size_t sample_count = 0;
};

float volume_for_meter(const ScenarioSample& sample, MeterId meter) {
    switch (meter) {
        case MeterId::N2:
            return sample.v_n2;
        case MeterId::N8:
            return sample.v_n8;
        case MeterId::N9:
            return sample.v_n9;
        default:
            return sample.v_n2;
    }
}

uint8_t y_true_for_meter(const ScenarioSample& sample, MeterId meter) {
    switch (meter) {
        case MeterId::N2:
            return sample.y_n2;
        case MeterId::N8:
            return sample.y_n8;
        case MeterId::N9:
            return sample.y_n9;
        default:
            return sample.y_n2;
    }
}

DetectionStep run_detector_step(
    DetectorKind kind,
    EwmaCusumDetector& ewma,
    PageHinkleyDetector& page_hinkley,
    ShewhartDetector& shewhart,
    float volume,
    uint8_t hour)
{
    switch (kind) {
        case DetectorKind::EwmaCusum:
            return ewma.update(volume, hour);
        case DetectorKind::PageHinkley:
            return page_hinkley.update(volume, hour);
        case DetectorKind::Shewhart:
            return shewhart.update(volume, hour);
        default:
            return ewma.update(volume, hour);
    }
}

ReplaySummary run_meter_replay(
    const ScenarioBuffer& scenario,
    const DetectorReplayConfig& detector_config,
    const MeterReplayConfig& meter_config,
    const GraphLeakStatsSet& stats_set,
    PredictionCsvSink& sink)
{
    EwmaCusumDetector ewma(stats_set.for_meter(meter_config.meter), kEwmaConfig);
    PageHinkleyDetector page_hinkley(stats_set.for_meter(meter_config.meter), kPageHinkleyConfig);
    ShewhartDetector shewhart(stats_set.for_meter(meter_config.meter), kShewhartConfig);
    ReplaySummary summary;
    summary.sample_count = scenario.samples.size();

    const char* meter = meter_name(meter_config.meter);
    bool seen_onset = false;
    uint32_t onset_time_s = 0;

    for (const ScenarioSample& sample : scenario.samples) {
        const uint8_t y_true = y_true_for_meter(sample, meter_config.meter);
        const DetectionStep step = run_detector_step(
            detector_config.kind,
            ewma,
            page_hinkley,
            shewhart,
            volume_for_meter(sample, meter_config.meter),
            sample.hour);

        PredictionRecord record;
        record.config = meter_config.config_name;
        record.cols = meter_config.column_name;
        record.alpha = detector_config.alpha;
        record.drift = detector_config.drift;
        record.delta = detector_config.delta;
        record.threshold = detector_config.threshold;
        record.group_col = "hour";
        record.meter = meter;
        record.scenario_id = scenario.scenario_id;
        record.source_file = scenario.source_file.c_str();
        record.scenario_type = scenario.scenario_type.c_str();
        record.last_label = scenario.last_label;
        record.time_s = sample.time_s;
        record.hour = sample.hour;
        record.y_true = y_true;
        record.y_pred = static_cast<uint8_t>(step.alarm ? 1U : 0U);
        record.score = step.score;

        if (!sink.write_row(record)) {
            ESP_LOGE(
                kTag,
                "Failed to write prediction row for detector=%s scenario_id=%u meter=%s",
                detector_config.detector_name,
                scenario.scenario_id,
                meter);
        }

        if (y_true == 1U && !seen_onset) {
            seen_onset = true;
            onset_time_s = sample.time_s;
        }

        if (y_true == 1U && step.alarm) {
            ++summary.tp;
        } else if (y_true == 0U && step.alarm) {
            ++summary.fp;
        } else if (y_true == 1U && !step.alarm) {
            ++summary.fn;
        } else {
            ++summary.tn;
        }

        if (step.alarm) {
            summary.detected_any = true;
            if (!summary.first_alert_time_s.has_value()) {
                summary.first_alert_time_s = sample.time_s;
            }
            if (y_true == 0U) {
                ++summary.fp_before_onset;
            } else if (!summary.first_valid_alert_time_s.has_value()) {
                summary.valid_detection = true;
                summary.first_valid_alert_time_s = sample.time_s;
                summary.delay_min = static_cast<int>((sample.time_s - onset_time_s) / 60U);
            }
        }

        if (step.score > summary.max_score) {
            summary.max_score = step.score;
        }
    }

    summary.clean_valid_detection = summary.valid_detection && summary.fp_before_onset == 0U;
    return summary;
}

bool run_detector_replays_from_csv(
    const GraphLeakStatsSet& stats_set,
    const DetectorReplayConfig& detector_config,
    PredictionCsvSink& sink,
    DetectorTiming& timing)
{
    timing = {};
    const int64_t start_us = esp_timer_get_time();

    const bool ok = for_each_graphleak_scenario_csv(
        kInputPath,
        [&](const ScenarioBuffer& scenario) {
            ++timing.scenario_count;
            for (const MeterReplayConfig& meter_config : kMeterConfigs) {
                const ReplaySummary summary = run_meter_replay(
                    scenario,
                    detector_config,
                    meter_config,
                    stats_set,
                    sink);
                ++timing.meter_replay_count;
                timing.sample_count += summary.sample_count;
            }
            return true;
        });

    if (!ok) {
        return false;
    }

    timing.elapsed_us = esp_timer_get_time() - start_us;
    return true;
}

}  // namespace

extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(1500));

    if (!init_sdcard_storage()) {
        ESP_LOGE(kTag, "Replay aborted: SD card init failed.");
        return;
    }

    GraphLeakStatsSet stats_set;
    if (!load_graphleak_stats_csv(kStatsPath, stats_set)) {
        ESP_LOGE(kTag, "Replay aborted: could not load stats CSV at %s", kStatsPath);
        return;
    }

    for (const DetectorReplayConfig& detector_config : kDetectorConfigs) {
        DetectorTiming timing;
        PredictionCsvSink sink;
        if (!sink.open(detector_config.output_path) || !sink.write_header()) {
            ESP_LOGE(kTag, "Replay aborted: could not open CSV output at %s", detector_config.output_path);
            return;
        }

        if (!run_detector_replays_from_csv(stats_set, detector_config, sink, timing)) {
            sink.close();
            ESP_LOGE(
                kTag,
                "Replay aborted: could not process input CSV at %s for detector=%s",
                kInputPath,
                detector_config.detector_name);
            return;
        }

        sink.close();
        ESP_LOGI(
            kTag,
            "TIMING detector=%s elapsed_ms=%.3f scenarios=%u meter_replays=%u samples=%u us_per_sample=%.3f output=%s",
            detector_config.detector_name,
            static_cast<double>(timing.elapsed_us) / 1000.0,
            static_cast<unsigned>(timing.scenario_count),
            static_cast<unsigned>(timing.meter_replay_count),
            static_cast<unsigned>(timing.sample_count),
            timing.sample_count > 0
                ? static_cast<double>(timing.elapsed_us) / static_cast<double>(timing.sample_count)
                : 0.0,
            detector_config.output_path);
    }
}
