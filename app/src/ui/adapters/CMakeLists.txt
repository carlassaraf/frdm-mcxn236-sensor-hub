add_library(ui_adapters
  scr_can.c
  scr_environment.c
  scr_overview.c
  scr_splash.c
  scr_tilt.c
)

target_include_directories(ui_adapters PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ui_adapters PUBLIC device_status ui)