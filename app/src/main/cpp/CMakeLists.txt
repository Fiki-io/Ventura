cmake_minimum_required(VERSION 3.22.1)
project("ventura_core" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(
    ventura_core
    SHARED
    core/memory_arena.cpp
    loader/pe32_loader.cpp
    aot/x86_aot_engine.cpp
    bridge/win32_bridge.cpp
    bridge/d3d9_surface_renderer.cpp
    bridge/winsock_samp_net.cpp
    archive/img_ver2_reader.cpp
    jni/ventura_jni.cpp
)

target_compile_options(
    ventura_core
    PRIVATE
    -O3
    -ffast-math
    -Wall
    -Wextra
    -Wno-unused-parameter
)

find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(gles3-lib GLESv3)

target_link_libraries(
    ventura_core
    ${log-lib}
    ${android-lib}
    ${egl-lib}
    ${gles3-lib}
)
