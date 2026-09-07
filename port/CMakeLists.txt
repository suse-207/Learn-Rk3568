# CAPI isoft-doip / isoft-uds port.
#
# The port deliberately does NOT pull in the full AUTOSAR NAI library.
# It supplies a small local compatibility layer (port/include + port/src)
# implementing only the NAI/naicpp/ara-log surface used by the two diag libs.

set(CAPI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/upstream/capi)
set(PORT_INC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# ara::core-types (headers + 3 small translation units from CAPI)
# ---------------------------------------------------------------------------
add_library(port_core_types STATIC
    ${CAPI_ROOT}/core-types/src/abort.cpp
    ${CAPI_ROOT}/core-types/src/initialize.cpp
    ${CAPI_ROOT}/core-types/src/phsl/types/bad_variant_access.cc
)
target_include_directories(port_core_types PUBLIC
    ${CAPI_ROOT}/core-types/include
)
target_link_libraries(port_core_types PUBLIC Threads::Threads)

# isoft/common is header-only in CAPI
add_library(port_isoft_common INTERFACE)
target_include_directories(port_isoft_common INTERFACE ${CAPI_ROOT}/common/include)

# diag/common headers (serialization + thread/therad_safe), NAI-free usage only
add_library(port_diag_common INTERFACE)
target_include_directories(port_diag_common INTERFACE ${CAPI_ROOT}/diag-common/include)

# isoft/serialize is header-only in CAPI
add_library(port_isoft_serialize INTERFACE)
target_include_directories(port_isoft_serialize INTERFACE ${CAPI_ROOT}/serialize/include)

# ---------------------------------------------------------------------------
# Local compatibility layer: minimal NAI shim, naicpp EvLoop, ara/log, thread pool
# ---------------------------------------------------------------------------
add_library(port_compat STATIC
    src/log.cpp
    src/evloop.cpp
    src/nai_shim.cpp
    src/port_loop.cpp
    src/thread_pool.cpp
)
target_include_directories(port_compat PUBLIC
    ${PORT_INC}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(port_compat PUBLIC
    port_core_types
    port_isoft_common
)

# ---------------------------------------------------------------------------
# isoft-doip (common + server)
# ---------------------------------------------------------------------------
file(GLOB DOIP_COMMON_SRC ${CAPI_ROOT}/isoft-doip/src/isoft-doip-common/src/*.cpp)
file(GLOB DOIP_SERVER_SRC ${CAPI_ROOT}/isoft-doip/src/isoft-doip-server/src/*.cpp)

add_library(port_doip STATIC ${DOIP_COMMON_SRC} ${DOIP_SERVER_SRC})
target_include_directories(port_doip PUBLIC
    ${CAPI_ROOT}/isoft-doip/src/isoft-doip-common/include
    ${CAPI_ROOT}/isoft-doip/src/isoft-doip-server/include
PRIVATE
    ${CAPI_ROOT}/isoft-doip/src/isoft-doip-common/src
    ${CAPI_ROOT}/isoft-doip/src/isoft-doip-server/src
)
target_link_libraries(port_doip PUBLIC
    port_compat
    port_core_types
    port_isoft_common
    port_diag_common
)
target_compile_definitions(port_doip PRIVATE _GNU_SOURCE)
target_compile_options(port_doip PRIVATE -include isoft/define.h)

# ---------------------------------------------------------------------------
# isoft-uds server (DCM; client directory is intentionally empty in CAPI)
# ---------------------------------------------------------------------------
set(UDS_SRC_DIR ${CAPI_ROOT}/isoft-uds/src/server/src)
file(GLOB UDS_FAULT_SRC ${UDS_SRC_DIR}/fault_management/*.cpp)

add_library(port_uds STATIC
    ${UDS_FAULT_SRC}
    ${UDS_SRC_DIR}/authentication_management/authentication_manager.cpp
    ${UDS_SRC_DIR}/authentication_management/client_authentication.cpp
    ${UDS_SRC_DIR}/authentication_management/client_authentication_controller.cpp
    ${UDS_SRC_DIR}/authentication_management/service_x29.cpp
    ${UDS_SRC_DIR}/authentication_management/client_authentication_controller_impl.cpp
    ${UDS_SRC_DIR}/common/general_service.cpp
    ${UDS_SRC_DIR}/common/timer.cpp
    ${UDS_SRC_DIR}/common/request_context.cpp
    ${UDS_SRC_DIR}/common/log.cpp
    ${UDS_SRC_DIR}/common/persistence_service.cpp
    ${UDS_SRC_DIR}/session_management/service_x10.cpp
    ${UDS_SRC_DIR}/session_management/session_manager.cpp
    ${UDS_SRC_DIR}/transfer_managment/service_x34.cpp
    ${UDS_SRC_DIR}/transfer_managment/service_x35.cpp
    ${UDS_SRC_DIR}/transfer_managment/service_x36.cpp
    ${UDS_SRC_DIR}/transfer_managment/service_x37.cpp
    ${UDS_SRC_DIR}/transfer_managment/service_x38.cpp
    ${UDS_SRC_DIR}/transfer_managment/transfer_manager.cpp
    ${UDS_SRC_DIR}/custom_management/service_custom.cpp
    ${UDS_SRC_DIR}/roe_management/diagnostic_roe_manager.cpp
    ${UDS_SRC_DIR}/roe_management/service_x86.cpp
    ${UDS_SRC_DIR}/com_management/service_x28.cpp
    ${UDS_SRC_DIR}/ecu_reset_management/service_x11.cpp
    ${UDS_SRC_DIR}/data_management/service_x2c.cpp
    ${UDS_SRC_DIR}/data_management/service_x2e.cpp
    ${UDS_SRC_DIR}/data_management/service_x2a.cpp
    ${UDS_SRC_DIR}/data_management/service_x22.cpp
    ${UDS_SRC_DIR}/data_management/environmental_condition.cpp
    ${UDS_SRC_DIR}/data_management/diagnostic_data_manager.cpp
    ${UDS_SRC_DIR}/data_management/environmental_condition_manager.cpp
    ${UDS_SRC_DIR}/routine_management/service_x31.cpp
    ${UDS_SRC_DIR}/security_access_management/security_level_manager.cpp
    ${UDS_SRC_DIR}/security_access_management/service_x27.cpp
    ${UDS_SRC_DIR}/cancellation_handler.cpp
    ${UDS_SRC_DIR}/cancellation_state.cpp
    ${UDS_SRC_DIR}/server.cpp
    ${UDS_SRC_DIR}/server_manager.cpp
    ${UDS_SRC_DIR}/server_impl.cpp
    ${UDS_SRC_DIR}/message.cpp
    ${UDS_SRC_DIR}/service_validation_service.cpp
)

target_include_directories(port_uds PUBLIC
    ${CAPI_ROOT}/isoft-uds/src/server/include
PRIVATE
    ${CAPI_ROOT}/isoft-uds/src/server/include
    ${UDS_SRC_DIR}
    ${UDS_SRC_DIR}/common
)
target_link_libraries(port_uds PUBLIC
    port_compat
    port_core_types
    port_isoft_common
    port_diag_common
    port_isoft_serialize
)
target_compile_definitions(port_uds PRIVATE _GNU_SOURCE)
target_compile_options(port_uds PRIVATE -include isoft/define.h)
