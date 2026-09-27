#pragma once
#include <cmath>
#include <cstdint>

// V2's Python OLC computes int(round(nonnegative_scaled_coordinate, 6)).
// Only rounding up across the next integer can affect the truncated result.
inline int64_t pcad_olc_quantize(double scaled) {
    const double next = std::ceil(scaled);
    return static_cast<int64_t>(next - scaled <= 0.0000005 ? next : std::floor(scaled));
}
