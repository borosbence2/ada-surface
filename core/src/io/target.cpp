#include "ada/io/target.hpp"

#include <Eigen/Geometry>
#include <fmt/format.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ada {

// Binary STL and PLY are little-endian; reading them with memcpy assumes the
// host is too, which every platform ADA builds for is.
static_assert(std::endian::native == std::endian::little);

namespace {

[[noreturn]] void fail(const std::filesystem::path& path, const std::string& what) {
    throw std::invalid_argument(fmt::format("{}: {}", path.generic_string(), what));
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

/// Reads a little-endian value at `pos` and moves past it.
template<typename T>
T take(const std::filesystem::path& path, const std::string& data, std::size_t& pos) {
    if (pos + sizeof(T) > data.size()) {
        fail(path, "the file ends early");
    }
    T value;
    std::memcpy(&value, data.data() + pos, sizeof(T));
    pos += sizeof(T);
    return value;
}

// --- OBJ ----------------------------------------------------------------------------

TargetMesh read_obj(const std::filesystem::path& path, const std::string& text) {
    TargetMesh mesh;
    std::istringstream in(text);
    std::string line;
    for (std::size_t number = 1; std::getline(in, line); ++number) {
        std::istringstream fields(line);
        std::string tag;
        fields >> tag;
        if (tag == "v") {
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            if (!(fields >> x >> y >> z)) {
                fail(path, fmt::format("line {}: a vertex needs x, y and z", number));
            }
            mesh.vertices.emplace_back(x, y, z);
        } else if (tag == "f") {
            std::vector<std::uint32_t> corners;
            std::string token;
            while (fields >> token) {
                long k = 0;
                try {
                    k = std::stol(token.substr(0, token.find('/')));
                } catch (const std::exception&) {
                    fail(path, fmt::format("line {}: '{}' is not a vertex index", number, token));
                }
                // 1-based, or negative counting back from the last vertex.
                const long count = static_cast<long>(mesh.vertices.size());
                const long index = k > 0 ? k - 1 : count + k;
                if (k == 0 || index < 0 || index >= count) {
                    fail(path, fmt::format("line {}: vertex {} does not exist", number, k));
                }
                corners.push_back(static_cast<std::uint32_t>(index));
            }
            if (corners.size() < 3) {
                fail(path, fmt::format("line {}: a face needs at least three vertices", number));
            }
            for (std::size_t m = 1; m + 1 < corners.size(); ++m) {
                mesh.triangles.push_back({corners[0], corners[m], corners[m + 1]});
            }
        }
    }
    return mesh;
}

// --- STL -----------------------------------------------------------------------------

constexpr std::size_t kStlHeader = 80;
constexpr std::size_t kStlFacet = 50; // normal, three vertices (float32 each), attribute

TargetMesh read_stl(const std::filesystem::path& path, const std::string& data) {
    TargetMesh mesh;
    // Some binary files also start with "solid", so the size decides: a
    // binary file is exactly header + count + facets.
    if (data.size() >= kStlHeader + 4) {
        std::size_t pos = kStlHeader;
        const auto count = take<std::uint32_t>(path, data, pos);
        if (data.size() == kStlHeader + 4 + kStlFacet * count) {
            for (std::uint32_t f = 0; f < count; ++f) {
                pos += 3 * sizeof(float); // the facet normal, recomputed where needed
                const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
                for (int corner = 0; corner < 3; ++corner) {
                    const auto x = take<float>(path, data, pos);
                    const auto y = take<float>(path, data, pos);
                    const auto z = take<float>(path, data, pos);
                    mesh.vertices.emplace_back(
                        static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
                }
                pos += sizeof(std::uint16_t);
                mesh.triangles.push_back({first, first + 1, first + 2});
            }
            return mesh;
        }
    }
    std::istringstream in(data);
    std::string token;
    while (in >> token) {
        if (lowercase(token) != "vertex") {
            continue;
        }
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        if (!(in >> x >> y >> z)) {
            fail(path, "a vertex needs x, y and z");
        }
        mesh.vertices.emplace_back(x, y, z);
        if (mesh.vertices.size() % 3 == 0) {
            const auto first = static_cast<std::uint32_t>(mesh.vertices.size() - 3);
            mesh.triangles.push_back({first, first + 1, first + 2});
        }
    }
    if (mesh.vertices.size() % 3 != 0) {
        fail(path, "a facet does not have three vertices");
    }
    return mesh;
}

// --- PLY -----------------------------------------------------------------------------

struct PlyProperty {
    std::string name;
    std::string type;       ///< the value type, or the item type of a list
    std::string count_type; ///< non-empty for a list
};

struct PlyElement {
    std::string name;
    std::size_t count = 0;
    std::vector<PlyProperty> properties;
};

std::size_t ply_size(const std::filesystem::path& path, const std::string& type) {
    static const std::map<std::string, std::size_t> sizes{{"char", 1},
                                                          {"int8", 1},
                                                          {"uchar", 1},
                                                          {"uint8", 1},
                                                          {"short", 2},
                                                          {"int16", 2},
                                                          {"ushort", 2},
                                                          {"uint16", 2},
                                                          {"int", 4},
                                                          {"int32", 4},
                                                          {"uint", 4},
                                                          {"uint32", 4},
                                                          {"float", 4},
                                                          {"float32", 4},
                                                          {"double", 8},
                                                          {"float64", 8}};
    const auto found = sizes.find(type);
    if (found == sizes.end()) {
        fail(path, fmt::format("unknown PLY type '{}'", type));
    }
    return found->second;
}

/// One value of a PLY type, read from binary data.
double take_ply(const std::filesystem::path& path,
                const std::string& data,
                std::size_t& pos,
                const std::string& type) {
    if (type == "char" || type == "int8") {
        return take<std::int8_t>(path, data, pos);
    }
    if (type == "uchar" || type == "uint8") {
        return take<std::uint8_t>(path, data, pos);
    }
    if (type == "short" || type == "int16") {
        return take<std::int16_t>(path, data, pos);
    }
    if (type == "ushort" || type == "uint16") {
        return take<std::uint16_t>(path, data, pos);
    }
    if (type == "int" || type == "int32") {
        return take<std::int32_t>(path, data, pos);
    }
    if (type == "uint" || type == "uint32") {
        return take<std::uint32_t>(path, data, pos);
    }
    if (type == "float" || type == "float32") {
        return static_cast<double>(take<float>(path, data, pos));
    }
    if (type == "double" || type == "float64") {
        return take<double>(path, data, pos);
    }
    fail(path, fmt::format("unknown PLY type '{}'", type));
}

TargetMesh read_ply(const std::filesystem::path& path, const std::string& data) {
    const std::size_t end = data.find("end_header");
    if (end == std::string::npos) {
        fail(path, "no end_header in the PLY header");
    }
    std::istringstream header(data.substr(0, end));
    std::string line;
    std::string format;
    std::vector<PlyElement> elements;
    while (std::getline(header, line)) {
        std::istringstream fields(line);
        std::string keyword;
        fields >> keyword;
        if (keyword == "format") {
            fields >> format;
        } else if (keyword == "element") {
            PlyElement element;
            fields >> element.name >> element.count;
            elements.push_back(element);
        } else if (keyword == "property") {
            if (elements.empty()) {
                fail(path, "a PLY property before any element");
            }
            PlyProperty property;
            std::string type;
            fields >> type;
            if (type == "list") {
                fields >> property.count_type >> property.type >> property.name;
                static_cast<void>(ply_size(path, property.count_type));
            } else {
                property.type = type;
                fields >> property.name;
            }
            static_cast<void>(ply_size(path, property.type));
            elements.back().properties.push_back(property);
        }
    }
    if (format != "ascii" && format != "binary_little_endian") {
        fail(path,
             fmt::format("PLY format '{}' is not supported (ascii or binary_little_endian)",
                         format));
    }
    const bool binary = format == "binary_little_endian";
    std::size_t pos = data.find('\n', end);
    pos = pos == std::string::npos ? data.size() : pos + 1;
    std::istringstream text(binary ? std::string() : data.substr(pos));

    auto next = [&](const std::string& type) -> double {
        if (binary) {
            return take_ply(path, data, pos, type);
        }
        double value = 0.0;
        if (!(text >> value)) {
            fail(path, "the PLY data ends early");
        }
        return value;
    };

    TargetMesh mesh;
    for (const PlyElement& element : elements) {
        for (std::size_t item = 0; item < element.count; ++item) {
            Vec3 point = Vec3::Zero();
            std::vector<std::uint32_t> corners;
            for (const PlyProperty& property : element.properties) {
                if (property.count_type.empty()) {
                    const double value = next(property.type);
                    if (element.name == "vertex" && property.name.size() == 1 &&
                        std::string("xyz").find(property.name) != std::string::npos) {
                        point[static_cast<Eigen::Index>(property.name[0] - 'x')] = value;
                    }
                    continue;
                }
                const auto count = static_cast<std::size_t>(next(property.count_type));
                for (std::size_t k = 0; k < count; ++k) {
                    const double value = next(property.type);
                    if (element.name == "face") {
                        corners.push_back(static_cast<std::uint32_t>(value));
                    }
                }
            }
            if (element.name == "vertex") {
                mesh.vertices.push_back(point);
            } else if (element.name == "face" && corners.size() >= 3) {
                for (const std::uint32_t c : corners) {
                    if (c >= mesh.vertices.size()) {
                        fail(path, fmt::format("face vertex {} does not exist", c));
                    }
                }
                for (std::size_t m = 1; m + 1 < corners.size(); ++m) {
                    mesh.triangles.push_back({corners[0], corners[m], corners[m + 1]});
                }
            }
        }
    }
    return mesh;
}

} // namespace

TargetMesh read_target_mesh(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        fail(path, "cannot open the file");
    }
    const std::string data{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    const std::string extension = lowercase(path.extension().string());
    if (extension == ".obj") {
        return read_obj(path, data);
    }
    if (extension == ".stl") {
        return read_stl(path, data);
    }
    if (extension == ".ply") {
        return read_ply(path, data);
    }
    fail(path, "targets are .obj, .stl or .ply files");
}

std::vector<Vec3> sample_target(const TargetMesh& mesh, double spacing) {
    if (!(spacing > 0.0)) {
        throw std::invalid_argument(
            fmt::format("sample spacing must be positive, got {}", spacing));
    }
    if (mesh.triangles.empty()) {
        return mesh.vertices;
    }
    std::vector<Vec3> points;
    for (const auto& t : mesh.triangles) {
        const Vec3& a = mesh.vertices[t[0]];
        const Vec3 ab = mesh.vertices[t[1]] - a;
        const Vec3 ac = mesh.vertices[t[2]] - a;
        if (ab.cross(ac).norm() == 0.0) {
            continue; // no area, nothing to sample
        }
        const double longest = std::max({ab.norm(), ac.norm(), (ac - ab).norm()});
        const auto n = static_cast<int>(std::max(1.0, std::ceil(longest / spacing)));
        // The centroids of the n^2 small triangles: n(n+1)/2 pointing like the
        // original, n(n-1)/2 the other way.
        for (int i = 0; i < n; ++i) {
            for (int j = 0; i + j < n; ++j) {
                points.push_back(a + ((i + 1.0 / 3.0) / n) * ab + ((j + 1.0 / 3.0) / n) * ac);
                if (i + j < n - 1) {
                    points.push_back(a + ((i + 2.0 / 3.0) / n) * ab + ((j + 2.0 / 3.0) / n) * ac);
                }
            }
        }
    }
    return points;
}

} // namespace ada
