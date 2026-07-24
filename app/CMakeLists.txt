cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sensor-hub)

target_sources(app PRIVATE src/main.cpp)

add_subdirectory(src/ui)
target_include_directories(ui PUBLIC src/ui)
target_link_libraries(ui PUBLIC LVGL)
target_link_libraries(app PRIVATE ui)