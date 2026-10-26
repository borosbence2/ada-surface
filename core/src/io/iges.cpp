#include "ada/io/iges.hpp"

#include "ada/build_info.hpp"

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace ada {

namespace {

// IGES 5.3 constants (sections 2.2 and 4.24 of the specification).
constexpr int kSurfaceEntity = 128;
constexpr int kVersionFlag = 11;     // IGES 5.3
constexpr int kUnitsMillimetres = 2; // Global parameter 14
constexpr std::size_t kDataColumns = 72;
constexpr std::size_t kParameterColumns = 64;
constexpr std::size_t kMaxNameLength = 40;

/// IGES strings are Hollerith constants: the length, 'H', the text.
std::string hollerith(const std::string& text) {
    return fmt::format("{}H{}", text.size(), text);
}

/// Reals always carry a decimal point (an IGES requirement; "800" would read
/// as an integer). 16 significant digits round-trip a double well past any
/// modelling tolerance.
std::string real(double value) {
    return fmt::format("{:.15E}", value);
}

/// Free-format parameters, delimited by ',' and closed by ';', packed into
/// lines of `width` columns without splitting a parameter.
std::vector<std::string> pack(const std::vector<std::string>& parameters, std::size_t width) {
    std::vector<std::string> lines;
    std::string line;
    for (std::size_t k = 0; k < parameters.size(); ++k) {
        const std::string token = parameters[k] + (k + 1 == parameters.size() ? ";" : ",");
        if (!line.empty() && line.size() + token.size() > width) {
            lines.push_back(line);
            line.clear();
        }
        line += token;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

std::string record(const std::string& data, char section, std::size_t sequence) {
    return fmt::format("{:<72}{}{:>7}", data, section, sequence);
}

std::string label_of(const std::string& name) {
    return name.size() > 8 ? name.substr(0, 8) : name;
}

} // namespace

std::string iges_document(const std::vector<NamedPatch>& patches,
                          const std::string& file_name,
                          const std::string& timestamp) {
    double max_coordinate = 0.0;
    for (const NamedPatch& p : patches) {
        if (p.patch.degree_u() < 1 || p.patch.degree_v() < 1) {
            throw std::invalid_argument(fmt::format(
                "patch '{}' has a degree-0 direction, which an IGES B-spline surface cannot hold",
                p.name));
        }
        for (const Vec3& point : p.patch.control_points()) {
            max_coordinate = std::max(max_coordinate, point.cwiseAbs().maxCoeff());
        }
    }

    // Strings are never split across records, so keep the name well inside
    // one Global-section line.
    const std::string name =
        file_name.size() > kMaxNameLength ? file_name.substr(0, kMaxNameLength) : file_name;
    std::vector<std::string> out;

    // Start section: free text for whoever opens the file.
    out.push_back(record(
        fmt::format("ADA Surface: {} Bezier patches as entity 128, millimetres.", patches.size()),
        'S',
        1));
    const std::size_t start_lines = 1;

    // Global section.
    const std::string version{build_info().version};
    const std::vector<std::string> global{
        "1H,",
        "1H;",
        hollerith(name),
        hollerith(name),
        hollerith("ADA Surface"),
        hollerith(version),
        "32",
        "38",
        "6",
        "308",
        "15",
        hollerith(name),
        real(1.0),
        std::to_string(kUnitsMillimetres),
        hollerith("MM"),
        "1",
        real(1.0),
        hollerith(timestamp),
        real(1e-6),
        real(max_coordinate),
        hollerith("ADA Surface"),
        hollerith("ADA Surface"),
        std::to_string(kVersionFlag),
        "0",
        hollerith(timestamp),
    };
    const std::vector<std::string> global_lines = pack(global, kDataColumns);
    for (std::size_t k = 0; k < global_lines.size(); ++k) {
        out.push_back(record(global_lines[k], 'G', k + 1));
    }

    // Parameter data first (in memory), so the directory entries can point
    // into it.
    struct Entity {
        std::vector<std::string> lines;
        std::size_t first_parameter_line = 0;
        std::string label;
    };

    std::vector<Entity> entities;
    std::size_t next_parameter_line = 1;
    for (const NamedPatch& p : patches) {
        const BezierPatch& s = p.patch;
        const int n = s.degree_u();
        const int m = s.degree_v();
        std::vector<std::string> params{std::to_string(kSurfaceEntity),
                                        std::to_string(n), // K1: upper index of the u sum
                                        std::to_string(m), // K2
                                        std::to_string(n), // M1: degree in u
                                        std::to_string(m), // M2
                                        "0",               // PROP1: not closed in u
                                        "0",               // PROP2: not closed in v
                                        "1",               // PROP3: polynomial (all weights equal)
                                        "0",               // PROP4: not periodic in u
                                        "0"};              // PROP5: not periodic in v
        // A single span: degree + 1 zeros, then degree + 1 ones.
        for (int k = 0; k <= n; ++k)
            params.push_back(real(0.0));
        for (int k = 0; k <= n; ++k)
            params.push_back(real(1.0));
        for (int k = 0; k <= m; ++k)
            params.push_back(real(0.0));
        for (int k = 0; k <= m; ++k)
            params.push_back(real(1.0));
        // Weights and control points with the first (u) index varying fastest.
        for (std::size_t j = 0; j < s.num_v(); ++j) {
            for (std::size_t i = 0; i < s.num_u(); ++i) {
                params.push_back(real(1.0));
            }
        }
        for (std::size_t j = 0; j < s.num_v(); ++j) {
            for (std::size_t i = 0; i < s.num_u(); ++i) {
                const Vec3& point = s.control_point(i, j);
                params.push_back(real(point.x()));
                params.push_back(real(point.y()));
                params.push_back(real(point.z()));
            }
        }
        for (const double bound : {0.0, 1.0, 0.0, 1.0}) {
            params.push_back(real(bound));
        }
        Entity e{pack(params, kParameterColumns), next_parameter_line, label_of(p.name)};
        next_parameter_line += e.lines.size();
        entities.push_back(std::move(e));
    }

    // Directory entries: two records per entity.
    for (std::size_t k = 0; k < entities.size(); ++k) {
        const Entity& e = entities[k];
        const std::size_t sequence = 2 * k + 1;
        out.push_back(record(fmt::format("{:>8}{:>8}{:>8}{:>8}{:>8}{:>8}{:>8}{:>8}{:>8}",
                                         kSurfaceEntity,
                                         e.first_parameter_line,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         "00000000"),
                             'D',
                             sequence));
        out.push_back(record(fmt::format("{:>8}{:>8}{:>8}{:>8}{:>8}{:>8}{:>8}{:>8}{:>8}",
                                         kSurfaceEntity,
                                         0,
                                         0,
                                         e.lines.size(),
                                         0,
                                         "",
                                         "",
                                         e.label,
                                         0),
                             'D',
                             sequence + 1));
    }

    // Parameter data: columns 66-72 point back at the entity's directory entry.
    for (std::size_t k = 0; k < entities.size(); ++k) {
        const Entity& e = entities[k];
        for (std::size_t l = 0; l < e.lines.size(); ++l) {
            out.push_back(record(fmt::format("{:<64} {:>7}", e.lines[l], 2 * k + 1),
                                 'P',
                                 e.first_parameter_line + l));
        }
    }

    out.push_back(record(fmt::format("S{:>7}G{:>7}D{:>7}P{:>7}",
                                     start_lines,
                                     global_lines.size(),
                                     2 * entities.size(),
                                     next_parameter_line - 1),
                         'T',
                         1));

    std::string text;
    for (const std::string& line : out) {
        text += line;
        text += '\n';
    }
    return text;
}

void write_iges(const Scene& scene, const std::filesystem::path& path) {
    std::vector<NamedPatch> patches;
    for (const ScenePatch& p : scene.patches) {
        if (!p.solved) {
            throw std::invalid_argument(
                fmt::format("patch '{}' is unsolved; solve the scene before exporting it", p.id));
        }
        patches.push_back({p.id, p.patch});
    }
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const std::string timestamp = fmt::format("{:%Y%m%d.%H%M%S}", now);
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::invalid_argument(fmt::format("cannot open {} for writing", path.string()));
    }
    stream << iges_document(patches, path.filename().string(), timestamp);
}

} // namespace ada
