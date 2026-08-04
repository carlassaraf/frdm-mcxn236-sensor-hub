add_library(ui_adapters
  scr_overview.c
  scr_splash.c
)

target_include_directories(ui_adapters PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ui_adapters PUBLIC device_status ui)