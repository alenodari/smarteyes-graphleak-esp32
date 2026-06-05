#include "prediction_csv_sink.h"

#include <cinttypes>

PredictionCsvSink::~PredictionCsvSink() {
    close();
}

bool PredictionCsvSink::open(const char* path) {
    close();
    file_ = std::fopen(path, "w");
    return file_ != nullptr;
}

bool PredictionCsvSink::write_header() {
    if (file_ == nullptr) {
        return false;
    }

    return std::fprintf(
               file_,
               "config,cols,alpha,drift,threshold,group_col,meter,scenario_id,"
               "source_file,scenario_type,time_s,hour,last_label,y_true,y_pred,score\n") > 0;
}

bool PredictionCsvSink::write_row(const PredictionRecord& record) {
    if (file_ == nullptr) {
        return false;
    }

    return std::fprintf(
               file_,
               "%s,%s,%.2f,%.2f,%.1f,%s,%s,%" PRIu16 ",%s,%s,%" PRIu32 ",%" PRIu8 ",%" PRIu8 ",%" PRIu8 ",%" PRIu8 ",%.6f\n",
               record.config,
               record.cols,
               static_cast<double>(record.alpha),
               static_cast<double>(record.drift),
               static_cast<double>(record.threshold),
               record.group_col,
               record.meter,
               record.scenario_id,
               record.source_file,
               record.scenario_type,
               record.time_s,
               record.hour,
               record.last_label,
               record.y_true,
               record.y_pred,
               static_cast<double>(record.score)) > 0;
}

void PredictionCsvSink::close() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}
