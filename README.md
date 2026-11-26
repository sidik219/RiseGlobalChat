# Rise Global Chat (RGC)

**Rise Global Chat (RGC)** is a global chat overlay for **Monster Hunter Rise (Steam / PC)**. It allows RGC users to communicate in real time through a shared WebSocket server, even when they are not in the same in-game lobby.

RGC connects to its hosted backend over **WSS/TLS**. Players do not need to configure port forwarding or host their own server.

![Version](https://img.shields.io/badge/version-0.6.6-blue)
![REFramework](https://img.shields.io/badge/REFramework-Nightly%2001433%2B-green)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey)
![License](https://img.shields.io/badge/license-GPL--3.0-blue)

> **Release status:** v0.6.6 is the planned Public Beta release. The previous v0.6.5 release introduced Private Chat (DM). Confirm the DLL version and release package contents before publishing this README.

---

## Server Status

RGC uses a hosted VPS backend. Public WebSocket traffic is routed through Cloudflare Tunnel.

- **Production endpoint:** `wss://chat.riseglobalchat.my.id`
- **Protocol:** WebSocket Secure (WSS) over TLS
- **Server platform:** Ubuntu Linux

The service is provided as-is. No fixed concurrent-player capacity, unlimited-bandwidth claim, uptime guarantee, or service-level commitment is advertised. Actual capacity depends on server resources, network conditions, and usage.

If RGC cannot connect, check your internet connection and review the troubleshooting section before reporting the issue.

---

## Features

### Chat and Channels

- **Global Chat** — clients join the public `#global` channel after successful authentication.
- **Custom Channels** — create or join named chat channels.
- **Custom Channel Limits** — up to 2 custom channels per owner and up to 10 members per channel.
- **Private Chat (DM)** — start a one-to-one conversation with another connected player.
- **DM Limits** — up to 2 active DM sessions per player, with a 10-second cooldown between DM creation requests.
- **Unique Display Names** — duplicate player names are assigned suffixes such as `#2` or `#3`.
- **Owner Disband** — when a custom-channel owner leaves, the channel is disbanded and its members return to `#global`.
- **Channel Tabs** — switch between active channels in the overlay.
- **Channel Isolation** — messages are routed to the relevant public channel, custom channel, or DM rather than broadcast to unrelated channels.
- **DM Lifecycle** — a DM is closed when either participant disconnects.

### Networking

- **WSS/TLS** — encrypted connection to the RGC production endpoint.
- **Server Authentication** — clients must complete the server's authentication handshake before using chat features.
- **Automatic Reconnection** — exponential backoff with jitter; the delay increases up to a configured maximum.
- **Lobby-Independent Chat** — message routing is handled by the RGC server, not by Monster Hunter Rise lobby membership.
- **Cross-Network Communication** — users can communicate through the internet from different network connections.

### Overlay and Controls

- **ImGui Overlay** — in-game chat panel provided by the REFramework plugin.
- **Panel Controls** — show or hide the panel and interact with chat through the overlay.
- **Keyboard Chat Mode** — open the chat input, send a message, and exit chat mode using the documented keys below.
- **Local Timestamps** — message timestamps are displayed using the client's local timezone.

---

## Controls and Commands

### Keyboard Controls

| Key | Action |
|---|---|
| `/` | Open chat mode and focus the input field |
| `Enter` | Send the current message |
| `Esc` | Exit chat mode |
| `F8` | Toggle panel visibility |

### In-Game Chat Commands

| Command | Action |
|---|---|
| `/rgc` | List active channels |
| `/rgcc <name>` | Create a custom channel |
| `/rgcj <name>` | Join a custom channel by name |
| `/rgcp <name>` | Start a private chat with a player |

Additional actions:

- Click a player's name to start a private chat, where supported by the overlay.
- Use the `(x)` control on a channel tab to leave that channel.
- When the owner leaves a custom channel, that channel is disbanded.

> **Command note:** `/help` and `/rgcl` are supported by the Python test client; they are not listed as in-game commands in this release documentation.

---

## Installation

### Requirements

- Monster Hunter Rise — Steam / PC version.
- Windows 10 or Windows 11, 64-bit.
- **REFramework Nightly 01433 or newer**, with Plugin API 1.15.0 or later.

> **Important:** RGC requires the REFramework Plugin API 1.15.0 or later. An older REFramework build may prevent the plugin from loading.

### Install REFramework

1. Download `REFramework.zip` from the [REFramework Nightly Releases](https://github.com/praydog/REFramework-nightly/releases).
2. Extract `dinput8.dll` into the Monster Hunter Rise game root directory.
3. Launch the game once so REFramework can create its folders.

### Install RGC

1. Download the `RiseGlobalChat-v0.6.6.zip` package from the RGC release page when it is published.
2. Extract `RiseGlobalChat.dll` from the package.
3. Copy the DLL into the following directory:

   `Monster Hunter Rise/reframework/plugins/`

4. Launch the game.
5. Press `F8` if the RGC panel is not visible.

### Verify Installation

Open the REFramework log file:

`reframework/re2_framework_log.txt`

Look for RGC plugin initialization and the configured production endpoint. Exact log wording may vary by build. The server target should be:

```text
wss://chat.riseglobalchat.my.id
```

---

## Configuration

### Player Name

RGC uses a player display name supplied by the client. The server validates and normalizes accepted names. Duplicate names may receive a unique suffix so that connected players can be distinguished.

### Server Endpoint

The official client is configured to use:

```text
wss://chat.riseglobalchat.my.id
```

Custom server deployments require a compatible backend and matching authentication configuration. Changing the server URL alone may not be sufficient.

---

## Building from Source

This section is intended for developers who have access to the RGC source code and its required dependencies. Build instructions must be followed together with the repository's current `CMakeLists.txt` and `build_deploy.bat` files.

### Requirements

- Visual Studio 2022 Build Tools with the C++ desktop development workload.
- CMake.
- vcpkg.
- REFramework headers/source required by the project.
- IXWebSocket and its configured TLS dependency.
- Other dependencies specified by the project's CMake configuration.

### vcpkg Setup

If vcpkg is not already installed:

```bat
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

The current IXWebSocket build configuration used for RGC is static x64 and uses **mbedTLS** as its TLS backend, with OpenSSL disabled. Install dependencies according to the project's current CMake configuration; do not assume that an OpenSSL package is required.

### Build

Before building, check the paths configured in `build_deploy.bat`, including:

- `VCPKG_ROOT` — vcpkg installation directory.
- `PLUGINS_DIR` — Monster Hunter Rise `reframework/plugins` directory.
- REFramework source or header path, if required by the current project configuration.

Run:

```bat
build_deploy.bat
```

The expected release DLL is:

```text
build/Release/RiseGlobalChat.dll
```

> Build instructions and dependency versions should be rechecked against the exact source included in the public repository before release.

---

## Dependencies and Third-Party Licenses

RGC uses the following third-party components. Refer to the included third-party notices and upstream license files for the applicable terms and notices.

| Dependency | Purpose | License |
|---|---|---|
| REFramework by praydog | Plugin host and game integration | MIT |
| IXWebSocket | WebSocket client | BSD-3-Clause |
| mbedTLS | TLS backend used by the current IXWebSocket build | Apache-2.0 |
| Dear ImGui | Overlay UI framework | MIT |
| nlohmann/json | JSON parsing | MIT |
| MinHook | API hooking | BSD-2-Clause |

OpenSSL is not listed as a direct RGC client dependency in this build configuration because the reviewed IXWebSocket build uses mbedTLS with OpenSSL disabled. Confirm the final dependency notices against the exact release binaries and source before publishing.

---

## Known Issues

### Cursor Visibility

The operating-system cursor may disappear when hovering over part of the RGC panel area.

**Workaround:** Move the cursor outside the panel area. This issue may require a future UI change to resolve fully.

### Alt+Tab While in Chat Mode

Chat mode may not automatically exit when switching to another application. Input focus may need to be restored after returning to the game.

**Workaround:** Press `Esc` to exit chat mode, then press `/` to open it again.

### REFramework DirectX 12 Reset Messages

During testing, REFramework logged `Present failed` and DirectX 12 device-reset messages around application focus or window transitions. RGC networking continued during the observed test, but the exact cause of these graphics messages has not been established.

If these messages affect the game or overlay, include the relevant section of `re2_framework_log.txt` in a bug report. Do not assume that every DirectX or Present message indicates an RGC network failure.

---

## Troubleshooting

### RGC Panel Does Not Appear

1. Verify that REFramework is installed and loads correctly.
2. Verify that `RiseGlobalChat.dll` is in `reframework/plugins/`.
3. Confirm that the installed REFramework build provides Plugin API 1.15.0 or later.
4. Check `reframework/re2_framework_log.txt` for plugin-loading errors.

### RGC Remains Disconnected

Check your internet connection and confirm that the client is configured for the official endpoint:

```text
wss://chat.riseglobalchat.my.id
```

Then check the REFramework log for WebSocket, TLS, or authentication errors. If the problem persists, report the relevant log entries through GitHub Issues.

### Authentication Failed

Use the official client package corresponding to the release. If authentication continues to fail, include the RGC log entries and the client version in your report. Do not post authentication tokens, credentials, or private configuration values in public issues.

### Messages Are Not Reaching Another Player

- Confirm that both clients are connected to RGC.
- Confirm that both users are in the intended channel or DM.
- Check whether the other player has disconnected.
- Check the client log for connection or channel errors.

Players do not need to be in the same Monster Hunter Rise lobby to communicate through RGC, provided both clients are connected to the RGC server.

### Reconnection Takes Longer

RGC uses exponential reconnection backoff with jitter. The delay increases after repeated failures and is capped by the client configuration. This helps avoid repeated connection attempts when the server or network is unavailable.

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for the complete version history.

### v0.6.6 — Public Beta

The v0.6.6 package is intended to provide the current RGC core chat experience for public beta testing. The final release notes must be synchronized with the actual DLL and server changes included in the release.

### v0.6.5

- Added Private Chat (DM) for one-to-one conversations with connected players.
- Added dedicated DM tabs and automatic switching when a DM is initiated or received.
- Added unique display-name suffixes to handle duplicate player names.
- Added a limit of 2 active DM sessions per player and a 10-second cooldown between DM creation requests.
- Added automatic DM closure when either participant disconnects.
- Kept DM sessions out of the public channel list.

---

## Architecture

RGC uses a client-server architecture. The game client maintains its own connection to the RGC backend; chat delivery does not depend on the player's Monster Hunter Rise lobby.

```text
Monster Hunter Rise
        |
        v
RGC Client DLL
(C++ / REFramework / IXWebSocket)
        |
        | WSS / TLS
        v
Cloudflare Edge
        |
        v
Cloudflare Tunnel
        |
        v
VPS - Python WebSocket Backend
        |
        v
Global Chat / Custom Channels / Private DM
```

### Components

- **Client:** C++ REFramework plugin, IXWebSocket, and ImGui overlay.
- **Server:** Python WebSocket backend running as a systemd service.
- **Public connectivity:** Cloudflare Tunnel.
- **Protocol:** JSON messages over WebSocket Secure.
- **Production endpoint:** `wss://chat.riseglobalchat.my.id`.

The backend handles client authentication, message routing, channel management, private conversations, and connection lifecycle events.

---

## Privacy and Server Use

RGC processes player display names and chat messages to provide the service. Server-side audit logs may contain information needed for service operation, security monitoring, and troubleshooting, including:

- Player display names.
- Chat message content.
- Channel identifiers and channel events.
- Private-chat participants and related events.
- Connection and disconnection events.

Do not send passwords, API keys, authentication tokens, personal information, or other sensitive data through RGC.

RGC v0.6.6 does not provide a public user account portal or self-service ban appeal system. The planned v0.6.7 work includes further server security, user identity/authentication improvements, and administrative user-management controls. These planned features are not available in v0.6.6.

The service is provided as-is. Availability and capacity may change without notice. No uptime or service-level guarantee is provided.

---

## Feedback and Bug Reports

Please report bugs through GitHub Issues. Include:

1. RGC-related entries from `re2_framework_log.txt`.
2. Monster Hunter Rise game version.
3. REFramework version.
4. RGC DLL version.
5. Steps to reproduce the issue.
6. Whether the issue affects Global Chat, Custom Channels, DM, or connection stability.

For general questions and community feedback, use the Nexus Mods comments section.

**Before sharing logs publicly:** remove authentication tokens, private credentials, IP addresses, or other information that should not be public.

---

## License

RGC is distributed under the **GNU General Public License v3.0**. See the `LICENSE` file for the complete terms.

Third-party components remain subject to their respective licenses. Retain the applicable upstream copyright notices and license texts when redistributing those components, as described in the included third-party notices.

---

## Credits

RGC is built using REFramework and the third-party components listed above. Thanks to their developers and maintainers for making their work available to the community.
