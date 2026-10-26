# Changelog — Rise Global Chat (RGC)

All notable changes to this project will be documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versioning based on [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Planned (v0.7.0)
- Separate OS window (Win32 + D3D11 swapchain) for chat panel — fixes cursor visibility and enables multi-monitor support
- Additional hardening for public server deployment (token auth, per-IP rate limit)

---

## [0.6.3] — 2026-10-26

### Added
- **Local timezone support** — chat timestamps now display in each player's local time, not server time (WIB/UTC). Client converts Unix timestamp via `localtime_s`.

### Changed
- `server.py` — v0.9: chat payload includes `timestamp_unix` (integer Unix epoch) alongside legacy `timestamp` (WIB string). Backward compatible.
- `dllmain.cpp` — parse `timestamp_unix` and convert to local time. Falls back to legacy `timestamp` string if missing.

### Backward Compatibility
- Client v0.6.2 (auth-only) still works — sees legacy `timestamp` string (WIB)
- Client v0.6.3 sees local time (e.g., JST for Japanese, PST for US)

### Notes
- Server logs `RGC Chat Server v0.9` (was v0.8)
- Client logs `v0.6.3` (was v0.6.2)

---

## [0.6.2] — 2026-10-26

### Added
- **Auth token validation** — server requires a valid token during `hello` handshake. Clients without valid token are rejected and disconnected.
- **Per-IP connection limit** — max 3 simultaneous connections per public IP address. Prevents one IP from opening many connections.
- **Per-IP failed auth rate limit** — max 10 failed auth attempts per 5 minutes per IP. Prevents brute-force token guessing.
- **Multi-token support** — server reads `RGC_AUTH_TOKEN` as comma-separated list, enabling zero-downtime token rotation.
- **`tools/generate_token.py`** — helper script to generate random 64-char token + XOR-encoded C++ array.
- **Auth failure guard** — client stops auto-reconnect after auth failure (prevents reconnect spam).

### Changed
- `server.py` — v0.8 (security hardened, was v0.7).
- `dllmain.cpp` — added auth token to `hello` message, added auth failure detection.
- `test_client_v2.py` — reads token from `RGC_TOKEN` env var, exits on auth failure.

### Security
- Token plaintext stored **only** in Render env var (server) and `set RGC_AUTH_TOKEN=...` (local test). Never committed to Git.
- Token obfuscated in DLL via XOR (key `0x5A`). Not cryptographically secure, but prevents `strings` scan and casual extraction.

### Notes
- **Default limits:** `MAX_CONNECTIONS_PER_IP = 3`, `MAX_FAILED_AUTH_PER_IP = 10/5min`, `AUTH_TOKENS` = 1 (single token).
- **Token rotation procedure** (only if token leaks): see `server.py` comment `RGC_AUTH_TOKEN=token1,token2,token3` for rotation window.

---

## [0.6.1] — 2026-10-25

### Fixed
- **Input bleed** — character no longer moves while typing in chat mode. Implemented via `GetRawInputData` / `GetRawInputBuffer` hook with `RI_KEY_BREAK` flag override during chat mode.
- **In-game chat trigger** — `Enter` key in RGC chat mode no longer opens MHRise's in-game chat panel.
- **In-game menu trigger** — `Esc` key in RGC chat mode no longer opens MHRise's in-game menu.

### Added
- **Exponential backoff + jitter** for reconnect: 1s → 2s → 4s → 8s → 16s → 32s → 60s cap, with 50–150% random jitter. Prevents reconnect storms when the server restarts.
- **Backoff reset** after 30 seconds of stable connection (attempt counter returns to #1 on next disconnect).
- `Reconnect scheduled: attempt #N in XXXX ms` log line for visibility.

### Verified
- **Cross-network test** — HP (Termux, mobile data) ↔ PC game via `wss://rgc-server.onrender.com`. Chat, channel create/join/chat/disband all working end-to-end.
- **Auto-reconnect test** — WiFi kill → reconnect in 10.5 seconds, all chat features restored.
- **Backoff test** — 8-attempt sequence verified (910 ms → 2440 → 3520 → 10400 → 19040 → 36800 → 60000 → 60000 cap), jitter confirmed random, reset confirmed after 30s stable.

### Known Issues
- **Cursor visibility** — OS cursor may disappear when hovering over RGC panel area. Workaround: click outside panel, cursor returns. Permanent fix deferred to v0.7.0 (separate OS window).
- **Alt+Tab in chat mode** — Chat mode does not auto-exit on alt-tab. Press `/` again after returning to game to re-focus chat input.
- **Cold start (Render free tier)** — First connect after 15-min server idle can take 30–60 seconds. Panel shows `Disconnected` temporarily, then auto-connects.

### Reverted
- ❌ `Hooked_ShowCursor` / `Hooked_SetCursor` — attempted for cursor visibility fix, caused freeze/Not Responding when triggering Win key or Alt+Tab during chat mode. Reverted, deferred to v0.7.0 architecture change.
- ❌ Alt/Tab/Win pass-through in `on_message` — not needed, v0.6.0 already handles this gracefully.
- ❌ Auto-exit chat mode on `WM_ACTIVATE` / focus lost — interfered with existing behavior, caused input state inconsistency.

---

## [0.6.0] — 2026-10-24

### Changed
- **Migrated WebSocket library:** `easywsclient` → `IXWebSocket 2.x` (native WSS/TLS support via mbedTLS).
- **Build system:** vcpkg triplet `x64-windows-static`, linked `ixwebsocket::ixwebsocket` + `OpenSSL::SSL` + `OpenSSL::Crypto` + `bcrypt` (required for mbedTLS `BCryptGenRandom`).
- **Networking:** `SERVER_URL` now `wss://rgc-server.onrender.com` (was `ws://127.0.0.1:8765` for local testing).

### Fixed
- **WSS/TLS handshake** — client now connects successfully to `wss://` endpoints. Root cause: `easywsclient` (dhbaird) has no `EZWS_HAVE_OPENSSL` guard, so `wss://` handshake was sent as plaintext to port 443 and rejected by Cloudflare/Render. Migrated to IXWebSocket for native TLS.
- **Hello timing** — `hello` only sent after `is_open()` returns true. Previously sent immediately after `from_url()` returned, which IXWebSocket silently dropped because the internal thread was still initializing → server `HELLO_TIMEOUT=10s` → connection closed.
- **Server event loop hang** — one slow client could block the entire `websockets` event loop via indefinite `await ws.send()`. Fixed with `asyncio.wait_for(ws.send(...), timeout=3.0)` in `send_to()` and `broadcast_channel()`. Slow clients are marked dead and removed from channels, keeping the loop responsive.
- **Hello state on reconnect** — `hello_sent` now properly reset in `try_connect()` via `WsClient` wrapper, so reconnect attempts always re-send hello correctly.
- **Winsock conflict** — added `#define WIN32_LEAN_AND_MEAN` and `#define NOMINMAX` before `<Windows.h>` to prevent conflict between legacy `winsock.h` and IXWebSocket's `winsock2.h`.

### Added
- **`WsClient` wrapper class** — encapsulates IXWebSocket with easywsclient-like API (`from_url` / `send` / `poll` / `dispatch` / `is_open` / `is_closed`), so existing RGC code paths needed minimal changes.
- **Diagnostic logging:**
  - `[RGC] IXWS <- msg (len=N)` — every incoming message
  - `[RGC] IXWS Close event (code=X reason=Y)` — WebSocket close with reason
  - `[RGC] IXWS send FAILED` — failed send queue
  - `[RGC] WebSocket TCP+TLS handshake complete` — confirm WS open event

### Verified
- **Local multi-client:** 3 min 40 s continuous session, 16 pings → 16 pongs, all channel operations working (create/join/chat/leave/disband).
- **Production WSS:** 5 min 36 s continuous session on Render free tier, chat #global 2-way, channel #mabar create/join/chat/disband verified.

### Known Issues
- **Input bleed** — still active, deferred to v0.6.1.
- **Cursor visibility** — still active, deferred to v0.7.0.
- **Render free tier** — auto-sleep after 15 min idle. Client has auto-reconnect, acceptable for MVP.

### Notes
- TLS backend is **mbedTLS** (bundled by IXWebSocket on Windows), not OpenSSL — although OpenSSL is also linked via vcpkg, it's not actively used by IXWebSocket default.
- Install size: ~14 min build time for OpenSSL from source. Post-cleanup (`buildtrees`/`packages`/`downloads` removed): ~300–500 MB.

---

## [0.5.5] — 2026-10-23

### Added
- **Channel system** — create, join, leave, disband.
- **Tab bar UI** — `[#Global] [#custom (x)] [+]`, click to switch, `(x)` to leave, `+` to create when < 2 custom channels.
- **Per-channel chat isolation** — messages in #global don't leak to custom channels.
- **System messages** — "X joined" / "X left" broadcast to channel members.
- **Owner disband on disconnect** — when owner disconnects, channel is disbanded and remaining members auto-return to #Global.

### Constraints
- Max 2 custom channels per owner.
- Max 10 members per channel.
- Owner cannot leave without disbanding.

### Commands
- `/rgc` — list all active channels on server
- `/rgcc <name>` — create channel (you become owner)
- `/rgcj <name>` — join channel by name
- `(x)` on tab — leave channel (owner → disband)

### Known Issues
- **Input bleed** — inherited from v0.5 (fixed in v0.6.1).
- **Cursor visibility** — inherited from v0.5 (deferred to v0.7.0).

---

## [0.5] — 2026-10-23

### Added
- **ImGui overlay UI** — panel at top-right, draggable, collapsible, closable.
- **Chat via UI** — send/receive through panel instead of F9 hotkey.
- **Status indicator** — `Connected` / `Disconnected` state visible in panel.
- **Hotkeys:** `/` open chat mode, `Enter` submit, `Esc` exit chat mode, `F8` toggle panel.
- **Steam name auto-detect** — player name resolved from Steam persona (fallback: Windows username, then `Hunter`).

### Known Issues
- **Input bleed** — character moves while typing in chat mode; in-game chat also triggers on `Enter`. Attempted fixes via WndProc, DInput, GetKeyboardState, XInput — all ineffective because MHRise reads keyboard through internal RE Engine path (`snow.StmInputManager` or Direct HID). Deferred to v0.6.1 (resolved via `GetRawInputData` flag override).
- **Cursor visibility** — OS cursor disappears over panel area. Deferred to v0.7.0.

---

## [0.4] — 2026-10-23

### Added
- **Chat send via F9** — hotkey sends predefined message to server.
- **Chat receive** — server broadcasts received chat to all connected clients.
- **Sender tracking** — server logs sender name on every chat.
- **`test_client_v2.py`** — Python CLI test client with chat support (later extended with channel commands).
- **`build_deploy.bat`** — automated build + deploy script.

### Verified
- Multi-client chat 2-way: Alice (CLI) ↔ game (Hunter).
- Server broadcast to N clients confirmed.
- Ping-pong stable under chat traffic.

### Notes
- Frame-based timer drifts when FPS drops (ping #7 → #8 took 85 s during alt-tab). Deferred std::chrono migration to future version (not yet resolved as of v0.6.1).

---

## [0.3] — 2026-10-23

### Added
- **Application-level handshake** — `hello` → `welcome` as connection verification.
- **`welcome_timeout`** — client gives up if server doesn't respond within 600 frames (~10 s).
- **Ping/pong heartbeat** — app-level, every 600 frames.
- **Server verbose logging** — connect/disconnect/hello/chat/ping events.
- **Multi-client test (Opsi C)** — 2 independent connections verified, disconnect isolation confirmed, reconnect working.

### Notes
- Supersedes v0.2 (which used `getReadyState()` — unreliable, never released).
- Baseline `ws://` client + server stack PROVEN for multi-client.

---

## [0.1] — 2026-10-23

### Added
- **Minimal RGC plugin skeleton** — DLL loads into REFramework without crashing.
- **Two exports:** `reframework_plugin_required_version` (v1.15.0) + `reframework_plugin_initialize`.
- **`on_present` callback** — heartbeat log every N frames.
- **API logging** — `API::get()->log_info` for visibility into plugin state.
- **Verified:** DLL loaded, plugin initialized, RiseQuestLoader still runs, 20,400+ frames without crash.

### Environment
- VS2022 Build Tools 17.4.3 + MSVC 19.34
- CMake 4.5.0-rc1
- Windows SDK 10.0.22000
- REFramework v1.5.9.1+515 (commit `d4611aef`)
- Plugin API: C++20, static runtime `/MT`

---

## Version History Summary

| Version | Date | Highlights |
|---|---|---|
| **v0.6.1** | 2026-10-25 | Input bleed fix, exponential backoff + jitter |
| **v0.6.0** | 2026-10-24 | IXWebSocket migration, server event loop fix, WSS production |
| **v0.5.5** | 2026-10-23 | Channel system |
| **v0.5** | 2026-10-23 | ImGui UI |
| **v0.4** | 2026-10-23 | Functional chat (F9) |
| **v0.3** | 2026-10-23 | WebSocket + application handshake baseline |
| **v0.1** | 2026-10-23 | Minimal plugin skeleton |

**Note:** v0.2 was never released (used unreliable `getReadyState()`, superseded by v0.3's application-level handshake).

---

## Migration Notes

### From v0.5.x to v0.6.x

**Build system changes:**
- Requires vcpkg with `ixwebsocket:x64-windows-static` and `openssl:x64-windows-static`
- `CMakeLists.txt` now needs `-DCMAKE_TOOLCHAIN_FILE` + `-DVCPKG_TARGET_TRIPLET=x64-windows-static`
- `build_deploy.bat` updated to include vcpkg toolchain flags
- `thirdparty/easywsclient.cpp` and `.hpp` are no longer used — safe to delete

**Runtime changes:**
- Server URL changed from `ws://127.0.0.1:8765` to `wss://rgc-server.onrender.com` for production
- For local development, revert to `ws://127.0.0.1:8765` in `SERVER_URL` constant

**Behavioral changes:**
- Reconnect now uses exponential backoff (up to 60s between attempts) instead of fixed 5s interval
- Chat input in chat mode no longer triggers in-game actions (v0.6.1)