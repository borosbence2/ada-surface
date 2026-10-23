#pragma once

#include <string>

namespace ada {

/// Required geometric continuity across an edge: G0 position, G1 tangent
/// plane, G2 curvature.
enum class Continuity { G0, G1, G2 };

const char* to_string(Continuity continuity);

/// Throws `std::invalid_argument` unless `name` is "G0", "G1" or "G2".
Continuity continuity_from_string(const std::string& name);

} // namespace ada
