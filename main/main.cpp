#include <cstdio>
#include <optional>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ewma_cusum_detector.h"
#include "graphleak_csv_replay.h"
#include "graphleak_stats_loader.h"
#include "prediction_csv_sink.h"
#include "sdcard_storage.h"

namespace {

constexpr char kTag[] = "graphleak";
constexpr char kInputPath[] = "/sdcard/graphleak_volume_experiments.csv";
constexpr char kStatsPath[] = "/sdcard/graphleak_reference_stats.csv";
constexpr char kOutputPath[] = "/sdcard/ewma_preds.csv";
constexpr EwmaCusumConfig kDefaultConfig{};

struct MeterReplayConfig {
    MeterId meter;
    const char* config_name;
    const char* column_name;
};

constexpr MeterReplayConfig kMeterConfigs[] = {
    {MeterId::N2, "local_N2", "V_N2"},
    {MeterId::N8, "local_N8_downstream_only", "V_N8"},
    {MeterId::N9, "local_N9_downstream_only", "V_N9"},
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

ReplaySummary run_meter_replay(
    const ScenarioBuffer& scenario,
    const MeterReplayConfig& meter_config,
    const GraphLeakStatsSet& stats_set,
    PredictionCsvSink& sink)
{
    EwmaCusumDetector detector(stats_set.for_meter(meter_config.meter), kDefaultConfig);
    ReplaySummary summary;
    summary.sample_count = scenario.samples.size();

    const char* meter = meter_name(meter_config.meter);
    bool seen_onset = false;
    uint32_t onset_time_s = 0;

    ESP_LOGI(
        kTag,
        "Running scenario_id=%u meter=%s type=%s samples=%u",
        scenario.scenario_id,
        meter,
        scenario.scenario_type.c_str(),
        static_cast<unsigned>(scenario.samples.size()));

    for (const ScenarioSample& sample : scenario.samples) {
        const uint8_t y_true = y_true_for_meter(sample, meter_config.meter);
        const DetectionStep step = detector.update(volume_for_meter(sample, meter_config.meter), sample.hour);

        PredictionRecord record;
        record.config = meter_config.config_name;
        record.cols = meter_config.column_name;
        record.alpha = kDefaultConfig.alpha;
        record.drift = kDefaultConfig.drift;
        record.threshold = kDefaultConfig.threshold;
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
        record.score = step.cusum;

        if (!sink.write_row(record)) {
            ESP_LOGE(kTag, "Failed to write prediction row for scenario_id=%u meter=%s", scenario.scenario_id, meter);
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

        if (step.cusum > summary.max_score) {
            summary.max_score = step.cusum;
        }
    }

    summary.clean_valid_detection = summary.valid_detection && summary.fp_before_onset == 0U;
    return summary;
}

void log_summary(
    const ScenarioBuffer& scenario,
    const MeterReplayConfig& meter_config,
    const ReplaySummary& summary)
{
    ESP_LOGI(
        kTag,
        "Summary scenario_id=%u meter=%s: samples=%u tp=%u fp=%u fn=%u tn=%u detected_any=%u "
        "valid=%u clean=%u first_alert=%ld first_valid=%ld delay_min=%d fp_before_onset=%u max_score=%.3f",
        scenario.scenario_id,
        meter_name(meter_config.meter),
        static_cast<unsigned>(summary.sample_count),
        static_cast<unsigned>(summary.tp),
        static_cast<unsigned>(summary.fp),
        static_cast<unsigned>(summary.fn),
        static_cast<unsigned>(summary.tn),
        summary.detected_any ? 1U : 0U,
        summary.valid_detection ? 1U : 0U,
        summary.clean_valid_detection ? 1U : 0U,
        summary.first_alert_time_s.has_value() ? static_cast<long>(*summary.first_alert_time_s) : -1L,
        summary.first_valid_alert_time_s.has_value() ? static_cast<long>(*summary.first_valid_alert_time_s) : -1L,
        summary.delay_min.has_value() ? *summary.delay_min : -1,
        static_cast<unsigned>(summary.fp_before_onset),
        summary.max_score);
}

bool run_all_replays_from_csv(const GraphLeakStatsSet& stats_set, PredictionCsvSink& sink) {
    size_t scenario_count = 0;

    const bool ok = for_each_graphleak_scenario_csv(
        kInputPath,
        [&](const ScenarioBuffer& scenario) {
            ++scenario_count;
            for (const MeterReplayConfig& meter_config : kMeterConfigs) {
                const ReplaySummary summary = run_meter_replay(scenario, meter_config, stats_set, sink);
                log_summary(scenario, meter_config, summary);
            }
            return true;
        });

    if (!ok) {
        return false;
    }

    ESP_LOGI(
        kTag,
        "Finished CSV replay for %u scenarios and %u meter configurations",
        static_cast<unsigned>(scenario_count),
        static_cast<unsigned>(sizeof(kMeterConfigs) / sizeof(kMeterConfigs[0])));
    return true;
}

}  // namespace

extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(kTag, "GraphLeak local EWMA+CUSUM detector starting on ESP32-S3");

    if (!init_sdcard_storage()) {
        ESP_LOGE(kTag, "Replay aborted: SD card init failed.");
        return;
    }

    GraphLeakStatsSet stats_set;
    if (!load_graphleak_stats_csv(kStatsPath, stats_set)) {
        ESP_LOGE(kTag, "Replay aborted: could not load stats CSV at %s", kStatsPath);
        return;
    }

    PredictionCsvSink sink;
    if (!sink.open(kOutputPath) || !sink.write_header()) {
        ESP_LOGE(kTag, "Replay aborted: could not open CSV output at %s", kOutputPath);
        return;
    }

    if (!run_all_replays_from_csv(stats_set, sink)) {
        sink.close();
        ESP_LOGE(kTag, "Replay aborted: could not process input CSV at %s", kInputPath);
        return;
    }

    sink.close();
    ESP_LOGI(kTag, "Replay finished. Input=%s Stats=%s Output=%s", kInputPath, kStatsPath, kOutputPath);
    ESP_LOGI(kTag, "Device is idle.");
}
