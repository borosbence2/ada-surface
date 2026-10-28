# The server logic is a library so the tests can drive it message by message;
# the executable only adds the stdio transport.
add_library(ada_mcp_server STATIC
    base64.cpp
    base64.hpp
    common.cpp
    common.hpp
    edit.hpp
    edit_tools.cpp
    server.cpp
    server.hpp
    target_tools.cpp
    tools.cpp
    tools.hpp)

add_library(ada::mcp_server ALIAS ada_mcp_server)

target_include_directories(ada_mcp_server PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(ada_mcp_server
    PUBLIC
        ada::core
        nlohmann_json::nlohmann_json
        ada::sanitizers
    PRIVATE
        fmt::fmt
        ada::warnings)

add_executable(ada-mcp main.cpp)

target_link_libraries(ada-mcp PRIVATE
    ada::mcp_server
    CLI11::CLI11
    fmt::fmt
    ada::warnings
    ada::sanitizers)
