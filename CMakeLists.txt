cmake_minimum_required(VERSION 3.15)
project(WirelessAudioSimulator VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler flags for performance and warnings
if(MSVC)
    add_compile_options(/W4 /WX-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options(-O2 -g)  # Optimize with debug symbols
endif()

# Find threads library
find_package(Threads REQUIRED)

# Common protocol library
add_library(protocol STATIC
    common/src/protocol.cpp
)
target_include_directories(protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common/include
)

# Accessory simulator (embedded side)
add_executable(accessory_simulator
    accessory/src/main.cpp
    accessory/src/connection_fsm.cpp
    accessory/src/audio_streamer.cpp
    accessory/src/crypto.cpp
    accessory/src/telemetry.cpp
    accessory/src/transport.cpp
)
target_include_directories(accessory_simulator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/accessory/include
)
target_link_libraries(accessory_simulator PRIVATE
    protocol
    Threads::Threads
)

# Host daemon
add_executable(host_daemon
    host/src/main.cpp
    host/src/device_manager.cpp
    host/src/audio_sync.cpp
    host/src/telemetry_processor.cpp
    host/src/transport.cpp
)
target_include_directories(host_daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/host/include
)
target_link_libraries(host_daemon PRIVATE
    protocol
    Threads::Threads
)

# Install targets
install(TARGETS accessory_simulator host_daemon
    RUNTIME DESTINATION bin
)

# Documentation
install(FILES README.md DESTINATION share/doc/wireless_audio_simulator)
install(DIRECTORY docs/ DESTINATION share/doc/wireless_audio_simulator/docs)

# Print build configuration
message(STATUS "=== Wireless Audio Accessory Simulator ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
