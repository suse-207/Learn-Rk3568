file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/doip/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/uds/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ota/*.cpp")
list(FILTER CORE_SRC EXCLUDE REGEX "common/capi|common/compat|doip/capi|uds/capi")

set(CAPI_COMMON_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/common/capi)
set(CAPI_DOIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/doip/capi/isoft-doip)
set(CAPI_UDS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/uds/capi/isoft-uds)
set(CAPI_COMPAT_INC ${CMAKE_CURRENT_SOURCE_DIR}/common/compat/include)
set(CAPI_COMPAT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/common/compat/src)

set(CAPI_CORE_SRC
    ${CAPI_COMMON_ROOT}/core-types/src/abort.cpp
    ${CAPI_COMMON_ROOT}/core-types/src/initialize.cpp
    ${CAPI_COMMON_ROOT}/core-types/src/phsl/types/bad_variant_access.cc
)
file(GLOB CAPI_COMPAT_CPP ${CAPI_COMPAT_SRC}/*.cpp)
file(GLOB_RECURSE CAPI_DOIP_SRC ${CAPI_DOIP_ROOT}/src/*.cpp)
file(GLOB_RECURSE CAPI_UDS_SRC ${CAPI_UDS_ROOT}/src/server/src/*.cpp)

add_library(platform_core STATIC
    ${CORE_SRC}
)
target_include_directories(platform_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(platform_core PUBLIC Threads::Threads OpenSSL::Crypto)

add_library(capi_common STATIC
    ${CAPI_CORE_SRC}
    ${CAPI_COMPAT_CPP}
)
target_include_directories(capi_common PUBLIC
    ${CAPI_COMPAT_INC}
    ${CAPI_COMMON_ROOT}/core-types/include
    ${CAPI_COMMON_ROOT}/common/include
    ${CAPI_COMMON_ROOT}/diag-common/include
    ${CAPI_COMMON_ROOT}/serialize/include
)
target_include_directories(capi_common PRIVATE
    ${CAPI_COMPAT_SRC}
)
target_link_libraries(capi_common PUBLIC Threads::Threads)

add_library(capi_doip STATIC ${CAPI_DOIP_SRC})
target_include_directories(capi_doip PUBLIC
    ${CAPI_DOIP_ROOT}/src/isoft-doip-common/include
    ${CAPI_DOIP_ROOT}/src/isoft-doip-server/include
)
target_include_directories(capi_doip PRIVATE
    ${CAPI_DOIP_ROOT}/src/isoft-doip-common/src
    ${CAPI_DOIP_ROOT}/src/isoft-doip-server/src
)
target_link_libraries(capi_doip PUBLIC capi_common)
target_compile_definitions(capi_doip PRIVATE _GNU_SOURCE)
set_source_files_properties(${CAPI_DOIP_SRC}
    PROPERTIES COMPILE_OPTIONS "-include;isoft/define.h")

add_library(capi_uds STATIC ${CAPI_UDS_SRC})
target_include_directories(capi_uds PUBLIC
    ${CAPI_UDS_ROOT}/src/server/include
)
target_include_directories(capi_uds PRIVATE
    ${CAPI_UDS_ROOT}/src/server/src
    ${CAPI_UDS_ROOT}/src/server/src/common
)
target_link_libraries(capi_uds PUBLIC capi_common)
target_compile_definitions(capi_uds PRIVATE _GNU_SOURCE)
set_source_files_properties(${CAPI_UDS_SRC}
    PROPERTIES COMPILE_OPTIONS "-include;isoft/define.h")
