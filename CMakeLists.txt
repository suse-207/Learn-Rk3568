cmake_minimum_required(VERSION 3.16)
project(rk3568-automotive-platform VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(WITH_VSOMEIP "Build with vsomeip support" ON)

# vsomeip third party
if(WITH_VSOMEIP)
    # Prefer the in-tree copy. When third_party/vsomeip is absent, fall back to a
    # system-installed vsomeip: src/com/CMakeLists.txt already probes
    # find_package(vsomeip3) and then pkg-config(vsomeip3) on its own.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vsomeip/CMakeLists.txt)
        add_subdirectory(third_party/vsomeip EXCLUDE_FROM_ALL)
    else()
        message(STATUS "third_party/vsomeip not found - src/com will use a system-installed vsomeip3")
    endif()
    if(TARGET vsomeip3)
        # vsomeip injects "-Werror" through CMAKE_CXX_FLAGS in its own
        # CMakeLists.txt (line ~240). With GCC 13/14 + Boost.ICL that turns
        # false-positive -Wstringop-overflow diagnostics raised *inside*
        # /usr/include/boost/icl/* into hard errors, e.g. when compiling
        # implementation/routing/src/routing_manager_client.cpp.
        # target_compile_options are appended after CMAKE_CXX_FLAGS, so
        # -Wno-error wins. Warnings stay visible, they just no longer abort.
        target_compile_options(vsomeip3 PRIVATE -fpermissive -Wno-error)
    endif()
endif()

# Source libraries
add_subdirectory(src)

# Demo applications
add_subdirectory(apps)
