# HOW TO BUILD — Developer guide

This guide is for **developers** who want to compile BadBlood-Revival
from source, modify it, or contribute back. If you just want to play,
use [HOW_TO_PLAY.md](HOW_TO_PLAY.md) instead.

There's also `build_all.bat` at the repo root that runs every build
script in sequence and produces a `dist/` folder ready for upload to
GitHub Releases. Try that first; this guide explains what it does.

---

## What you'll build

| Output                          | From                          | Tool       |
|---------------------------------|-------------------------------|------------|
| `PIE-LAUNCHER.exe`              | `launcher/`                   | MinGW-w64 GCC |
| `BEClient_x64.dll`              | `stubs/src/beclient.c`        | MinGW-w64 GCC |
| `BEServer_x64.dll`              | `stubs/src/beserver.c`        | MinGW-w64 GCC |
| `EasyAntiCheat_x64.dll`         | `stubs/src/eac_x64.c`         | MinGW-w64 GCC |
| `EasyAntiCheat_x86.dll`         | `stubs/src/eac_x86.c`         | MinGW-w64 GCC (32-bit needed) |
| `texture_hook.dll`              | `texture-hook/texture_hook.cpp` | MinGW-w64 G++ |
| `LightFX.dll` (proxy)           | `texture-hook/lightfx_proxy.cpp` | MinGW-w64 G++ |
| `(optional) discord-rpc.dll` (proxy) | `texture-hook/discord_proxy.cpp` | MinGW-w64 G++ |
| Server (no build needed)        | `server/*.py`                 | Python — just `pip install` |
| Patcher (no build needed)       | `patcher/apply_patches.py`    | Python — just run it |

All in C/C++ except the Python pieces. Single C++ files + Dear ImGui
embedded as source. No CMake, no Visual Studio, no MSBuild — just
`build.bat` for each piece.

---

## Toolchain setup

### Required: MinGW-w64 GCC

You need a recent GCC (13.x or newer) for Windows.

**Recommended: winlibs.com**

1. Open <https://winlibs.com/>
2. Scroll to "Release versions". Pick the latest **GCC X.Y.Z + MinGW-w64
   Y.Z (UCRT) - release X**, **64-bit only**, **Win32 threads** or
   **POSIX threads** (either works for our code).
3. Download the **7z** archive
4. Extract to `C:\mingw64` (so you end up with `C:\mingw64\bin\g++.exe`)
5. Add `C:\mingw64\bin` to your PATH:
   - Win + R → `sysdm.cpl` → Advanced → Environment Variables
   - Under "User variables", select `Path` → Edit → New
   - Add `C:\mingw64\bin`
   - OK, OK, OK
6. Open a **new** Command Prompt (must be new — old ones won't see the PATH change)
7. Verify:
   ```cmd
   g++ --version
   windres --version
   ```
   Both should print version info.

**Alternative: MSYS2**

```bash
# In an MSYS2 MINGW64 shell:
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-binutils
```

Then add `C:\msys64\mingw64\bin` to PATH.

### Required: Python 3.10+

For the patcher and (optionally) the server.

Follow the install in [HOW_TO_PLAY.md Step 1a](HOW_TO_PLAY.md#1a-python-310-or-newer).

### Optional: 32-bit MinGW

Only needed if you want a real 32-bit `EasyAntiCheat_x86.dll`. The
default build uses x64 toolchain which produces a 64-bit DLL with an
x86 filename — works for the game, doesn't work for the EAC launcher
(which we don't use anyway).

From winlibs.com, pick a **32-bit** GCC release and extract to
`C:\mingw32`. Add `C:\mingw32\bin` to PATH (after `C:\mingw64\bin` so
the 64-bit one is found first by default).

### Optional: PyInstaller

For packaging the patcher as a `.exe` for users who don't have Python.

```cmd
python -m pip install pyinstaller
```

### Optional: 7-Zip

For producing the final release ZIP. Windows' built-in zip is fine too.

---

## Build everything in one shot

```cmd
cd C:\BadBloodRevival
build_all.bat
```

Output: `dist/BadBloodRevival-vX.X.zip` with everything users need.

If anything fails, the script aborts and tells you which component
broke. Continue reading to build pieces individually.

---

## Building each component

### Server (Python — no build, just pip install)

```cmd
cd server
python -m venv venv
venv\Scripts\activate.bat
pip install -r requirements.txt
```

Test:
```cmd
python main.py
:: Should bind to port 80. Ctrl+C to stop.
```

### Launcher (C++ + ImGui + DirectX 11)

```cmd
cd launcher
build.bat
```

Output: `PIE-LAUNCHER.exe` (~2-3 MB).

What build.bat does:
```cmd
windres src/resource.rc -o src/resource.o
g++ -O2 -mwindows -o PIE-LAUNCHER.exe ^
    src/main.cpp src/resource.o ^
    lib/imgui/imgui.cpp ^
    lib/imgui/imgui_draw.cpp ^
    lib/imgui/imgui_tables.cpp ^
    lib/imgui/imgui_widgets.cpp ^
    lib/imgui/backends/imgui_impl_win32.cpp ^
    lib/imgui/backends/imgui_impl_dx11.cpp ^
    -I lib/imgui -I lib/imgui/backends ^
    -ld3d11 -ldxgi -ldwmapi -ld3dcompiler -lwininet -lole32 -lshell32 ^
    -static -std=c++17
```

Configuration (before building) is in the top of `src/main.cpp`:
```c
#define SERVER_HOST     "127.0.0.1"   // launcher backend host
#define VPS_IP          "127.0.0.1"   // matchmaking relay IP
#define GAME_HOST       "pls.dlbb.com" // don't change
```

Change these for your deployment, then `build.bat`.

### Stubs (BattlEye + EAC)

```cmd
cd stubs
build.bat
```

Output in `out/`:
- `BEClient_x64.dll`
- `BEServer_x64.dll`
- `EasyAntiCheat_x64.dll`
- `EasyAntiCheat_x86.dll` (actually x64 unless you have the 32-bit toolchain — see note below)

Configuration: `NEW_HOST` in `src/eac_x64.c` and `src/dns_redirect.c`.
Default `pls.dlbb.com` = no-op redirect (local setup).

**Why is EAC x86 actually x64?**

The build.bat uses the same x64 GCC for everything. The "x86" DLL is
just named that way because the EAC launcher (32-bit) looks for it by
filename. The game itself only loads the x64 EAC, so it works. If you
need real x86 (e.g. you want EAC launcher to actually start), install
the 32-bit MinGW (see "Optional: 32-bit MinGW") and adjust the build
command in build.bat:
```cmd
:: Add to build.bat after the x64 builds:
i686-w64-mingw32-gcc -shared -O2 -o out\EasyAntiCheat_x86.dll src\eac_x86.c -static
```

### Texture hook + LightFX proxy

```cmd
cd texture-hook
build.bat
```

Output: `texture_hook.dll`.

Then build the proxy DLL of your choice. The default and most reliable
injection point is LightFX:

```cmd
g++ -shared -O2 -o LightFX.dll lightfx_proxy.cpp -static -std=c++17
```

Optional proxies (alternatives if LightFX timing doesn't work):
```cmd
g++ -shared -O2 -o XINPUT1_3.dll xinput_proxy.cpp -static -std=c++17
g++ -shared -O2 -o XGamepad.dll xgamepad_proxy.cpp -static -std=c++17
g++ -shared -O2 -o discord-rpc.dll discord_proxy.cpp -static -std=c++17
```

You typically only need **one** proxy active at a time. Pick LightFX.

Configuration: `VPS_IP` (twice — string + 4-int) in `texture_hook.cpp`.

### Patcher (no build needed)

The patcher is Python:
```cmd
cd patcher
python apply_patches.py --help
```

If you want to ship a `.exe` for users without Python:

```cmd
cd patcher
pyinstaller --onefile --console apply_patches.py
:: produces dist/apply_patches.exe
```

The `.exe` is ~10 MB (Python runtime + script). Users can run it
without installing Python.

Bundle `patches.json` next to the `.exe` — the script looks for it in
the same directory.

---

## Verifying your build works

After building everything, test the integration end-to-end:

1. Make a copy of a vanilla DLBB install at `C:\DLBB-test\`
2. Steamless unpack `BadBloodGame.exe` (per HOW_TO_PLAY.md Step 4)
3. Run your patcher:
   ```cmd
   cd patcher
   python apply_patches.py --game-dir "C:\DLBB-test" --local --dry-run
   ```
   Confirm all 5 byte patches are detected. If `--dry-run` shows
   issues, fix before going further.
4. Drop your DLLs into the game folder.
5. Run the server.
6. Run your launcher.
7. Confirm registration + game launch + reaching main menu.

If steps 1-7 all work, your build is good.

---

## Troubleshooting builds

### `g++: command not found`

PATH isn't set. Verify in a **new** Command Prompt:
```cmd
where g++
```
Should print `C:\mingw64\bin\g++.exe`. If not, redo the PATH setup.

### `windres: command not found`

`windres` ships with MinGW-w64. If `g++` works but `windres` doesn't,
you have a partial install. Re-download the full MinGW-w64 release
from winlibs.com (not just gcc).

### `undefined reference to ImGui_ImplWin32_*`

The launcher build expects ImGui backends to be compiled in. Check
`lib/imgui/backends/imgui_impl_win32.cpp` exists. If you only have a
partial ImGui copy, re-clone the repo.

### `cannot find -ld3d11` or `-ldxgi`

Your MinGW doesn't have DirectX SDK headers. Get the winlibs build
(which includes them) or install windows-default-headers via MSYS2:
```bash
pacman -S mingw-w64-x86_64-headers-git
```

### Linker error about ImGui demo

Make sure you're NOT compiling `imgui_demo.cpp`. The launcher's
`build.bat` already excludes it. If you modified the build command,
remove that file.

### Patcher refuses to apply: "bytes at offset 0xXXXXXX don't match"

You're running it against a game version with different offsets. The
patcher is conservative — it refuses to patch if it doesn't recognize
the pre-patch bytes. If you really want to patch a different version,
either:
- Update `patcher/patches.json` with the new offsets after locating
  them manually (use IDA, Ghidra, or `objdump`)
- Set `"before": null` for unknown patches (forces trust mode — may
  break things)

### Texture hook crashes the game at startup

You changed `VPS_IP` but only in the `#define`, not in the 4-int
computation inside `Hooked_connect()`. Both need to match. The 4 ints
are `(127UL << 24) | (0UL << 16) | (0UL << 8) | 1UL` for `127.0.0.1`.

Or it might be that the hook is hitting code paths it shouldn't. Look
at `texture_hook.log` next to the DLL.

### Build succeeds but launcher fails to start: "missing VCRUNTIME140.dll"

You built without `-static`. Add `-static` to the g++ command. The
default `build.bat` already has it.

### My antivirus deletes the texture hook on build

DLL-injection patterns are a hot button for AVs. Either disable AV
during build/test (re-enable after!) or use a less aggressive AV like
Windows Defender with proper exclusions for your dev folder.

---

## Project structure

```
release/                       (= repo root, what you cloned)
├── server/                    Python FastAPI backend
│   ├── main.py                ~700 lines, all endpoints + Goldberg relay
│   ├── db.py                  ~700 lines, SQLite layer
│   ├── config.py              env-var configuration
│   ├── fixtures/              static JSON responses
│   ├── stunnel.conf.template
│   ├── setup-local.bat        one-stop local setup
│   └── ...
├── launcher/                  Win32 launcher
│   ├── src/main.cpp           ~2300 lines, single-file
│   ├── lib/imgui/             Dear ImGui sources
│   ├── lib/stb_image.h        for PNG loading
│   └── build.bat
├── patcher/                   Python byte-patcher
│   ├── apply_patches.py       ~400 lines
│   └── patches.json           declarative recipe
├── stubs/                     EAC + BattlEye stubs
│   ├── src/eac_x64.c          50 real EAC functions stubbed
│   ├── src/beclient.c
│   └── build.bat
├── texture-hook/              DLL hook engine
│   ├── texture_hook.cpp       ~700 lines: texture replace + DNS + SSL bypass
│   ├── lightfx_proxy.cpp      injection vector
│   └── build.bat
├── docs/                      reference docs
│   ├── engine-patches.md      every byte explained
│   ├── recon-findings.md      original DNS + EAC research
│   ├── technical-notes.md     Steamless, Goldberg, the launcher gate
│   ├── rpack-format.md        RP6L file format
│   └── known-issues.md        honest "what's broken" list
├── tools/                     research helpers (rpack tool, memscan, ...)
├── website/                   static landing page
├── server-linux/              Linux deployment helpers
├── README.md
├── HOW_TO_PLAY.md             user guide
├── HOW_TO_HOST.md             server operator guide
├── HOW_TO_BUILD.md            you are here
├── LICENSE                    Apache 2.0
├── NOTICE                     attribution per Apache 2.0 4(d)
└── CONTRIBUTING.md            PR process
```

---

## Coding style

No formal style guide. Match the existing file. General preferences:

- **C++**: 4 spaces, opening brace on same line, no exceptions, plain
  C-with-classes preferred over modern C++ template gymnastics
- **Python**: PEP 8-ish, 4 spaces, double-quoted strings unless single
  is genuinely clearer, type hints welcome
- **No emoji-heavy decoration** in code or commit messages
- **Imperative commit messages**: "fix X", not "fixed X" or "fixes X"

---

## Contributing

PRs welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for:
- What kind of PRs are wanted (and not)
- License grant on contributions (Apache 2.0)
- Code style notes
- Issue templates

---

## Questions

Open an issue at <https://github.com/ThePie88/BadBlood-Revival/issues>
with the `question` or `build-help` label.
