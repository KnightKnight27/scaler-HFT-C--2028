#pragma once

#include <cmath>
#include <format>
#include <string>

#include "order_book/types.h"

inline constexpr Price kTicksPerRupee = 100;

inline Price toTicks(double rupees) {
  return static_cast<Price>(std::llround(rupees * kTicksPerRupee));
}

inline std::string formatPrice(Price ticks) {
  return std::format("{}.{:02}", ticks / kTicksPerRupee, ticks % kTicksPerRupee);
}
