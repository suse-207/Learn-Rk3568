# Copyright 2024
# Licensed under the Apache License, Version 2.0

# CAPI Communication Library

cmake_minimum_required(VERSION 3.16)

project(com VERSION 1.0.0 LANGUAGES CXX)

# Options
option(COM_WITH_VSOMEIP "Build with vsomeip support" ON)
option(COM_BUILD_TESTS "Build tests" OFF)

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

# Include directories
target_include_directories(com
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Link dependencies
if(COM_WITH_VSOMEIP)
    # Check if vsomeip3 target exists (built as part of this project)
    if(TARGET vsomeip3)
        target_link_libraries(com PUBLIC vsomeip3)
        target_compile_definitions(com PUBLIC COM_WITH_VSOMEIP)
    else()
        # Try to find vsomeip as installed package
        find_package(vsomeip3 QUIET)
        if(vsomeip3_FOUND)
            target_link_libraries(com PUBLIC vsomeip3::vsomeip3)
            target_compile_definitions(com PUBLIC COM_WITH_VSOMEIP)
        else()
            # Try pkg-config
            find_package(PkgConfig QUIET)
            if(PKG_CONFIG_FOUND)
                pkg_check_modules(VSOMEIP QUIET vsomeip3)
                if(VSOMEIP_FOUND)
                    target_link_libraries(com PUBLIC ${VSOMEIP_LIBRARIES})
                    target_include_directories(com PUBLIC ${VSOMEIP_INCLUDE_DIRS})
                    target_compile_definitions(com PUBLIC COM_WITH_VSOMEIP)
                endif()
            endif()
        endif()
    endif()
endif()

# Install
install(TARGETS com
    EXPORT comTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/com
    DESTINATION include
)

# Export
install(EXPORT comTargets
    FILE comTargets.cmake
    NAMESPACE com::
    DESTINATION lib/cmake/com
)

# Config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/comConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/comConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/comConfig.cmake"
    @ONLY
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/comConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/comConfigVersion.cmake"
    DESTINATION lib/cmake/com
)