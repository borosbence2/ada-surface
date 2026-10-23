#include "ada/analysis/sampling.hpp"

#include <fmt/format.h>

#include <stdexcept>

namespace ada {

std::vector<double> uniform_parameters(std::size_t count) {
    if (count < 2) {
        throw std::invalid_argument(
            fmt::format("sampling [0, 1] needs at least 2 parameters, got {}", count));
    }
    std::vector<double> values(count);
    const double last = static_cast<double>(count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        values[i] = static_cast<double>(i) / last;
    }
    return values;
}

} // namespace ada
