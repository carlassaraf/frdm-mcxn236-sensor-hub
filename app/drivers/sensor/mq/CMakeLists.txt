add_library(mq mq.c)
target_include_directories(mq PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mq PRIVATE zephyr_interface)