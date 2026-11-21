#pragma once
#include <array>

namespace gh
{
// GM/FPC roles mapped to the user's four-octave, 12-role layout.
inline int mapGmNote(int note)
{
    static constexpr std::array<int, 53> mapping {
        37, 36, 40, 38, 39, 40, 80, 43, 68, 55, 56, 46, 56, 44, 47, 44,
        59, 71, 83, 45, 71, 42, 71, 66, 83, 41, 53, 65, 77, 53, 42, 54,
        66, 78, 57, 69, 42, 54, 66, 78, 41, 65, 77, 42, 54, 66, 78, 81,
        57, 83, 65, 56, 80
    };
    return note >= 35 && note <= 87 ? mapping[static_cast<size_t>(note - 35)] : -1;
}
}
