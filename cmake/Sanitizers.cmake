# ASan + UBSan for Debug builds on Linux/macOS. The MinGW toolchain ships no
# sanitizer runtime and MSVC has no UBSan, so on those platforms the option
# degrades to a no-op instead of breaking the `dev` preset.

add_library(ada_sanitizers INTERFACE)
add_library(ada::sanitizers ALIAS ada_sanitizers)

set(ADA_SANITIZERS_ACTIVE "OFF" CACHE INTERNAL "" FORCE)

if(ADA_ENABLE_SANITIZERS)
    if(WIN32)
        message(STATUS "Sanitizers requested but unsupported on this platform - skipping")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(_ada_san_flags
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all)
        target_compile_options(ada_sanitizers INTERFACE ${_ada_san_flags})
        target_link_options(ada_sanitizers INTERFACE ${_ada_san_flags})
        set(ADA_SANITIZERS_ACTIVE "address,undefined" CACHE INTERNAL "" FORCE)
    else()
        message(STATUS "Sanitizers requested but ${CMAKE_CXX_COMPILER_ID} is unsupported - skipping")
    endif()
endif()
