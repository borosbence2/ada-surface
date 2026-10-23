#pragma once

#include <cstddef>
#include <vector>

namespace ada {

/// `count` evenly spaced parameters covering [0, 1], both ends included.
/// Throws `std::invalid_argument` for `count < 2`.
std::vector<double> uniform_parameters(std::size_t count);

} // namespace ada
