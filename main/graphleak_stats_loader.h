#pragma once

#include "graphleak_types.h"

struct GraphLeakStatsSet {
    HourlyRobustStats n2;
    HourlyRobustStats n8;
    HourlyRobustStats n9;

    const HourlyRobustStats& for_meter(MeterId meter) const;
};

bool load_graphleak_stats_csv(const char* path, GraphLeakStatsSet& stats_set);
