cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sensor-hub)

target_sources(app PRIVATE src/main.cpp)

add_subdirectory(src/device_status)
add_subdirectory(src/ui)
add_subdirectory(drivers/sensor/mq)
target_link_libraries(app PRIVATE ui_manager device_status mq)