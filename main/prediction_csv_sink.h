#pragma once

#include <cstdint>
#include <cstdio>

struct PredictionRecord {
    const char* config = nullptr;
    const char* cols = nullptr;
    float alpha = 0.0f;
    float drift = 0.0f;
    float threshold = 0.0f;
    const char* group_col = nullptr;
    const char* meter = nullptr;
    uint16_t scenario_id = 0;
    const char* source_file = nullptr;
    const char* scenario_type = nullptr;
    uint8_t last_label = 0;
    uint32_t time_s = 0;
    uint8_t hour = 0;
    uint8_t y_true = 0;
    uint8_t y_pred = 0;
    float score = 0.0f;
};

class PredictionCsvSink {
public:
    PredictionCsvSink() = default;
    ~PredictionCsvSink();

    // Opens the target file in truncate mode so each run replaces older output.
    bool open(const char* path);
    bool write_header();
    bool write_row(const PredictionRecord& record);
    void close();

private:
    FILE* file_ = nullptr;
};
