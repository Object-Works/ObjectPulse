cmake_minimum_required(VERSION 3.20)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" OP_VERSION LIMIT_COUNT 1)
project(ObjectPulse VERSION ${OP_VERSION} LANGUAGES CXX RC)
configure_file(src/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h" @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ObjectPulse WIN32
    src/main.cpp
    src/common.cpp
    src/config.cpp
    src/frame_link.cpp
    src/renderer.cpp
    src/views.cpp
    src/taskbar.cpp
    src/pulse.cpp
    src/menu.cpp
    src/corners.cpp
    src/controls.cpp
    src/sides.cpp
    src/app.cpp
    src/app.manifest
    src/pulse.rc
)

target_compile_definitions(ObjectPulse PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(ObjectPulse PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

if(MSVC)
    target_compile_options(ObjectPulse PRIVATE /W4 /utf-8 /permissive-)
    set_property(TARGET ObjectPulse PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(ObjectPulse PRIVATE d2d1 dwrite windowscodecs ole32 oleaut32 shell32 user32 gdi32 shcore winmm dwmapi version uuid dxva2 wbemuuid)
