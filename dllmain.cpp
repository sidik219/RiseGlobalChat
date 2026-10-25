// ==================================================
// RGC - Rise Global Chat
// v0.6.1 - Input bleed fix + reconnect backoff
//
// Copyright (C) 2026 Sidik Mulyana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// Attribution: If you fork, modify, or distribute this software, you
// must retain this copyright notice and credit the original author.
// ==================================================
//
// Technical notes:
// - Uses IXWebSocket 2.x (WSS native) for networking
// - On Windows, IXWebSocket uses mbedTLS as TLS backend (not OpenSSL,
//   though OpenSSL is linked via vcpkg for compatibility)
// - Uses REFramework on_message callback (no RGC WndProc subclass)
// - Routes RGC hotkeys through on_message instead of GetAsyncKeyState polling
// - GetRawInputData/GetRawInputBuffer hooks: diagnostic logging + input
//   bleed suppression via RI_KEY_BREAK flag override during chat mode
// - Exponential backoff + jitter for reconnect (1s → 60s cap, 30s reset)
// - WebSocket hello sent only after is_open() to prevent silent drop
//
// Known limitation: REFramework itself cannot block keyboard input to
// MHRise (confirmed: WASD still moves when REF menu is open). The
// GetRawInputData flag override works because the game reads keyboard
// via that path, but does not guarantee blocking if the game reads
// input via an internal RE Engine path (snow.StmInputManager).
// ==================================================

// WAJIB sebelum <Windows.h>: cegah windows.h meng-include winsock.h lama
// (konflik dengan winsock2.h yang dibawa IXWebSocket)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <set>
#include <map>
#include <deque>
#include <memory>
#include <functional>
#include <Windows.h>
#include <reframework/API.hpp>

// IXWebSocket (WSS/TLS native via OpenSSL)
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketMessage.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include "json.hpp"
#include "MinHook.h"

// ImGui backends (from imgui/ copied from REFramework example_plugin)
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_impl_win32.h"

// Rendering helpers
#include "rendering/d3d11.hpp"
#include "rendering/d3d12.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace reframework;
using json = nlohmann::json;

// --- Constants ---
static const char* SERVER_URL = "wss://rgc-server.onrender.com";
static std::string g_player_name = "Hunter";
static const uint64_t WELCOME_TIMEOUT_FRAMES = 600;
static const uint64_t PING_INTERVAL_FRAMES = 600;
static const uint64_t PONG_TIMEOUT_FRAMES = 7200;
static const int64_t RECONNECT_BASE_DELAY_MS = 1000;
static const int64_t RECONNECT_MAX_DELAY_MS  = 60000;
static const int64_t RECONNECT_STABLE_RESET_SEC = 30;

// --- Chat entry ---
struct ChatEntry {
    std::string sender;
    std::string text;
    std::string timestamp;
    bool is_self{};
};

// --- Network state ---
enum class ConnState { Disconnected, Connecting, Connected, Closed };

// ==================================================
// WsClient - drop-in replacement untuk easywsclient
// API mirip: from_url / send / poll / dispatch
// IXWebSocket callback berjalan di thread internal,
// jadi message di-queue lalu di-dispatch ke main thread via poll().
// ==================================================
class WsClient {
public:
    using MessageCallback = std::function<void(const std::string&)>;

    static std::unique_ptr<WsClient> from_url(const std::string& url) {
        auto c = std::unique_ptr<WsClient>(new WsClient());
        c->ws_.setUrl(url);
        c->ws_.disableAutomaticReconnection(); // reconnect ditangani RGC sendiri
        c->ws_.setPingInterval(10);            // WS-level ping tiap 10 detik (IXWS sendiri)
        c->ws_.setOnMessageCallback(
            [ptr = c.get()](const ix::WebSocketMessagePtr& msg) {
                ptr->onIxMessage(msg);
            });
        c->ws_.start();
        return c;
    }

    void send(const std::string& s) {
        auto info = ws_.send(s);
        if (!info.success) {
            API::get()->log_warn("[RGC] IXWS send FAILED (wireSize=%u)", (unsigned)info.wireSize);
        }
    }

    // Dipanggil dari on_present (main thread): pindahkan inbox ke pending.
    void poll() {
        std::lock_guard<std::mutex> lock(mtx_);
        while (!inbox_.empty()) {
            pending_.push_back(std::move(inbox_.front()));
            inbox_.pop_front();
        }
    }

    // Dispatch pesan ke callback (tetap di main thread).
    void dispatch(const MessageCallback& cb) {
        while (true) {
            std::string msg;
            {
                std::lock_guard<std::mutex> lock(mtx_);
                if (pending_.empty()) break;
                msg = std::move(pending_.front());
                pending_.pop_front();
            }

            if (msg == "__OPEN__") {
                open_.store(true);
                closed_.store(false);
                API::get()->log_info("[RGC] WebSocket TCP+TLS handshake complete");
            } else if (msg == "__CLOSED__") {
                closed_.store(true);
                open_.store(false);
                API::get()->log_warn("[RGC] IXWebSocket CLOSED (server closed or network lost)");
            } else {
                cb(msg);
            }
        }
    }

    bool is_open() const { return open_.load() && !closed_.load(); }
    bool is_closed() const { return closed_.load(); }

    ~WsClient() { ws_.stop(); }

private:
    WsClient() = default;

    void onIxMessage(const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Message) {
            API::get()->log_info("[RGC] IXWS <- msg (len=%zu)", msg->str.size());
        } else if (msg->type == ix::WebSocketMessageType::Close) {
            API::get()->log_warn("[RGC] IXWS Close event (code=%u reason=%s)",
                msg->closeInfo.code, msg->closeInfo.reason.c_str());
        } else if (msg->type == ix::WebSocketMessageType::Error) {
            API::get()->log_error("[RGC] IXWS Error: %s", msg->errorInfo.reason.c_str());
        }
        std::lock_guard<std::mutex> lock(mtx_);
        if (msg->type == ix::WebSocketMessageType::Message) {
            inbox_.push_back(msg->str);
        } else if (msg->type == ix::WebSocketMessageType::Open) {
            inbox_.push_back("__OPEN__");
        } else if (msg->type == ix::WebSocketMessageType::Close ||
                   msg->type == ix::WebSocketMessageType::Error) {
            inbox_.push_back("__CLOSED__");
        }
    }

    ix::WebSocket ws_;
    std::mutex mtx_;
    std::deque<std::string> inbox_;
    std::deque<std::string> pending_;
    std::atomic_bool open_{false};
    std::atomic_bool closed_{false};
};

static std::unique_ptr<WsClient> g_ws = nullptr;
static ConnState g_state = ConnState::Disconnected;
static uint64_t g_frame_count = 0;
static uint64_t g_state_since_frame = 0;
static uint64_t g_last_ping_frame = 0;
static uint64_t g_last_pong_frame = 0;
static bool g_logged_first_frame = false;
static uint32_t g_welcome_count = 0;
static uint32_t g_pong_count = 0;
static bool g_hello_sent = false;
static int g_reconnect_attempt = 0;
static std::chrono::steady_clock::time_point g_next_reconnect_time;
static std::chrono::steady_clock::time_point g_connected_since_time;

// Channel state
static std::string g_current_channel = "global";
static std::vector<std::string> g_joined_channels = {"global"};
static std::set<std::string> g_owned_channels;
static std::map<std::string, std::vector<ChatEntry>> g_channel_logs;
static std::mutex g_channel_logs_mutex;

// UI state. Requests are atomics because on_message and on_present can run
// at different points in the game's frame/message lifecycle.
static bool g_ui_open = true;
static std::atomic_bool g_ui_initialized{false};
static char g_input_buf[256] = {0};
static char g_create_channel_buf[32] = {0};
static bool g_should_scroll = true;
static std::atomic_bool g_chat_mode{false};
static bool g_set_focus = false;
static std::atomic_bool g_open_chat_requested{false};
static std::atomic_bool g_close_chat_requested{false};
static std::atomic_bool g_toggle_ui_requested{false};
static std::atomic_bool g_clear_input_requested{false};
static std::atomic_bool g_suppress_slash_keyup{false};
static std::atomic_bool g_suppress_f8_keyup{false};
static std::atomic_bool g_suppress_esc_keyup{false};

// Used by the ImGui Win32 backend. RGC no longer subclasses the game's WndProc.
static HWND g_wnd = nullptr;

// g_lua is referenced by rendering/*.cpp through Plugin.hpp.
lua_State* g_lua = nullptr;

// ==================================================
// Raw Input diagnostics (logging only; data is never modified)
// ==================================================
using GetRawInputDataFn = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using GetRawInputBufferFn = UINT(WINAPI*)(PRAWINPUT, PUINT, UINT);

static GetRawInputDataFn g_original_GetRawInputData = nullptr;
static GetRawInputBufferFn g_original_GetRawInputBuffer = nullptr;
static void* g_raw_input_data_target = nullptr;
static void* g_raw_input_buffer_target = nullptr;
static std::atomic<uint32_t> g_raw_data_calls{0};
static std::atomic<uint32_t> g_raw_buffer_calls{0};
static std::atomic<uint32_t> g_raw_data_keyboard_records{0};
static std::atomic<uint32_t> g_raw_buffer_keyboard_records{0};
static bool g_raw_input_diag_installed = false;

static const char* raw_input_type_name(DWORD type) {
    switch (type) {
        case RIM_TYPEMOUSE: return "MOUSE";
        case RIM_TYPEKEYBOARD: return "KEYBOARD";
        case RIM_TYPEHID: return "HID";
        default: return "UNKNOWN";
    }
}

static UINT WINAPI Hooked_GetRawInputData(
    HRAWINPUT hRawInput,
    UINT uiCommand,
    LPVOID pData,
    PUINT pcbSize,
    UINT cbSizeHeader)
{
    if (g_original_GetRawInputData == nullptr) {
        return static_cast<UINT>(-1);
    }

    const UINT result = g_original_GetRawInputData(
        hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);

    const uint32_t call_no = g_raw_data_calls.fetch_add(1) + 1;
    if (call_no <= 8) {
        API::get()->log_info(
            "[RGC-DIAG] GetRawInputData hit #%u command=%u result=%u data=%p",
            call_no, uiCommand, result, pData);
    }

    if (uiCommand == RID_INPUT && pData != nullptr &&
        result != static_cast<UINT>(-1) && result >= sizeof(RAWINPUTHEADER)) {
        auto* raw = static_cast<RAWINPUT*>(pData);

        if (g_chat_mode.load() && raw->header.dwType == RIM_TYPEKEYBOARD) {
            raw->data.keyboard.Flags = RI_KEY_BREAK;
        } 

        if (raw->header.dwType == RIM_TYPEKEYBOARD) {
            const uint32_t keyboard_no = g_raw_data_keyboard_records.fetch_add(1) + 1;
            if (keyboard_no <= 40) {
                API::get()->log_info(
                    "[RGC-DIAG] GetRawInputData KEYBOARD #%u size=%u make=%u flags=0x%X vkey=%u message=0x%X",
                    keyboard_no,
                    raw->header.dwSize,
                    raw->data.keyboard.MakeCode,
                    raw->data.keyboard.Flags,
                    raw->data.keyboard.VKey,
                    raw->data.keyboard.Message);
            }
        }
    }

    return result;
}

static UINT WINAPI Hooked_GetRawInputBuffer(
    PRAWINPUT pData,
    PUINT pcbSize,
    UINT cbSizeHeader)
{
    if (g_original_GetRawInputBuffer == nullptr) {
        return static_cast<UINT>(-1);
    }

    const UINT result = g_original_GetRawInputBuffer(pData, pcbSize, cbSizeHeader);
    const uint32_t call_no = g_raw_buffer_calls.fetch_add(1) + 1;

    if (call_no <= 12) {
        API::get()->log_info(
            "[RGC-DIAG] GetRawInputBuffer hit #%u result=%u data=%p",
            call_no, result, pData);
    }

    // GetRawInputBuffer returns the number of records copied when pData is non-null.
    // Log keyboard records only; do not alter the buffer or the returned count.
    if (pData != nullptr && result != static_cast<UINT>(-1) && result > 0) {
        PRAWINPUT current = pData;
        const UINT max_records = (result > 256U) ? 256U : result;
        for (UINT i = 0; i < max_records; ++i) {
            if (current == nullptr || current->header.dwSize < sizeof(RAWINPUTHEADER)) {
                break;
            }

            if (g_chat_mode.load() && current->header.dwType == RIM_TYPEKEYBOARD) {
                current->data.keyboard.Flags = RI_KEY_BREAK;
            }

            if (current->header.dwType == RIM_TYPEKEYBOARD) {
                const uint32_t keyboard_no = g_raw_buffer_keyboard_records.fetch_add(1) + 1;
                if (keyboard_no <= 40) {
                    API::get()->log_info(
                        "[RGC-DIAG] GetRawInputBuffer KEYBOARD #%u size=%u make=%u flags=0x%X vkey=%u message=0x%X",
                        keyboard_no,
                        current->header.dwSize,
                        current->data.keyboard.MakeCode,
                        current->data.keyboard.Flags,
                        current->data.keyboard.VKey,
                        current->data.keyboard.Message);
                }
            }

            const uintptr_t next = reinterpret_cast<uintptr_t>(current) +
                ((static_cast<uintptr_t>(current->header.dwSize) + sizeof(DWORD) - 1U) &
                 ~(static_cast<uintptr_t>(sizeof(DWORD) - 1U)));
            current = reinterpret_cast<PRAWINPUT>(next);
        }
    }

    return result;
}

static bool create_enable_hook(void* target, void* detour, void** original, const char* label) {
    if (target == nullptr) {
        API::get()->log_warn("[RGC-DIAG] %s target not found", label);
        return false;
    }

    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
        API::get()->log_warn("[RGC-DIAG] MH_CreateHook(%s) failed: %d", label, static_cast<int>(status));
        return false;
    }

    status = MH_EnableHook(target);
    if (status != MH_OK && status != MH_ERROR_ENABLED) {
        API::get()->log_warn("[RGC-DIAG] MH_EnableHook(%s) failed: %d", label, static_cast<int>(status));
        return false;
    }

    API::get()->log_info("[RGC-DIAG] Hook installed: %s @ %p", label, target);
    return true;
}

static void install_raw_input_diagnostics() {
    if (g_raw_input_diag_installed) {
        return;
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        API::get()->log_warn("[RGC-DIAG] user32.dll unavailable; Raw Input diagnostics skipped");
        return;
    }

    const auto raw_data = reinterpret_cast<void*>(GetProcAddress(user32, "GetRawInputData"));
    const auto raw_buffer = reinterpret_cast<void*>(GetProcAddress(user32, "GetRawInputBuffer"));

    MH_STATUS mh = MH_Initialize();
    if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) {
        API::get()->log_warn("[RGC-DIAG] MH_Initialize failed: %d", static_cast<int>(mh));
        return;
    }

    const bool data_ok = create_enable_hook(
        raw_data, reinterpret_cast<void*>(&Hooked_GetRawInputData),
        reinterpret_cast<void**>(&g_original_GetRawInputData), "GetRawInputData");
    const bool buffer_ok = create_enable_hook(
        raw_buffer, reinterpret_cast<void*>(&Hooked_GetRawInputBuffer),
        reinterpret_cast<void**>(&g_original_GetRawInputBuffer), "GetRawInputBuffer");

    g_raw_input_data_target = raw_data;
    g_raw_input_buffer_target = raw_buffer;
    g_raw_input_diag_installed = data_ok || buffer_ok;

    API::get()->log_info(
        "[RGC-DIAG] Raw Input diagnostics active (data=%s, buffer=%s). Hooks only log; no input is blocked here.",
        data_ok ? "yes" : "no", buffer_ok ? "yes" : "no");
}

// ==================================================
// State helpers
// ==================================================
static const char* state_name(ConnState s) {
    switch (s) {
        case ConnState::Disconnected: return "Disconnected";
        case ConnState::Connecting:   return "Connecting";
        case ConnState::Connected:    return "Connected";
        case ConnState::Closed:       return "Closed";
    }
    return "?";
}

static std::chrono::milliseconds compute_reconnect_delay(int attempt) {
    // Exponential base: attempt 1 = 1s, 2 = 2s, 3 = 4s, ... cap 60s
    int64_t base_ms = RECONNECT_BASE_DELAY_MS;
    for (int i = 1; i < attempt; ++i) {
        base_ms *= 2;
        if (base_ms >= RECONNECT_MAX_DELAY_MS) {
            base_ms = RECONNECT_MAX_DELAY_MS;
            break;
        }
    }
    // Jitter: 50% - 150%
    int jitter_pct = 50 + (std::rand() % 101);   // 50 - 150
    int64_t final_ms = (base_ms * jitter_pct) / 100;
    if (final_ms > RECONNECT_MAX_DELAY_MS) final_ms = RECONNECT_MAX_DELAY_MS;
    if (final_ms < 100) final_ms = 100;          // minimum 100 ms
    return std::chrono::milliseconds(final_ms);
}

static void schedule_reconnect(const char* why) {
    ++g_reconnect_attempt;
    auto delay = compute_reconnect_delay(g_reconnect_attempt);
    g_next_reconnect_time = std::chrono::steady_clock::now() + delay;
    API::get()->log_info("[RGC] Reconnect scheduled: attempt #%d in %lld ms (%s)",
        g_reconnect_attempt, (long long)delay.count(), why);
}

static void set_state(ConnState s, const char* reason) {
    API::get()->log_info("[RGC] state: %s -> %s (%s)",
        state_name(g_state), state_name(s), reason);
    g_state = s;
    g_state_since_frame = g_frame_count;
}

static void try_connect() {
    API::get()->log_info("[RGC] Trying to connect to %s (attempt #%d)",
        SERVER_URL, g_reconnect_attempt);

    g_hello_sent = false;
    g_welcome_count = 0;
    g_pong_count = 0;
    g_last_ping_frame = g_frame_count;
    g_last_pong_frame = g_frame_count;

    g_ws = WsClient::from_url(SERVER_URL);
    if (g_ws == nullptr) {
        API::get()->log_warn("[RGC] from_url returned null");
        set_state(ConnState::Disconnected, "from_url_null");
        schedule_reconnect("from_url_null");
        return;
    }

    set_state(ConnState::Connecting, "ws_created");
}

static void drop_ws(const char* reason) {
    g_ws = nullptr;
    g_hello_sent = false;
    set_state(ConnState::Disconnected, reason);
    schedule_reconnect(reason);
}

// ==================================================
// Resolve player name: Steam persona name, fallback Windows username
// Dynamic load steam_api64.dll — tidak butuh .lib atau SDK
// ==================================================
static std::string resolve_player_name() {
    // --- Try Steam API first ---
    HMODULE steam = GetModuleHandleA("steam_api64.dll");
    if (!steam) steam = LoadLibraryA("steam_api64.dll");

    if (steam) {
        typedef void* (*SteamFriendsFn)();
        typedef const char* (*GetPersonaNameFn)(void*);

        // Export name bisa versi-spesifik (v018, v017) atau unversioned
        SteamFriendsFn pfnFriends = nullptr;
        for (const char* exp : {
                 "SteamAPI_SteamFriends_v018",
                 "SteamAPI_SteamFriends_v017",
                 "SteamAPI_SteamFriends"}) {
            pfnFriends = (SteamFriendsFn)GetProcAddress(steam, exp);
            if (pfnFriends) break;
        }

        auto pfnGetName = (GetPersonaNameFn)GetProcAddress(
            steam, "SteamAPI_ISteamFriends_GetPersonaName");

        if (pfnFriends && pfnGetName) {
            void* friends = pfnFriends();
            if (friends) {
                const char* raw = pfnGetName(friends);
                if (raw && raw[0]) {
                    std::string result(raw);
                    // Trim spasi depan-belakang
                    while (!result.empty() && (result.front() == ' ' || result.front() == '\t'))
                        result.erase(result.begin());
                    while (!result.empty() && (result.back() == ' ' || result.back() == '\t'))
                        result.pop_back();
                    if (!result.empty()) {
                        API::get()->log_info("[RGC] Steam name resolved: %s",
                                             result.c_str());
                        return result;
                    }
                }
            }
        }
        API::get()->log_warn("[RGC] Steam API available but GetPersonaName failed");
    } else {
        API::get()->log_warn("[RGC] steam_api64.dll not found");
    }

    // --- Fallback: Windows username ---
    char buf[256] = {0};
    DWORD len = sizeof(buf);
    if (GetUserNameA(buf, &len) && buf[0]) {
        API::get()->log_info("[RGC] Fallback to Windows username: %s", buf);
        return std::string(buf);
    }

    // --- Last resort ---
    API::get()->log_info("[RGC] Using default name: Hunter");
    return "Hunter";
}

// ==================================================
// Chat send
// ==================================================
static void add_system_message(const std::string& channel, const std::string& msg) {
    ChatEntry e;
    e.sender = "System";
    e.text = msg;
    e.timestamp = "";
    e.is_self = false;
    std::lock_guard<std::mutex> lock(g_channel_logs_mutex);
    g_channel_logs[channel].push_back(e);
    g_should_scroll = true;
}

static void send_raw_chat(const std::string& channel, const std::string& text) {
    if (!g_ws || g_state != ConnState::Connected) {
        API::get()->log_warn("[RGC] Cannot send chat: not connected");
        return;
    }
    json j;
    j["type"] = "chat";
    j["channel"] = channel;
    j["message"] = text;
    g_ws->send(j.dump());
    API::get()->log_info("[RGC] -> chat #%s: %s", channel.c_str(), text.c_str());
}

static void send_channel_create(const std::string& name) {
    if (!g_ws || g_state != ConnState::Connected) return;
    json j; j["type"] = "channel_create"; j["name"] = name;
    g_ws->send(j.dump());
    API::get()->log_info("[RGC] -> channel_create: %s", name.c_str());
}

static void send_channel_join(const std::string& name) {
    if (!g_ws || g_state != ConnState::Connected) return;
    json j; j["type"] = "channel_join"; j["name"] = name;
    g_ws->send(j.dump());
    API::get()->log_info("[RGC] -> channel_join: %s", name.c_str());
}

static void send_channel_leave(const std::string& name) {
    if (!g_ws || g_state != ConnState::Connected) return;
    json j; j["type"] = "channel_leave"; j["name"] = name;
    g_ws->send(j.dump());
    API::get()->log_info("[RGC] -> channel_leave: %s", name.c_str());
}

static void send_channel_list() {
    if (!g_ws || g_state != ConnState::Connected) return;
    g_ws->send("{\"type\":\"channel_list\"}");
    API::get()->log_info("[RGC] -> channel_list");
}

static bool try_parse_command(const std::string& text) {
    if (text.empty() || text[0] != '/') return false;
    std::string cmd = text.substr(1);
    size_t sp = cmd.find(' ');
    std::string name = (sp == std::string::npos) ? cmd : cmd.substr(0, sp);
    std::string arg  = (sp == std::string::npos) ? "" : cmd.substr(sp + 1);
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    if (name == "rgc") {
        send_channel_list();
        return true;
    }
    if (name == "rgcc") {
        if (arg.empty()) { add_system_message(g_current_channel, "Usage: /rgcc <nama>"); return true; }
        send_channel_create(arg);
        return true;
    }
    if (name == "rgcj") {
        if (arg.empty()) { add_system_message(g_current_channel, "Usage: /rgcj <nama>"); return true; }
        send_channel_join(arg);
        return true;
    }
    return false;
}

static void submit_chat_input(const std::string& text) {
    if (text.empty()) return;
    if (try_parse_command(text)) return;
    send_raw_chat(g_current_channel, text);
}

// ==================================================
// Windows message helpers
// ==================================================
static bool is_keyboard_message(UINT msg) {
    switch (msg) {
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_CHAR:
        case WM_DEADCHAR:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_SYSCHAR:
        case WM_SYSDEADCHAR:
        case WM_UNICHAR:
        case WM_IME_CHAR:
        case WM_IME_COMPOSITION:
        case WM_IME_STARTCOMPOSITION:
        case WM_IME_ENDCOMPOSITION:
            return true;
        default:
            return false;
    }
}

static bool is_initial_keydown(UINT msg, LPARAM lparam) {
    if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN) {
        return false;
    }
    // Bit 30 is 0 for the first keydown and 1 for auto-repeat.
    return (static_cast<uintptr_t>(lparam) & (uintptr_t{1} << 30)) == 0;
}

// ==================================================
// ImGui initialization
// ==================================================
static bool initialize_imgui() {
    if (g_ui_initialized.load()) {
        return true;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = "rgc_ui.ini";

    const auto renderer_data = API::get()->param()->renderer_data;
    if (!renderer_data || !renderer_data->swapchain) {
        API::get()->log_warn("[RGC] renderer_data belum siap");
        ImGui::DestroyContext();
        return false;
    }

    auto* swapchain = static_cast<IDXGISwapChain*>(renderer_data->swapchain);
    DXGI_SWAP_CHAIN_DESC swap_desc{};
    if (FAILED(swapchain->GetDesc(&swap_desc))) {
        API::get()->log_error("[RGC] IDXGISwapChain::GetDesc failed");
        ImGui::DestroyContext();
        return false;
    }
    g_wnd = swap_desc.OutputWindow;

    if (!ImGui_ImplWin32_Init(g_wnd)) {
        API::get()->log_error("[RGC] ImGui_ImplWin32_Init failed");
        ImGui::DestroyContext();
        g_wnd = nullptr;
        return false;
    }

    // Deliberately do NOT subclass GWLP_WNDPROC here. REFramework already owns
    // the window message hook and exposes on_message to plugins.
    if (renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D11) {
        if (!g_d3d11.initialize()) {
            API::get()->log_error("[RGC] D3D11 init failed");
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_wnd = nullptr;
            return false;
        }
    } else if (renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D12) {
        if (!g_d3d12.initialize()) {
            API::get()->log_error("[RGC] D3D12 init failed");
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_wnd = nullptr;
            return false;
        }
    } else {
        API::get()->log_error("[RGC] Unsupported renderer type: %d", renderer_data->renderer_type);
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_wnd = nullptr;
        return false;
    }

    g_ui_initialized.store(true);
    API::get()->log_info("[RGC] ImGui initialized; using REFramework on_message callback (no RGC WndProc subclass)");
    return true;
}

// ==================================================
// Chat network tick
// ==================================================
static void chat_network_tick() {
    if (g_state == ConnState::Disconnected &&
        std::chrono::steady_clock::now() >= g_next_reconnect_time) {
        try_connect();
    }
    if (!g_ws) {
        return;
    }

    g_ws->poll();
    g_ws->dispatch([](const std::string& message) {
        try {
            const auto j = json::parse(message);
            const std::string type = j.value("type", "");

            if (type == "chat") {
                ChatEntry entry;
                entry.sender = j.value("sender", "?");
                entry.text = j.value("message", "");
                entry.timestamp = j.value("timestamp", "");
                entry.is_self = (entry.sender == g_player_name);
                const std::string ch = j.value("channel", "global");
                {
                    std::lock_guard<std::mutex> lock(g_channel_logs_mutex);
                    auto& logs = g_channel_logs[ch];
                    logs.push_back(entry);
                    if (logs.size() > 200) logs.erase(logs.begin());
                }
                if (ch == g_current_channel) g_should_scroll = true;
                API::get()->log_info("[RGC] <- chat #%s [%s] %s: %s",
                    ch.c_str(), entry.timestamp.c_str(), entry.sender.c_str(), entry.text.c_str());
            } else if (type == "welcome") {
                ++g_welcome_count;
                if (g_state == ConnState::Connecting) {
                    set_state(ConnState::Connected, "welcome_received");
                    g_connected_since_time = std::chrono::steady_clock::now();
                    g_last_ping_frame = g_frame_count;
                    g_last_pong_frame = g_frame_count;

                    // Reset channel state on new session
                    const bool was_custom = (g_current_channel != "global");
                    g_joined_channels.clear();
                    g_joined_channels.push_back("global");
                    g_current_channel = "global";
                    g_owned_channels.clear();
                    if (was_custom) {
                        g_chat_mode.store(false);
                        g_clear_input_requested.store(true);
                    }
                    add_system_message("global", "Terhubung ke server. Kamu di #Global.");
                }
            } else if (type == "pong") {
                ++g_pong_count;
                g_last_pong_frame = g_frame_count;
            } else if (type == "error") {
                const std::string error_msg = j.value("msg", "");
                API::get()->log_warn("[RGC] <- server error: %s", error_msg.c_str());
            } else if (type == "system") {
                add_system_message(j.value("channel", g_current_channel), j.value("msg", ""));
            } else if (type == "channel_created") {
                const std::string ch = j.value("name", "");
                g_owned_channels.insert(ch);
                if (std::find(g_joined_channels.begin(), g_joined_channels.end(), ch) == g_joined_channels.end())
                    g_joined_channels.push_back(ch);
                g_current_channel = ch;
                add_system_message(ch, "Channel #" + ch + " dibuat. Kamu sekarang di sini.");
            } else if (type == "channel_joined") {
                const std::string ch = j.value("name", "");
                if (std::find(g_joined_channels.begin(), g_joined_channels.end(), ch) == g_joined_channels.end())
                    g_joined_channels.push_back(ch);
                g_current_channel = ch;
                add_system_message(ch, "Kamu masuk #" + ch);
            } else if (type == "channel_left") {
                const std::string ch = j.value("name", "");
                g_owned_channels.erase(ch);
                g_joined_channels.erase(
                    std::remove(g_joined_channels.begin(), g_joined_channels.end(), ch),
                    g_joined_channels.end());
                if (g_current_channel == ch) {
                    g_current_channel = "global";
                    g_chat_mode.store(false);
                    g_clear_input_requested.store(true);
                }
            } else if (type == "channel_disbanded") {
                const std::string ch = j.value("channel", "");
                g_owned_channels.erase(ch);
                g_joined_channels.erase(
                    std::remove(g_joined_channels.begin(), g_joined_channels.end(), ch),
                    g_joined_channels.end());
                if (g_current_channel == ch) {
                    g_current_channel = "global";
                    g_chat_mode.store(false);
                    g_clear_input_requested.store(true);
                }
                add_system_message("global", j.value("msg", "Channel dibubarkan"));
            } else if (type == "channel_list") {
                std::string msg = "Channel aktif:";
                for (auto& c : j["channels"]) {
                    msg += "\n  #" + c.value("name", "?") + " (" +
                        std::to_string(c.value("count", 0)) + " member)";
                }
                add_system_message(g_current_channel, msg);
            } else if (type == "channel_error") {
                add_system_message(g_current_channel, "Error: " + j.value("msg", "?"));
            }
        } catch (const std::exception& e) {
            API::get()->log_warn("[RGC] JSON parse error: %s", e.what());
        }
    });

    // IXWebSocket melaporkan Close/Error lewat is_closed(); drop & biarkan reconnect.
    if (g_ws && g_ws->is_closed() && g_state != ConnState::Disconnected) {
        drop_ws("ws_closed");
        return;
    }

    // Application-level handshake. Kirim hello HANYA setelah WebSocket open.
    // Sebelumnya ada bug: hello terkirim sebelum WS open, IXWebSocket drop.
    if (g_state == ConnState::Connecting && !g_hello_sent && g_ws->is_open()) {
        json j;
        j["type"] = "hello";
        j["name"] = g_player_name;
        g_ws->send(j.dump());
        g_hello_sent = true;
        g_state_since_frame = g_frame_count;  // reset timer: welcome timeout dihitung dari saat WS open
        API::get()->log_info("[RGC] -> hello sent (WS open)");
    }

    // Timeout hanya jika WS sudah open tapi welcome tidak datang (welcome_timeout),
    // atau WS tidak pernah open sama sekali (connect_timeout).
    if (g_state == ConnState::Connecting &&
        (g_frame_count - g_state_since_frame) > WELCOME_TIMEOUT_FRAMES) {
        drop_ws(g_ws->is_open() ? "welcome_timeout" : "connect_timeout");
        return;
    }

    if (g_state == ConnState::Connected &&
        (g_frame_count - g_last_ping_frame) > PING_INTERVAL_FRAMES) {
        g_last_ping_frame = g_frame_count;
        g_ws->send("{\"type\":\"ping\"}");
        API::get()->log_info("[RGC] -> ping sent (pongs: %u)", g_pong_count);
    }

    if (g_state == ConnState::Connected &&
        (g_frame_count - g_last_pong_frame) > PONG_TIMEOUT_FRAMES) {
        drop_ws("pong_timeout");
        return;
    }

    if (g_state == ConnState::Connected && g_reconnect_attempt > 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - g_connected_since_time).count();
        if (elapsed >= RECONNECT_STABLE_RESET_SEC) {
            API::get()->log_info("[RGC] Connection stable %llds, backoff reset (was attempt #%d)",
                (long long)elapsed, g_reconnect_attempt);
            g_reconnect_attempt = 0;
        }
    }
}

// ==================================================
// Apply keyboard requests on the render/game frame
// ==================================================
static void apply_input_requests() {
    if (g_toggle_ui_requested.exchange(false)) {
        g_ui_open = !g_ui_open;
        if (!g_ui_open) {
            g_chat_mode.store(false);
            g_clear_input_requested.store(true);
        }
        API::get()->log_info("[RGC] UI %s", g_ui_open ? "shown" : "hidden");
    }

    if (g_open_chat_requested.exchange(false)) {
        g_ui_open = true;
        g_chat_mode.store(true);
        g_set_focus = true;
        API::get()->log_info("[RGC] Chat mode ON (/) ");
    }

    if (g_close_chat_requested.exchange(false)) {
        g_chat_mode.store(false);
        g_clear_input_requested.store(true);
        g_set_focus = false;
        API::get()->log_info("[RGC] Chat mode OFF (Esc)");
    }

    if (g_clear_input_requested.exchange(false)) {
        g_input_buf[0] = '\0';
    }
}

// ==================================================
// UI draw
// ==================================================
static void draw_ui() {
    if (!g_ui_open) return;

    ImGui::SetNextWindowSize(ImVec2(480, 380), ImGuiCond_FirstUseEver);
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 480 - 20, 20), ImGuiCond_FirstUseEver);

    const bool contents_visible = ImGui::Begin("Rise Global Chat", &g_ui_open, ImGuiWindowFlags_None);

    if (!g_ui_open) {
        g_chat_mode.store(false);
        g_input_buf[0] = '\0';
        g_set_focus = false;
    }

    if (contents_visible && g_ui_open) {
        // ===== TAB BAR =====
        static std::string pending_leave;
        ImGui::BeginChild("##tabs", ImVec2(0, 32), false);
        for (const auto& ch : g_joined_channels) {
            const bool is_current = (ch == g_current_channel);
            if (is_current) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
            char label[64];
            snprintf(label, sizeof(label), "#%s", ch.c_str());
            if (ImGui::Button(label)) g_current_channel = ch;
            if (is_current) ImGui::PopStyleColor();

            if (ch != "global") {
                ImGui::SameLine(0, 2);
                char xlbl[32];
                snprintf(xlbl, sizeof(xlbl), "x##%s", ch.c_str());
                if (ImGui::SmallButton(xlbl)) pending_leave = ch;
            }
            ImGui::SameLine();
        }
        if (g_owned_channels.size() < 2) {
            if (ImGui::SmallButton("+")) {
                g_create_channel_buf[0] = '\0';
                ImGui::OpenPopup("Buat Channel");
            }
        }
        ImGui::EndChild();

        if (!pending_leave.empty()) {
            send_channel_leave(pending_leave);
            pending_leave.clear();
        }

        if (ImGui::BeginPopupModal("Buat Channel", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Nama channel (maks 20 char):");
            ImGui::InputText("##cn", g_create_channel_buf, sizeof(g_create_channel_buf));
            if (ImGui::Button("Buat", ImVec2(80, 0))) {
                std::string n(g_create_channel_buf);
                if (!n.empty()) send_channel_create(n);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Batal", ImVec2(80, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ===== STATUS =====
        ImGui::TextColored(
            g_state == ConnState::Connected ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
            "Status: %s  |  #%s  |  Pongs: %u",
            state_name(g_state), g_current_channel.c_str(), g_pong_count);
        ImGui::Separator();

        // ===== CHAT AREA =====
        const float input_height = ImGui::GetFrameHeightWithSpacing() + 8.0f;
        ImGui::BeginChild("##chat_scroll", ImVec2(0, -input_height), true,
            ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lock(g_channel_logs_mutex);
            auto& logs = g_channel_logs[g_current_channel];
            for (const auto& e : logs) {
                if (e.sender == "System") {
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.0f), "* %s", e.text.c_str());
                } else if (e.is_self) {
                    ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f),
                        "[%s] %s: %s", e.timestamp.c_str(), e.sender.c_str(), e.text.c_str());
                } else {
                    ImGui::Text("[%s] %s: %s",
                        e.timestamp.c_str(), e.sender.c_str(), e.text.c_str());
                }
            }
        }
        if (g_should_scroll) { ImGui::SetScrollHereY(1.0f); g_should_scroll = false; }
        ImGui::EndChild();

        // ===== INPUT =====
        if (g_set_focus) { ImGui::SetKeyboardFocusHere(); g_set_focus = false; }

        ImGui::TextDisabled("[#%s]", g_current_channel.c_str());
        ImGui::SameLine();
        ImGui::PushItemWidth(-80);
        const bool enter_pressed = ImGui::InputText("##input", g_input_buf, sizeof(g_input_buf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        const bool input_active = ImGui::IsItemActive();
        ImGui::PopItemWidth();

        if (input_active && !g_chat_mode.load()) g_chat_mode.store(true);

        ImGui::SameLine();
        const bool send_pressed = ImGui::Button("Kirim", ImVec2(70, 0));

        if (enter_pressed || send_pressed) {
            std::string text(g_input_buf);
            const size_t s = text.find_first_not_of(" \t");
            const size_t e = text.find_last_not_of(" \t");
            if (s != std::string::npos && e != std::string::npos)
                text = text.substr(s, e - s + 1);
            else
                text.clear();

            if (!text.empty()) {
                submit_chat_input(text);
                g_input_buf[0] = '\0';
                g_chat_mode.store(true);
                g_set_focus = true;
            } else if (enter_pressed) {
                g_chat_mode.store(false);
                g_set_focus = false;
            }
        }
    }
    ImGui::End();
}

// ==================================================
// ImGui render
// ==================================================
static void render_imgui() {
    const auto renderer_data = API::get()->param()->renderer_data;
    if (!renderer_data) {
        return;
    }

    if (renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D11) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        draw_ui();
        ImGui::EndFrame();
        ImGui::Render();
        g_d3d11.render_imgui();
    } else if (renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D12) {
        const auto* cmd_queue = static_cast<ID3D12CommandQueue*>(renderer_data->command_queue);
        if (!cmd_queue) {
            return;
        }
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        draw_ui();
        ImGui::EndFrame();
        ImGui::Render();
        g_d3d12.render_imgui();
    }
}

// ==================================================
// on_present
// ==================================================
void on_present() {
    ++g_frame_count;

    if (!g_logged_first_frame) {
        API::get()->log_info("[RGC] on_present callback firing (frame 1)");
        g_logged_first_frame = true;
    }

    apply_input_requests();
    chat_network_tick();

    if (!g_ui_initialized.load()) {
        if (!initialize_imgui()) {
            return;
        }
    }

    // Periodic diagnostic summary: API call counts and keyboard records observed.
    if ((g_frame_count % 600) == 0) {
        API::get()->log_info(
            "[RGC-DIAG] RawInput calls: GetRawInputData=%u (keyboard records=%u), GetRawInputBuffer=%u (keyboard records=%u)",
            g_raw_data_calls.load(), g_raw_data_keyboard_records.load(),
            g_raw_buffer_calls.load(), g_raw_buffer_keyboard_records.load());
    }

    render_imgui();
}

// ==================================================
// on_message: REFramework's message callback is the only RGC Windows-message path.
// Return false to ask REFramework not to pass the message further to the game.
// ==================================================
static bool on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (!g_ui_initialized.load()) {
        return true;
    }

    // Keep RGC's ImGui Win32 backend fed without taking ownership of GWLP_WNDPROC.
    ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);

    // Consume key-up for hotkeys whose initial key-down was consumed.
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        if (wparam == VK_OEM_2 && g_suppress_slash_keyup.exchange(false)) {
            return false;
        }
        if (wparam == VK_F8 && g_suppress_f8_keyup.exchange(false)) {
            return false;
        }
        if (wparam == VK_ESCAPE && g_suppress_esc_keyup.exchange(false)) {
            return false;
        }
    }

    if (is_initial_keydown(msg, lparam)) {
        if (wparam == VK_F8 && !g_chat_mode.load()) {
            g_toggle_ui_requested.store(true);
            g_suppress_f8_keyup.store(true);
            return false;
        }

        if (wparam == VK_OEM_2 && !g_chat_mode.load()) {
            g_open_chat_requested.store(true);
            g_suppress_slash_keyup.store(true);
            return false;
        }

        if (wparam == VK_ESCAPE && g_chat_mode.load()) {
            g_close_chat_requested.store(true);
            g_suppress_esc_keyup.store(true);
            return false;
        }
    }

    // While the text box owns chat mode, don't forward keyboard or raw-input
    // window messages to the game's WndProc. This does NOT suppress a separate
    // game-internal hardware polling path; Raw Input diagnostics help identify it.
    if (g_chat_mode.load() && (is_keyboard_message(msg) || msg == WM_INPUT)) {
        return false;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput) {
        return false;
    }

    return true;
}

// ==================================================
// on_device_reset
// ==================================================
static void on_device_reset() {
    API::get()->log_info("[RGC] device reset");

    if (g_ui_initialized.load()) {
        const auto renderer_data = API::get()->param()->renderer_data;
        if (renderer_data) {
            if (renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D11) {
                ImGui_ImplDX11_Shutdown();
                g_d3d11 = D3D11{};
            } else if (renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D12) {
                ImGui_ImplDX12_Shutdown();
                g_d3d12 = D3D12{};
            }
        }
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_ui_initialized.store(false);
        g_wnd = nullptr;
    }

    // Raw Input diagnostic hooks deliberately remain installed across device resets.
}

// ==================================================
// Exports
// ==================================================
extern "C" __declspec(dllexport) void reframework_plugin_required_version(
    REFrameworkPluginVersion* version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;
}

extern "C" __declspec(dllexport) bool reframework_plugin_initialize(
    const REFrameworkPluginInitializeParam* param) {
    API::initialize(param);
    auto& api = API::get();

    api->log_info("====================================================");
    api->log_info("[RGC] Rise Global Chat v0.6.0 - IXWebSocket (WSS)");
    api->log_info("[RGC] Compiled against API %d.%d.%d",
        REFRAMEWORK_PLUGIN_VERSION_MAJOR,
        REFRAMEWORK_PLUGIN_VERSION_MINOR,
        REFRAMEWORK_PLUGIN_VERSION_PATCH);
    api->log_info("[RGC] Server target: %s", SERVER_URL);
    api->log_info("[RGC] Hotkeys: / = chat, Esc = close chat, F8 = toggle UI");

    // IXWebSocket: inisialisasi Winsock lewat IXNetSystem (aman dipanggil ulang).
    ix::initNetSystem();

    // Init reconnect backoff state
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    g_reconnect_attempt = 0;
    g_next_reconnect_time = std::chrono::steady_clock::now();

    g_player_name = resolve_player_name();
    api->log_info("[RGC] Player name: %s", g_player_name.c_str());
    api->log_info("[RGC] WndProc subclass disabled; using REFramework on_message");
    api->log_info("====================================================");

    install_raw_input_diagnostics();

    param->functions->on_present(on_present);
    param->functions->on_message((REFOnMessageCb)on_message);
    param->functions->on_device_reset(on_device_reset);
    return true;
}