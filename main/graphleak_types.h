#pragma once

#include <array>
#include <cstdint>

enum class MeterId : uint8_t {
    N2 = 0,
    N8 = 1,
    N9 = 2,
};

struct HourlyRobustStats {
    std::array<float, 24> median{};
    std::array<float, 24> scale{};
};

inline const char* meter_name(MeterId meter) {
    switch (meter) {
        case MeterId::N2:
            return "N2";
        case MeterId::N8:
            return "N8";
        case MeterId::N9:
            return "N9";
        default:
            return "unknown";
    }
}
