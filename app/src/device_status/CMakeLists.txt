add_library(device_status device_status.c)
target_include_directories(device_status PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(device_status PRIVATE zephyr_interface)

if(CONFIG_TILT_SIM)
  target_sources(device_status PRIVATE tilt_sim.c)
endif()
