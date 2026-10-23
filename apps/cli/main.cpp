// ada: command-line front end to the kernel. `report` arrives with M2,
// `bench` and `export` with M6.

#include "ada/build_info.hpp"

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <string>

int main(int argc, char** argv) {
    CLI::App app{"ADA Surface command-line tools", "ada"};
    app.set_version_flag("--version", std::string{ada::build_info_string()});
    app.require_subcommand(0, 1);

    CLI11_PARSE(app, argc, argv);

    if (app.get_subcommands().empty()) {
        fmt::print("{}", app.help());
    }
    return 0;
}
