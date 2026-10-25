# ==================================================
# RGC - Rise Global Chat
# CMakeLists v4 - IXWebSocket (WSS/TLS support)
# ==================================================

cmake_minimum_required(VERSION 3.15)
project(RiseGlobalChat C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Path ke header REFramework ---
set(REF_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../backup/reframework-src/include")
set(IMGUI_CORE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/../../backup/reframework-src/dependencies/imgui")
set(EASYWS_DIR      "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")

if(NOT EXISTS "${REF_INCLUDE_DIR}/reframework/API.hpp")
    message(FATAL_ERROR "API.hpp tidak ditemukan di: ${REF_INCLUDE_DIR}/reframework/API.hpp")
endif()

if(NOT EXISTS "${IMGUI_CORE_DIR}/imgui.h")
    message(FATAL_ERROR "imgui.h tidak ditemukan di: ${IMGUI_CORE_DIR}/imgui.h\n"
                        "Pastikan folder backup/reframework-src ada dan dependencies/imgui terisi")
endif()

if(NOT EXISTS "${EASYWS_DIR}/json.hpp")
    message(FATAL_ERROR "json.hpp tidak ditemukan di thirdparty/")
endif()

set(MINHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/minhook")
if(NOT EXISTS "${MINHOOK_DIR}/include/MinHook.h")
    message(FATAL_ERROR "MinHook.h tidak ditemukan di ${MINHOOK_DIR}/include\n"
                        "Download dari: https://github.com/TsudaKageyu/minhook/releases")
endif()

message(STATUS "MinHook dir            : ${MINHOOK_DIR}")
message(STATUS "REFramework include dir: ${REF_INCLUDE_DIR}")
message(STATUS "ImGui core dir        : ${IMGUI_CORE_DIR}")
message(STATUS "thirdparty dir        : ${EASYWS_DIR}")

# --- vcpkg toolchain (fallback kalau tidak di-set via command line) ---
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
endif()

# --- IXWebSocket + OpenSSL (pengganti easywsclient) ---
find_package(ixwebsocket CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# MinHook dikompilasi sebagai C, warning MSVC tertentu aman diabaikan
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

# --- Library plugin (SHARED = DLL) ---
add_library(RiseGlobalChat SHARED
    dllmain.cpp

    # MinHook
    "${MINHOOK_DIR}/src/hook.c"
    "${MINHOOK_DIR}/src/trampoline.c"
    "${MINHOOK_DIR}/src/buffer.c"
    "${MINHOOK_DIR}/src/hde/hde64.c"
    "${MINHOOK_DIR}/src/hde/hde32.c"

    # ImGui core (dari dependencies REFramework)
    "${IMGUI_CORE_DIR}/imgui.cpp"
    "${IMGUI_CORE_DIR}/imgui_draw.cpp"
    "${IMGUI_CORE_DIR}/imgui_tables.cpp"
    "${IMGUI_CORE_DIR}/imgui_widgets.cpp"

    # ImGui backends (dari example_plugin, sudah dicopy ke imgui/)
    imgui/imgui_impl_dx11.cpp
    imgui/imgui_impl_dx12.cpp
    imgui/imgui_impl_win32.cpp

    # Rendering helpers (dari example_plugin, sudah dicopy ke rendering/)
    rendering/d3d11.cpp
    rendering/d3d12.cpp
)

target_include_directories(RiseGlobalChat PRIVATE
    ${REF_INCLUDE_DIR}
    ${IMGUI_CORE_DIR}
    ${EASYWS_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/rendering
    ${MINHOOK_DIR}/include
    ${MINHOOK_DIR}/src
    ${MINHOOK_DIR}/src/hde
)

# Static runtime (sama seperti REFramework)
set_property(TARGET RiseGlobalChat PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Link library
target_link_libraries(RiseGlobalChat PRIVATE
    user32
    kernel32
    ws2_32
    d3d11
    d3d12
    dxgi
    dinput8
    dxguid
    bcrypt

    ixwebsocket::ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Output
set_target_properties(RiseGlobalChat PROPERTIES
    PREFIX ""
    OUTPUT_NAME "RiseGlobalChat"
)