file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/doip/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/uds/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ota/*.cpp")

add_library(platform_core STATIC ${CORE_SRC})
target_include_directories(platform_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(platform_core PUBLIC Threads::Threads OpenSSL::Crypto)
