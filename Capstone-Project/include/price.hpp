#pragma once

#include <iomanip>
#include <sstream>
#include <string>

inline std::string display_price(int tick) {
    std::ostringstream out;
    out << (tick / 100) << '.' << std::setw(2) << std::setfill('0') << (tick % 100);
    return out.str();
}
