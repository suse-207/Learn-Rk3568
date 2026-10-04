# Standalone demo applications (no CAPI dependencies)

# DoIP server demo
add_executable(doip_server doip_server/main.cpp)
target_link_libraries(doip_server PRIVATE platform_core)

# OTA demo
add_executable(ota_demo ota_demo/main.cpp)
target_link_libraries(ota_demo PRIVATE platform_core)

# UCM-style OTA demo (in-process: HTTP -> TransferData -> ProcessSwPackage -> A/B switch)
add_executable(ucm_ota_demo ucm_ota_demo/main.cpp)
target_link_libraries(ucm_ota_demo PRIVATE platform_core)

# UCM service demo (SOME-IP TransferData server)
add_executable(ucm_service ucm_service/main.cpp)
target_link_libraries(ucm_service PRIVATE ucm)

# UCM client demo (HTTP download + SOME-IP TransferData)
add_executable(ucm_client ucm_client/main.cpp)
target_link_libraries(ucm_client PRIVATE ucm)

# SOME/IP server demo (using vsomeip binding)
add_executable(someip_server someip_server/main.cpp)
target_link_libraries(someip_server PRIVATE platform_core com)

# SOME/IP client demo (using vsomeip binding)
add_executable(someip_client someip_client/main.cpp)
target_link_libraries(someip_client PRIVATE platform_core com)
