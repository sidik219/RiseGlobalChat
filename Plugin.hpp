#pragma once

#include "reframework/API.hpp"

// Dibutuhkan oleh rendering/d3d11.cpp dan rendering/d3d12.cpp
// (keduanya include "../Plugin.hpp"). Definisi g_lua ada di dllmain.cpp.
extern lua_State* g_lua;