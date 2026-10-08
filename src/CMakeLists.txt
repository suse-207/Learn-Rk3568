file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/doip/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/uds/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ota/*.cpp")
# NOTE: "common/ara-core" MUST stay excluded. Its sources are compiled into the
#       ara_core target below; without this filter the GLOB_RECURSE over
#       common/*.cpp|*.cc would pull them into platform_core as well and cause
#       duplicate symbol definitions at link time.
list(FILTER CORE_SRC EXCLUDE REGEX "common/(ara-core|capi|compat)|doip/capi|uds/capi")

# ara::core / phsl core types (AUTOSAR reference implementation), required by
# the ara::com-style communication layer in src/com.
set(ARA_CORE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/common/ara-core)
set(ARA_CORE_SRC
    ${ARA_CORE_ROOT}/src/abort.cpp
    ${ARA_CORE_ROOT}/src/initialize.cpp
    ${ARA_CORE_ROOT}/src/phsl/types/bad_variant_access.cc
)

add_library(platform_core STATIC
    ${CORE_SRC}
)
target_include_directories(platform_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
target_link_libraries(platform_core PUBLIC Threads::Threads OpenSSL::Crypto CURL::libcurl)

add_library(ara_core STATIC ${ARA_CORE_SRC})
target_include_directories(ara_core PUBLIC
    ${ARA_CORE_ROOT}/include
)
target_link_libraries(ara_core PUBLIC Threads::Threads)
set_target_properties(ara_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The CAPI-derived diagnostic libraries are kept separate from the native
# DoIP/UDS stack. Their public headers remain available to the legacy demo.
set(CAPI_COMMON_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/common/capi)
set(CAPI_DOIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/doip/capi/isoft-doip)
set(CAPI_UDS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/uds/capi/isoft-uds)
set(CAPI_COMPAT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/common/compat)

file(GLOB CAPI_COMPAT_SRC CONFIGURE_DEPENDS
    "${CAPI_COMPAT_ROOT}/src/*.cpp")
file(GLOB_RECURSE CAPI_DOIP_SRC CONFIGURE_DEPENDS
    "${CAPI_DOIP_ROOT}/src/*.cpp")
file(GLOB_RECURSE CAPI_UDS_SRC CONFIGURE_DEPENDS
    "${CAPI_UDS_ROOT}/src/server/src/*.cpp")

add_library(capi_common STATIC ${CAPI_COMPAT_SRC})
target_include_directories(capi_common
    PUBLIC
        ${CAPI_COMPAT_ROOT}/include
        ${ARA_CORE_ROOT}/include
        ${CAPI_COMMON_ROOT}/core-types/include
        ${CAPI_COMMON_ROOT}/common/include
        ${CAPI_COMMON_ROOT}/diag-common/include
        ${CAPI_COMMON_ROOT}/serialize/include
    PRIVATE
        ${CAPI_COMPAT_ROOT}/src
)
target_link_libraries(capi_common PUBLIC ara_core Threads::Threads)

add_library(capi_doip STATIC ${CAPI_DOIP_SRC})
target_include_directories(capi_doip
    PUBLIC
        ${CAPI_DOIP_ROOT}/src/isoft-doip-common/include
        ${CAPI_DOIP_ROOT}/src/isoft-doip-server/include
    PRIVATE
        ${CAPI_DOIP_ROOT}/src/isoft-doip-common/src
        ${CAPI_DOIP_ROOT}/src/isoft-doip-server/src
)
target_link_libraries(capi_doip PUBLIC capi_common)
target_compile_definitions(capi_doip PRIVATE _GNU_SOURCE)
set_source_files_properties(${CAPI_DOIP_SRC}
    PROPERTIES COMPILE_OPTIONS "-include;isoft/define.h")

add_library(capi_uds STATIC ${CAPI_UDS_SRC})
target_include_directories(capi_uds
    PUBLIC
        ${CAPI_UDS_ROOT}/src/server/include
    PRIVATE
        ${CAPI_UDS_ROOT}/src/server/src
        ${CAPI_UDS_ROOT}/src/server/src/common
)
target_link_libraries(capi_uds PUBLIC capi_common)
target_compile_definitions(capi_uds PRIVATE _GNU_SOURCE)
set_source_files_properties(${CAPI_UDS_SRC}
    PROPERTIES COMPILE_OPTIONS "-include;isoft/define.h")

# Communication layer (ara::com-style)
add_subdirectory(com)

# UCM transport adapter: SOME-IP TransferData over the com layer
add_library(ucm STATIC
    ucm/ucm_proxy.cpp
    ucm/ucm_service_skeleton.cpp
)
target_include_directories(ucm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ucm PUBLIC platform_core com)
