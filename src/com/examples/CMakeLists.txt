# Copyright 2024
# Licensed under the Apache License, Version 2.0

# com examples

add_executable(vsomeip_service_example vsomeip_service_example.cpp)
target_link_libraries(vsomeip_service_example PRIVATE com)

add_executable(vsomeip_client_example vsomeip_client_example.cpp)
target_link_libraries(vsomeip_client_example PRIVATE com)
