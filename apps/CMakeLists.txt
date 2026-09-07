add_executable(doip_server doip_server/main.cpp)
target_link_libraries(doip_server PRIVATE platform_core)

add_executable(ota_demo ota_demo/main.cpp)
target_link_libraries(ota_demo PRIVATE platform_core)

add_executable(someip_server someip_server/main.cpp)
target_link_libraries(someip_server PRIVATE platform_core vsomeip3)

add_executable(someip_client someip_client/main.cpp)
target_link_libraries(someip_client PRIVATE platform_core vsomeip3)

add_executable(capi_diag_server capi_diag_server/main.cpp)
target_link_libraries(capi_diag_server PRIVATE
    port_doip
    port_uds
    port_compat
    port_core_types
    port_diag_common
    port_isoft_serialize
)
