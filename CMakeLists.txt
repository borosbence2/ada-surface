cmake_minimum_required(VERSION 3.24)

project(ada_surface
    VERSION 0.1.0
    DESCRIPTION "AI-Driven A-surfacing: a constraint-solving Class-A surface kernel"
    LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use `cmake --preset dev`.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MINGW)
    # Git for Windows ships its own, older libstdc++-6.dll in <Git>/mingw64/bin,
    # which precedes the real toolchain on PATH inside a Git Bash shell. Freshly
    # linked executables then fail to start with STATUS_ENTRYPOINT_NOT_FOUND
    # (0xC0000139) before main() is reached. Linking the runtime statically
    # removes the DLL search order from the picture and makes the binaries
    # self-contained, which also matters once Studio spawns ada-mcp. Applied
    # before the dependencies are added so that it covers every target.
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif()

# Single output directory keeps `./build/<preset>/bin/ada-mcp` predictable
# across generators (MSVC otherwise appends a per-config subdirectory). Studio
# looks for the sidecar there during development.
foreach(_cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${_cfg} "${CMAKE_BINARY_DIR}/bin")
endforeach()

option(ADA_BUILD_TESTS "Build the Catch2 test suite" ON)
option(ADA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(ADA_ENABLE_SANITIZERS "Enable ASan + UBSan (Linux/macOS, GCC/Clang only)" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Sanitizers)
include(GitVersion)
include(Dependencies)

if(ADA_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(core)
add_subdirectory(apps/cli)
add_subdirectory(apps/mcp)

if(ADA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

message(STATUS "")
message(STATUS "ADA Surface ${PROJECT_VERSION} (${ADA_GIT_DESCRIBE})")
message(STATUS "  build type ......... ${CMAKE_BUILD_TYPE}")
message(STATUS "  compiler ........... ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  tests .............. ${ADA_BUILD_TESTS}")
message(STATUS "  warnings as errors . ${ADA_WARNINGS_AS_ERRORS}")
message(STATUS "  sanitizers ......... ${ADA_SANITIZERS_ACTIVE}")
message(STATUS "")
