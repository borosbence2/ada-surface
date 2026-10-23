add_executable(ada-mcp main.cpp)

target_link_libraries(ada-mcp PRIVATE
    ada::core
    CLI11::CLI11
    fmt::fmt
    ada::warnings
    ada::sanitizers)
