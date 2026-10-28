#include "ada/doc/bench.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("the bench runs every committed case and they all pass", "[doc][bench]") {
    const ada::BenchResult result = ada::run_bench(std::filesystem::path(ADA_SOURCE_DIR) / "cases");
    // L0 to L3; cases/examples and cases/targets are subdirectories, not part of the bench.
    REQUIRE(result.cases.size() == 6);
    for (const ada::BenchCase& c : result.cases) {
        INFO(c.name << ": " << c.error);
        CHECK(c.pass);
        CHECK(c.error.empty());
    }
    CHECK(result.pass);
    const std::string markdown = ada::bench_markdown(result);
    CHECK(markdown.find("| L2-hood-2x2-sym | pass |") != std::string::npos);
    CHECK(markdown.find("| L3-hood-from-target | pass |") != std::string::npos);
    CHECK(ada::to_json(result).at("cases").size() == 6);
}

TEST_CASE("a broken case fails the bench without stopping it", "[doc][bench]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ada-bench-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::filesystem::copy_file(std::filesystem::path(ADA_SOURCE_DIR) / "cases" /
                                   "L0-roof-centre.ada.json",
                               dir / "a-good.ada.json");
    std::ofstream(dir / "b-broken.ada.json") << "{ not json";

    const ada::BenchResult result = ada::run_bench(dir);
    REQUIRE(result.cases.size() == 2);
    CHECK(result.cases[0].pass);
    CHECK_FALSE(result.cases[1].pass);
    CHECK_FALSE(result.cases[1].error.empty());
    CHECK_FALSE(result.pass);
    CHECK(ada::bench_markdown(result).find("**ERROR**") != std::string::npos);
    std::filesystem::remove_all(dir);
}
