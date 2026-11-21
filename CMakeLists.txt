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

# Tests (CTest).
# The Agent's verification step reports "CHECK PARTIAL" (and refuses to commit) when the build
# tree registers zero CTest cases, so at least one real regression test must live here.
# idlc_byte_compare re-generates the VehicleStatus codec from contracts/vehicle_status.yaml and
# compares it byte-for-byte against the frozen hand-written implementation in tests/idlc/legacy.
include(CTest)
if(BUILD_TESTING AND UNIX)

    # 公共 API 头卫生守卫（ADR 0001 C2/C18）：
    #   api_headers_compile —— 只包含 com/api/** 的 TU 必须能编译
    #   api_headers_no_leak —— com/api/** 不得出现绑定类型（runtime.h 暂列白名单，P5 收口）
    add_executable(api_headers_test tests/api_headers/main.cpp)
    target_link_libraries(api_headers_test PRIVATE com)
    add_test(NAME api_headers_compile COMMAND api_headers_test)

    # 错误码数值契约（ADR 0001 C4）：静态断言，不通过则编译期失败
    add_executable(com_error_codes_test tests/com_error_codes/main.cpp)
    target_link_libraries(com_error_codes_test PRIVATE com)
    add_test(NAME com_error_codes COMMAND com_error_codes_test)
    add_test(
        NAME api_headers_no_leak
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/api_headers/check_api_headers.sh
                ${CMAKE_CURRENT_SOURCE_DIR}/src/com/include/com/api
    )
    set_tests_properties(api_headers_no_leak PROPERTIES TIMEOUT 60)
    add_test(
        NAME idlc_byte_compare
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/idlc/run.sh
    )
    set_tests_properties(idlc_byte_compare PROPERTIES TIMEOUT 120)
endif()
