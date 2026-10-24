cmake_minimum_required(VERSION 3.20)
project(Launcher LANGUAGES CXX)

add_executable(Launcher WIN32 src/main.cpp src/process_manager.cpp src/feature_controller.cpp)
target_compile_features(Launcher PRIVATE cxx_std_17)
target_compile_definitions(Launcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(Launcher PRIVATE user32 gdi32 gdiplus dwmapi winmm)
if(MSVC)
  target_compile_options(Launcher PRIVATE /W4 /permissive-)
  set_property(TARGET Launcher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_custom_command(TARGET Launcher POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons"
    "$<TARGET_FILE_DIR:Launcher>/assets/icons"
)
