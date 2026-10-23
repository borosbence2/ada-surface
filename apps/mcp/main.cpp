// ada-mcp: the kernel's MCP server and the only process that touches
// geometry. Studio and agents are both clients of it.
//
// Transport: MCP stdio. One JSON-RPC message per line on stdin, one per line
// on stdout. Nothing else may ever be written to stdout; diagnostics go to
// stderr.

#include "ada/build_info.hpp"

#include "server.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

int main(int argc, char** argv) {
    CLI::App app{"ADA Surface MCP server (stdio)", "ada-mcp"};
    app.set_version_flag("--version", std::string{ada::build_info_string()});
    std::filesystem::path scene;
    bool ui_tools = false;
    app.add_option("--scene", scene, "Scene to open at start")->check(CLI::ExistingFile);
    app.add_flag(
        "--ui-tools", ui_tools, "Also offer Studio's tools (tessellation, CV hulls, combs)");
    CLI11_PARSE(app, argc, argv);

#ifdef _WIN32
    // Text mode would turn every "\n" into "\r\n" on the wire.
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif

    ada::mcp::ServerOptions options;
    options.ui_tools = ui_tools;
    if (!scene.empty()) {
        options.scene = scene;
    }

    try {
        ada::mcp::McpServer server(options);
        fmt::print(stderr,
                   "ada-mcp {} ready{}\n",
                   ada::build_info().version,
                   ui_tools ? " (ui tools)" : "");
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.find_first_not_of(" \t\r") == std::string::npos) {
                continue;
            }
            if (const auto response = server.handle_line(line)) {
                std::fwrite(response->data(), 1, response->size(), stdout);
                std::fputc('\n', stdout);
                std::fflush(stdout);
            }
        }
    } catch (const std::exception& e) {
        fmt::print(stderr, "ada-mcp: {}\n", e.what());
        return 1;
    }
    return 0;
}
