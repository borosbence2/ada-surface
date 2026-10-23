// ada-mcp: the kernel's MCP stdio server, and the only process that touches
// geometry. Studio and agents are both clients of it. The JSON-RPC transport
// and the first tools arrive with M3; until then it only reports its version.

#include "ada/build_info.hpp"

#include <CLI/CLI.hpp>

#include <string>

int main(int argc, char** argv) {
    CLI::App app{"ADA Surface MCP server (stdio)", "ada-mcp"};
    app.set_version_flag("--version", std::string{ada::build_info_string()});

    CLI11_PARSE(app, argc, argv);
    return 0;
}
