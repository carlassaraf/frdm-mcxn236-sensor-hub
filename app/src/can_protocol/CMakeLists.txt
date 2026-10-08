add_library(can_protocol can_protocol.c)
target_include_directories(can_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(can_protocol PRIVATE zephyr_interface kernel)
