# Copyright 2024
# Licensed under the Apache License, Version 2.0

# CAPI Communication Library

cmake_minimum_required(VERSION 3.16)

project(com VERSION 1.0.0 LANGUAGES CXX)

# Options
option(COM_WITH_VSOMEIP "Build with vsomeip support" ON)
option(COM_BUILD_EXAMPLES "Build com examples" OFF)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(COM_SOURCES
    src/runtime.cpp
)

# Header files
set(COM_HEADERS
    include/com/types.h
    include/com/com_error_domain.h
    include/com/bind_runtime.h
    include/com/runtime.h
    include/com/proxy.h
    include/com/skeleton.h
)

# vsomeip binding sources
if(COM_WITH_VSOMEIP)
    list(APPEND COM_SOURCES
        src/vsomeip/vsomeip_bind_runtime.cpp
        src/vsomeip/vsomeip_bind_handle.cpp
        src/vsomeip/vsomeip_bind_skeleton.cpp
    )
    list(APPEND COM_HEADERS
        include/com/vsomeip/vsomeip_bind_runtime.h
        include/com/vsomeip/vsomeip_bind_handle.h
        include/com/vsomeip/vsomeip_bind_skeleton.h
    )
endif()

# Create library
add_library(com ${COM_SOURCES} ${COM_HEADERS})
# ara_core provides the ara/core/* + phsl/* headers and their few out-of-line
# symbols (ara::core::Abort, phsl::bad_variant_access, ...). It also exports the
# ara-core include directory publicly, so the BUILD_INTERFACE path below is only
# kept as a fallback for standalone builds of this sub-project.
if(TARGET ara_core)
    target_link_libraries(com PUBLIC ara_core)
endif()

# Include directories
target_include_directories(com
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../common/ara-core/include>
        $<INSTALL_INTERFACE:include>
)

# Link dependencies. Prefer the in-project vsomeip3 target, otherwise fall back
# to an installed CMake package or pkg-config. Fail loudly instead of silently
# building the vsomeip binding without its dependency.
if(COM_WITH_VSOMEIP)
    if(TARGET vsomeip3)
        target_link_libraries(com PUBLIC vsomeip3)
    else()
        find_package(vsomeip3 QUIET)
        if(vsomeip3_FOUND)
            target_link_libraries(com PUBLIC vsomeip3::vsomeip3)
        else()
            find_package(PkgConfig QUIET)
            if(PKG_CONFIG_FOUND)
                pkg_check_modules(VSOMEIP vsomeip3)
            endif()
            if(VSOMEIP_FOUND)
                target_link_libraries(com PUBLIC ${VSOMEIP_LIBRARIES})
                target_include_directories(com PUBLIC ${VSOMEIP_INCLUDE_DIRS})
            else()
                message(FATAL_ERROR
                    "COM_WITH_VSOMEIP=ON but vsomeip3 was not found "
                    "(no in-project target, CMake package, or pkg-config module).")
            endif()
        endif()
    endif()
    target_compile_definitions(com PUBLIC COM_WITH_VSOMEIP)
endif()

# Examples
if(COM_BUILD_EXAMPLES AND COM_WITH_VSOMEIP)
    add_subdirectory(examples)
endif()

