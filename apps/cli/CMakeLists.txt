add_executable(ada main.cpp)

target_link_libraries(ada PRIVATE
    ada::core
    CLI11::CLI11
    fmt::fmt
    ada::warnings
    ada::sanitizers)
