cmake_minimum_required(VERSION 3.16)
project(rk3568-automotive-platform VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(WITH_VSOMEIP "Build with vsomeip support" ON)

# vsomeip third party
if(WITH_VSOMEIP)
    add_subdirectory(third_party/vsomeip EXCLUDE_FROM_ALL)
    if(TARGET vsomeip3)
        target_compile_options(vsomeip3 PRIVATE -fpermissive)
    endif()
endif()

# Source libraries
add_subdirectory(src)

# Demo applications
add_subdirectory(apps)
