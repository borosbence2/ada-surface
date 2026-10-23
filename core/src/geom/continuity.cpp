#include "ada/geom/continuity.hpp"

#include <fmt/format.h>

#include <stdexcept>

namespace ada {

const char* to_string(Continuity continuity) {
    switch (continuity) {
    case Continuity::G0:
        return "G0";
    case Continuity::G1:
        return "G1";
    case Continuity::G2:
        return "G2";
    }
    throw std::invalid_argument("unknown continuity");
}

Continuity continuity_from_string(const std::string& name) {
    for (const Continuity c : {Continuity::G0, Continuity::G1, Continuity::G2}) {
        if (name == to_string(c)) {
            return c;
        }
    }
    throw std::invalid_argument(
        fmt::format("'{}' is not a continuity; expected G0, G1 or G2", name));
}

} // namespace ada
