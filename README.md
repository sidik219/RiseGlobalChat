# Rise Global Chat (RGC)

Cross-platform global chat overlay for **Monster Hunter Rise**. Chat with other hunters in real-time via dedicated VPS backend (WSS/TLS) — no port forwarding, no dedicated server needed.

![Version](https://img.shields.io/badge/version-0.6.4-blue)
![REFramework](https://img.shields.io/badge/REFramework-Nightly%2001433%2B-green)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey)
![License](https://img.shields.io/badge/license-GPL--3.0-blue)

---

## Server Status

The backend runs on a dedicated VPS (Ubuntu, Nginx + Let's Encrypt SSL) with capacity for **up to 200 concurrent players**. No bandwidth limit.

If you see "Server penuh" in the panel, the server has reached capacity — please try again later or report it on GitHub Issues.

---

## Features

### Chat and channels

- **#Global** — auto-join public channel
- **Custom channels** — up to 2 per owner, max 10 members per channel
- **Owner disband** — owner leaves → channel disbanded, members back to #Global
- **Tab bar** — switch between channels with 1 click
- **Per-channel isolation** — messages in #global don't leak to custom channels

### Networking

- **WSS/TLS** — encrypted connection to `wss://shyrly.duckdns.org` (dedicated VPS + Let's Encrypt SSL)
- **Auto-reconnect** — exponential backoff (1s → 2s → 4s → ... → 60s cap) with jitter 50–150%
- **Multi-client** — up to 200 concurrent users per server
- **Cross-region** — no port forwarding needed
- **Auth token** — secure handshake, no unauthorized access
- **Per-IP limit** — max 3 concurrent connections per public IP
- **Cross-network verified** — HP (Termux, mobile data) ↔ PC game tested

### Overlay and controls

- **ImGui overlay** — panel can drag, collapse, close
- **Real-time** — messages appear without delay (<100ms typical)
- **Input bleed fixed** — character does NOT move while typing in chat mode
- **Local timezone** — chat timestamps in your local time (v0.6.3+)

  -----------------------------------------------------------------------
  Key / command                       Action
  ----------------------------------- -----------------------------------
  `/`                                 Open chat mode and focus the input

  `Enter`                             Send the message

  `Esc`                               Exit chat mode

  `F8`                                Toggle panel visibility

  `/rgc`                              List active channels

  `/rgcc <name>`                      Create a custom channel

  `/rgcj <name>`                      Join a channel by name

  `(x)` on a channel tab              Leave a channel; leaving as owner
                                      disbands it
  -----------------------------------------------------------------------

---

## Installation

### Requirements

- **Monster Hunter Rise** (Steam, latest version)
- **REFramework Nightly 01433+** — [download here](https://github.com/praydog/REFramework-nightly/releases)
  - **IMPORTANT:** Stable REFramework v1.5.9.1 from Nexus may be too old (Plugin API < 1.15.0). RGC requires Plugin API 1.15.0+, available in REF Nightly 01433+.
- Windows 10/11 x64

### Steps

1. **Install REFramework Nightly 01433+** first:
   - Download `REFramework.zip` from [nightly releases](https://github.com/praydog/REFramework-nightly/releases)
   - Extract `dinput8.dll` to MHRise root folder (replace old if exists)
   - Run game once → `reframework/` folder auto-created
2. **Download** `RiseGlobalChat-v0.6.4.zip`
3. **Extract** `RiseGlobalChat.dll`
4. **Copy** to `<Game Directory>\reframework\plugins\`
5. Run game → RGC panel appears at top-right

**Verify install:** Open `reframework/re2_framework_log.txt`, look for:
```
[RGC] Rise Global Chat v0.6.4 - VPS dedicated server
[RGC] Server target: wss://shyrly.duckdns.org
```

---

## Configuration

### Player name
Auto-detected from:
1. **Steam persona name** (priority)
2. Windows username (fallback)
3. `Hunter` (last resort)

No manual config needed.

### Server
Default: `wss://shyrly.duckdns.org` (dedicated VPS, Ubuntu + Nginx + Let's Encrypt)
- Max 200 concurrent clients
- No bandwidth limit
- Auto-reconnect with exponential backoff
- Auth token required (bundled in DLL)

**For custom server:** edit `SERVER_URL` in `dllmain.cpp`, rebuild.

---

## Building from source

These instructions are intended for developers with access to the RGC
source code.

### Requirements
- **Visual Studio 2022 Build Tools** (MSVC 19.34+)
- **CMake** 3.15+
- **vcpkg** — for IXWebSocket + OpenSSL
- **REFramework source** — download to `..\..\backup\reframework-src\`

### Setup
```bat
:: 1. Clone vcpkg (once, outside project)
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

:: 2. Install dependencies (static triplet mandatory)
C:\vcpkg\vcpkg.exe install ixwebsocket:x64-windows-static
C:\vcpkg\vcpkg.exe install openssl:x64-windows-static

:: 3. Setup REFramework source
:: Download REFramework source to: ..\..\backup\reframework-src\
```

### Build
```bat
:: Edit build_deploy.bat:
:: - VCPKG_ROOT (default C:\vcpkg)
:: - PLUGINS_DIR (your MHRise plugins folder)

build_deploy.bat
```

Output: build\Release\RiseGlobalChat.dll

### Dependencies
- REFramework by praydog (MIT) — plugin host + ImGui
- IXWebSocket by Machine Zone (BSD-3-Clause) — WSS/TLS WebSocket client
- mbedTLS by ARM (Apache 2.0) — TLS backend used by IXWebSocket on Windows
- OpenSSL (Apache 2.0) — linked via vcpkg (transitive)
- ImGui by Omar Cornut (MIT) — UI framework
- nlohmann/json (MIT) — JSON parser
- MinHook by TsudaKageyu (BSD-2-Clause) — API hooking

---

### Known Issues

## Cursor visibility
OS cursor may disappear when hovering over RGC panel area. Workaround: click outside panel area, cursor returns. Permanent fix planned for v0.7.0 (separate OS window).

## Alt+Tab while in chat mode
Chat mode does NOT auto-exit when you alt-tab. After coming back, press / again if input box lost focus.

---

## Troubleshooting

Panel not appearing / stuck at Disconnected

Cause: REFramework Plugin API version mismatch. RGC requires Plugin API 1.15.0.

Fix: Download REF Nightly 01433+, replace dinput8.dll in game root, launch game once.

Symptoms in log:
    - [PluginLoader] RiseGlobalChat requires version 1.15.0 followed by silence
    - OR panel missing entirely

"Auth token tidak valid" error

Cause: Using old version (v0.6.3 or earlier — Render URL).

Fix: Download v0.6.4 from Nexus Files tab, replace old RiseGlobalChat.dll.
"Server penuh" in panel

Server has reached 200 concurrent capacity. Please try again later.
Chat not reaching other players
    - Check internet connection
    - Verify other players are also Connected
    - Check if in same channel (/rgc to list)

Reconnect takes long
    - After disconnect, reconnect delay grows: 1s, 2s, 4s, 8s, 16s, 32s, up to 60s max
    - This is intentional (prevent reconnect storm)
    - Backoff resets to 1s after 30s of stable connection

---

## Changelog

See CHANGELOG.md for full version history.

v0.6.4 highlights:
    - Migrated to dedicated VPS (200 concurrent, no bandwidth limit)
    - Server URL: wss://shyrly.duckdns.org

v0.6.3 highlights:
    - Local timezone support for chat timestamps

v0.6.2 highlights:
    - Auth token + per-IP security

v0.6.1 highlights:
    - Input bleed fixed (character stays still while typing)
    - Exponential backoff + jitter for reconnect

---

## Architecture

- Client: C++ DLL, REFramework plugin, IXWebSocket (WSS/TLS), ImGui overlay
- Server: Python 3.14 + websockets, systemd, Nginx reverse proxy, Let's Encrypt
- Protocol: JSON over WebSocket (hello, chat, channel_create, channel_join, channel_leave, ping/pong)

## Privacy and server use

RGC uses a player name and chat messages to provide the chat service.
These are sent to the RGC server and may be processed in server logs for
operation and troubleshooting. Do not send passwords, API keys, personal
information, or other sensitive data through chat.

The service is provided as-is. Availability, connection time, and
capacity may vary, especially while using free-tier hosting. Do not rely
on the current server capacity statement as a security guarantee or
service-level commitment.

---

## Feedback

- Nexus comments — general questions
- GitHub Issues — bug reports, include:
    1. [RGC] log from re2_framework_log.txt
    2. Game version (Steam)
    3. REFramework version (check [REFramework] [info] Tag: in log)
    4. Steps to reproduce

---

## License

GNU General Public License v3.0 — see LICENSE file for details.

Attribution requirement: any fork, modification, or derivative work MUST retain the original copyright notice and credit the original author.

Third-party libraries retain their own licenses:

- REFramework (MIT)
- IXWebSocket (BSD-3-Clause)
- mbedTLS (Apache 2.0)
- OpenSSL (Apache 2.0)
- ImGui (MIT)
- nlohmann/json (MIT)
- MinHook (BSD-2-Clause)

---

## Credits

RGC uses REFramework and the third-party components listed above. Please
retain applicable upstream copyright and license notices when
redistributing those components.
