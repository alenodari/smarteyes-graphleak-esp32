idf_component_register(
    SRCS
        "main.cpp"
        "ewma_cusum_detector.cpp"
        "graphleak_csv_replay.cpp"
        "graphleak_stats_loader.cpp"
        "page_hinkley_detector.cpp"
        "prediction_csv_sink.cpp"
        "sdcard_storage.cpp"
        "shewhart_detector.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_timer
        fatfs
        esp_driver_sdmmc
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
