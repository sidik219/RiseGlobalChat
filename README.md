# Rise Global Chat (RGC)

A cross-platform global chat overlay for **Monster Hunter Rise
(Steam)**. Chat with hunters in real time through a cloud-hosted
WebSocket server secured with WSS/TLS. No port forwarding or dedicated
server setup is required for players.

![Version](https://img.shields.io/badge/version-0.6.1-blue)
![REFramework](https://img.shields.io/badge/REFramework-1.5.9.1%2B-green)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey)
![License](https://img.shields.io/badge/license-GPLv3-blue)

## Server Status

The backend runs on a dedicated VPS (Ubuntu, Nginx + Let's Encrypt SSL) with capacity for **up to 200 concurrent players**. No bandwidth limit.

If you see "Server penuh" in the panel, the server has reached capacity — please try again later or report it on GitHub Issues.

## Features

### Chat and channels

-   **Global channel:** automatically join the public `#Global` channel.
-   **Custom channels:** create or join custom channels.
-   **Channel limits:** each owner can create up to 2 custom channels;
    each channel supports up to 10 members.
-   **Channel tabs:** switch between channels from the RGC panel.
-   **Channel isolation:** messages are delivered to the relevant
    channel.
-   **Owner disband:** when an owner leaves or disconnects, their custom
    channel is disbanded and remaining members return to `#Global`.

### Networking

-   **WSS/TLS:** encrypted connection to
    `wss://rgc-server.onrender.com`.
-   **Automatic reconnect:** exponential backoff with jitter, capped at
    60 seconds.
-   **Cross-network support:** tested between a PC running the game and
    a phone using Termux over mobile data.
-   **Cloud-hosted server:** no port forwarding is required.

### Overlay and controls

-   ImGui overlay that can be moved, collapsed, and hidden.
-   Chat input mode designed to prevent game movement and in-game UI
    actions while typing.
-   Connection status displayed in the panel.

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

## Installation

### Requirements

-   Monster Hunter Rise for Steam.
-   Windows 10/11 x64.
-   [REFramework](https://www.nexusmods.com/monsterhunterrise/mods/26),
    version 1.5.9.1 or later.

### Steps

1.  Install REFramework if it is not already installed.
2.  Launch the game once so REFramework can create its folder.
3.  Download the RGC release ZIP.
4.  Extract `RiseGlobalChat.dll`.
5.  Copy the DLL into `<Game Directory>\reframework\plugins\`.
6.  Launch the game. The RGC panel should appear.

To verify that the plugin loaded, open
`reframework/re2_framework_log.txt` and look for RGC startup messages.

## Configuration

### Player name

RGC attempts to determine the player name in this order:

1.  Steam persona name.
2.  Windows username.
3.  `Hunter` as a fallback.

No manual configuration is normally required.

### Server

The default server is `wss://rgc-server.onrender.com`.

The server currently runs on Render's free tier. It may sleep after a
period of inactivity, so the first connection after idle time can take
around 30--60 seconds. RGC retries automatically when disconnected.

A custom server requires a client build configured with the appropriate
server URL.

## Troubleshooting

### The panel does not appear

-   Confirm REFramework is installed and working.
-   Confirm `RiseGlobalChat.dll` is in `reframework/plugins/`, not the
    game root.
-   Check `reframework/re2_framework_log.txt` for plugin-loading errors.
-   Check that your REFramework version meets the requirement above.

### The status remains disconnected

-   Check your internet connection.
-   Allow time for the free-tier server to wake up after inactivity.
-   Review RGC-related messages in the REFramework log.

### Messages do not reach other players

-   Confirm both players are connected.
-   Confirm both players are using the same channel.
-   If the issue persists, report it with the relevant log messages and
    reproduction steps.

### Reconnection takes a while

RGC uses exponential backoff with jitter to avoid repeated connection
attempts overwhelming the server. Delays can increase up to a 60-second
cap and reset after a stable connection.

### The cursor disappears or chat focus is lost

-   If the OS cursor disappears over the panel, click outside the panel
    area.
-   If you Alt+Tab while chat mode is active, press `/` again after
    returning to the game if the input has lost focus.

## Building from source

These instructions are intended for developers with access to the RGC
source code.

### Requirements

-   Visual Studio 2022 Build Tools with MSVC.
-   CMake 3.15 or later.
-   vcpkg.
-   REFramework source and the dependencies specified by the project
    build files.

### Dependency setup

Example vcpkg setup on Windows:

``` bat
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

C:\vcpkg\vcpkg.exe install ixwebsocket:x64-windows-static
C:\vcpkg\vcpkg.exe install openssl:x64-windows-static
```

Configure `VCPKG_ROOT` and the plugin output directory in
`build_deploy.bat` as needed for your machine, then run:

``` bat
build_deploy.bat
```

Use the project's `CMakeLists.txt` and build scripts as the source of
truth for any additional build configuration.

## Architecture

-   **Client:** C++ REFramework plugin with an ImGui overlay.
-   **Server:** Python 3.13 with the `websockets` library, hosted on
    Render.
-   **Transport:** JSON messages over WebSocket secured with WSS/TLS.
-   **Runtime state:** chat and channel state is maintained by the
    server while it is running.

## Privacy and server use

RGC uses a player name and chat messages to provide the chat service.
These are sent to the RGC server and may be processed in server logs for
operation and troubleshooting. Do not send passwords, API keys, personal
information, or other sensitive data through chat.

The service is provided as-is. Availability, connection time, and
capacity may vary, especially while using free-tier hosting. Do not rely
on the current server capacity statement as a security guarantee or
service-level commitment.

## Feedback and bug reports

For a useful bug report, include:

1.  Relevant `[RGC]` log messages from
    `reframework/re2_framework_log.txt`.
2.  Your Monster Hunter Rise version.
3.  Your REFramework version.
4.  Steps to reproduce the issue.

Avoid posting personal information or unrelated log contents. Use the
project's designated issue tracker or release-page discussion channel,
if available.

## License

Rise Global Chat (RGC) is licensed under the **GNU General Public
License v3.0 (GPLv3)**. See [`LICENSE`](LICENSE) for the full license
text.

If you distribute a compiled RGC binary, GPLv3's applicable source-code
and notice requirements also apply. Make the corresponding source code
available to recipients in a manner that complies with GPLv3. Keeping a
repository private does not remove those distribution obligations.

### Third-party licenses

Third-party components retain their respective licenses and notices:

-   **REFramework** --- MIT
-   **IXWebSocket** --- BSD-3-Clause
-   **OpenSSL** --- Apache License 2.0
-   **Dear ImGui** --- MIT
-   **nlohmann/json** --- MIT
-   **MinHook** --- BSD-2-Clause

Include the applicable copyright notices, license texts, and any other
required notices for the versions and components actually distributed
with RGC. Verify the final release package against the licenses of the
exact dependencies and build configuration you ship.

## Credits

RGC uses REFramework and the third-party components listed above. Please
retain applicable upstream copyright and license notices when
redistributing those components.
