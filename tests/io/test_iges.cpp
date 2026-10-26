#include "ada/io/iges.hpp"

#include "support/panels.hpp"
#include "support/polynomial.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using ada::BezierPatch;
using ada::NamedPatch;
using ada::Vec3;

namespace {

constexpr std::size_t kRecordLength = 80;
/// 16 significant digits on a ~1 m part.
constexpr double kRoundTripMm = 1e-9;

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        lines.push_back(line);
    }
    return lines;
}

std::string trim(std::string s) {
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    while (!s.empty() && s.front() == ' ')
        s.erase(s.begin());
    return s;
}

/// A minimal IGES reader for what the writer produces: enough to check the
/// structure and read entity 128 back. Independent of the writer's code.
struct Parsed {
    std::map<char, std::vector<std::string>> sections;
    std::vector<BezierPatch> patches;
    std::vector<std::string> labels;
};

Parsed parse(const std::string& text) {
    Parsed parsed;
    for (const std::string& line : lines_of(text)) {
        REQUIRE(line.size() == kRecordLength);
        const char section = line[72];
        const std::size_t sequence = std::stoul(line.substr(73));
        auto& lines = parsed.sections[section];
        CHECK(sequence == lines.size() + 1); // consecutive within each section
        lines.push_back(line);
    }
    const auto& d = parsed.sections['D'];
    const auto& p = parsed.sections['P'];
    REQUIRE(d.size() % 2 == 0);
    for (std::size_t k = 0; k < d.size(); k += 2) {
        auto field = [&](const std::string& line, std::size_t index) {
            return trim(line.substr(8 * index, 8));
        };
        CHECK(field(d[k], 0) == "128");
        CHECK(field(d[k + 1], 0) == "128");
        const std::size_t first = std::stoul(field(d[k], 1));
        const std::size_t count = std::stoul(field(d[k + 1], 3));
        parsed.labels.push_back(field(d[k + 1], 7));

        std::string data;
        for (std::size_t l = first; l < first + count; ++l) {
            const std::string& line = p.at(l - 1);
            CHECK(std::stoul(trim(line.substr(65, 7))) == k + 1); // points back at its D entry
            data += trim(line.substr(0, 64));
        }
        REQUIRE(data.back() == ';');
        data.pop_back();
        std::vector<double> v;
        std::stringstream tokens(data);
        for (std::string token; std::getline(tokens, token, ',');) {
            v.push_back(std::stod(token));
        }
        std::size_t at = 0;
        auto next = [&]() { return v.at(at++); };
        CHECK(next() == 128.0);
        const int k1 = static_cast<int>(next());
        const int k2 = static_cast<int>(next());
        const int m1 = static_cast<int>(next());
        const int m2 = static_cast<int>(next());
        CHECK(k1 == m1); // single span
        CHECK(k2 == m2);
        for (int prop = 0; prop < 5; ++prop) {
            CHECK(next() == (prop == 2 ? 1.0 : 0.0)); // only PROP3 (polynomial) set
        }
        for (int i = 0; i <= m1; ++i)
            CHECK(next() == 0.0);
        for (int i = 0; i <= m1; ++i)
            CHECK(next() == 1.0);
        for (int i = 0; i <= m2; ++i)
            CHECK(next() == 0.0);
        for (int i = 0; i <= m2; ++i)
            CHECK(next() == 1.0);
        const std::size_t n_points = static_cast<std::size_t>((k1 + 1) * (k2 + 1));
        for (std::size_t w = 0; w < n_points; ++w)
            CHECK(next() == 1.0);
        std::vector<Vec3> points(n_points);
        for (std::size_t j = 0; j <= static_cast<std::size_t>(k2); ++j) {
            for (std::size_t i = 0; i <= static_cast<std::size_t>(k1); ++i) {
                const double x = next();
                const double y = next();
                const double z = next();
                points[i * static_cast<std::size_t>(k2 + 1) + j] = Vec3(x, y, z);
            }
        }
        CHECK(next() == 0.0);
        CHECK(next() == 1.0);
        CHECK(next() == 0.0);
        CHECK(next() == 1.0);
        CHECK(at == v.size());
        parsed.patches.emplace_back(m1, m2, std::move(points));
    }
    return parsed;
}

} // namespace

TEST_CASE("IGES output reads back to the same patches", "[io][iges]") {
    std::mt19937 rng(20261026);
    const std::vector<NamedPatch> patches{
        {"front", ada::test::hood_pair().first},
        {"a_long_patch_name", ada::test::random_patch(5, 3, rng, 1500.0)},
        {"bicubic", ada::test::random_patch(3, 3, rng, 800.0)},
    };
    const Parsed parsed = parse(ada::iges_document(patches, "hood.igs", "20261026.120000"));

    REQUIRE(parsed.patches.size() == patches.size());
    for (std::size_t k = 0; k < patches.size(); ++k) {
        INFO(patches[k].name);
        CHECK(parsed.patches[k].degree_u() == patches[k].patch.degree_u());
        CHECK(parsed.patches[k].degree_v() == patches[k].patch.degree_v());
        for (std::size_t c = 0; c < patches[k].patch.control_points().size(); ++c) {
            CHECK((parsed.patches[k].control_points()[c] - patches[k].patch.control_points()[c])
                      .norm() < kRoundTripMm);
        }
    }
    CHECK(parsed.labels[0] == "front");
    CHECK(parsed.labels[1] == "a_long_p"); // labels are eight columns

    // Terminate counts every other section's records.
    const std::string& t = parsed.sections.at('T').at(0);
    CHECK(std::stoul(t.substr(1, 7)) == parsed.sections.at('S').size());
    CHECK(std::stoul(t.substr(9, 7)) == parsed.sections.at('G').size());
    CHECK(std::stoul(t.substr(17, 7)) == parsed.sections.at('D').size());
    CHECK(std::stoul(t.substr(25, 7)) == parsed.sections.at('P').size());

    // Global section: millimetres (units flag 2, name "MM"), IGES 5.3 (11).
    std::string global;
    for (const std::string& line : parsed.sections.at('G')) {
        global += line.substr(0, 72);
    }
    CHECK(global.find(",2,2HMM,") != std::string::npos);
    CHECK(global.find(",11,0,15H20261026.120000;") != std::string::npos);
}

TEST_CASE("unsolved scenes and degree-0 patches are not exported", "[io][iges]") {
    std::mt19937 rng(20261026);
    CHECK_THROWS_AS(ada::iges_document(
                        {{"flat", ada::test::random_patch(0, 3, rng)}}, "x.igs", "20261026.120000"),
                    std::invalid_argument);

    ada::Scene scene = ada::test::hood_scene();
    scene.patches[0].solved = false;
    CHECK_THROWS_AS(
        ada::write_iges(scene, std::filesystem::temp_directory_path() / "ada-unsolved.igs"),
        std::invalid_argument);
}
