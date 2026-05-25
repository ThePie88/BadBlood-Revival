# launcher/

The launcher is a Win32 / Direct3D 11 / Dear ImGui app that handles:

- Account registration + login (talks to server `/api/register`, `/api/login`)
- Friends list (sidebar UI — server endpoints already exist; integration
  in-progress, see Known Issues)
- Patch download + version check (`/api/version`, `/api/patch`)
- Game launch with Goldberg `force_account_name` / `force_steamid` written
  from server-returned session

Source is a single `main.cpp` (~2300 lines) plus stb_image and Dear ImGui
under `lib/`. No CMake, no MSBuild — just `build.bat`.

## Configure before building

Edit the header of `src/main.cpp`:

```c
#define SERVER_HOST     "pls.example.it"   // your server hostname
#define VPS_IP          "0.0.0.0"          // your server public IP
```

`GAME_HOST` should stay `pls.dlbb.com` — it's the original Techland hostname
baked into the patched engine, redirected via hosts file to 127.0.0.1 where
stunnel listens. Don't change it unless you know what you're doing.

`SERVER_HOST` must match the hostname you passed to `patcher/apply_patches.py`
via `--server-host`. Must be 12 ASCII characters (same constraint as the
hostname patch — keeps things consistent).

## Build (Windows, MinGW-w64)

```cmd
build.bat
```

Output: `PIE-LAUNCHER.exe` next to `build.bat`.

Requires a working `g++` and `windres` in PATH. Tested with:
- MinGW-w64 GCC 13.2 from winlibs.com
- MSYS2 mingw-w64-x86_64-gcc package

For Visual Studio: a `.sln` is not provided. The single-file architecture
makes it trivial to drop into a new VS project — link the same libs
(`d3d11`, `dxgi`, `dwmapi`, `d3dcompiler`, `wininet`, `ole32`, `shell32`)
and add the imgui sources.

## Runtime files

When the launcher runs, it creates next to the .exe:

- `pie_launcher.dat` — cached username, server URL, etc. (binary)
- `pie_version.txt` — last applied patch version (text)
- `PATCH-PIE/` — extracted patch payload downloaded from server (read-only
  after extraction)

Delete these to reset state.

## Login flow

1. User enters username + password.
2. Launcher POSTs `/api/login` to `SERVER_HOST`.
3. Server returns `{username, session_id, pls_id, token}`.
4. Launcher writes:
   - `steam_settings/force_account_name.txt` = username
   - `steam_settings/force_steamid.txt` = session_id
   - `steam_settings/custom_broadcasts.txt` = VPS_IP (for Goldberg matchmaking)
5. Launcher spawns `BadBloodGame.exe`.

## Known issues

- **Friends list UI exists, integration partial** — server endpoints
  `/api/friends/*` and `/api/lobby/invites/*` are implemented (see
  `server/main.py`), launcher polls them, but the UI behaviour needs more
  testing with real concurrent users.
- **No auto-update for the launcher itself** — only for the patch payload.
- **No proxy support** — launcher uses WinINet which honours system proxy
  settings but doesn't expose its own config.

## Anti-tamper (PIENUVO) — not implemented

The brief in the parent project's `TODO.md` describes a custom VM-based
anti-tamper system. None of it is implemented in this repo. The launcher
is plain code.
