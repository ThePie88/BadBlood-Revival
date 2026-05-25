# texture-hook/

The texture hook is a DLL injected into `BadBloodGame.exe` via a proxy
`LightFX.dll`. At runtime it:

1. **Replaces textures** — intercepts `RCreateTexture2D` in `rd3d11_x64_rwdi.dll`
   and substitutes pixel data with custom DDS files from
   `<game>/mods/textures/`. This is how custom skins work without repacking
   `Data0.pak` or rpack archives.

2. **Redirects DNS/connect** — catches `getaddrinfo`, `GetAddrInfoW`,
   `gethostbyname`, and `connect` calls that would reach Techland's CloudFront
   range, and points them at your server. Belt-and-braces over the hosts
   file because some code paths inside the game (and EAC stubs) bypass the
   OS resolver.

3. **Disables SSL cert validation** — hooks `CertVerifyCertificateChainPolicy`
   to always return success. Required because we use self-signed certs.

## What's here

| File | Role |
|------|------|
| `texture_hook.cpp`    | The hook engine. Compiles to `texture_hook.dll`. |
| `lightfx_proxy.cpp`   | DLL-of-the-same-name as the original `LightFX.dll`. Acts as a load-time proxy: forwards all exports to the real LightFX (renamed to `LightFX_original.dll`) and side-loads `texture_hook.dll`. |
| `discord_proxy.cpp`   | Discord RPC proxy — same idea, for `discord-rpc.dll`. Optional. |
| `xinput_proxy.cpp`    | `XINPUT1_3.dll` proxy — alternative injection point if LightFX doesn't load early enough. |
| `xgamepad_proxy.cpp`  | Same for `XGamepad.dll`. |
| `beclient_stub.cpp`   | BattlEye stub variant that side-loads the hook. |
| `build.bat`           | MinGW-w64 build script. |

You only need ONE injection vector. The default and most reliable is
`lightfx_proxy.cpp` → `LightFX.dll`. The others exist as fallbacks if you
hit timing issues.

## Configure before building

Edit `texture_hook.cpp`:

```c
#define VPS_IP        "0.0.0.0"            // your server public IP
```

⚠ **Important**: `VPS_IP` is currently duplicated as four ints inside
`Hooked_connect()` (search for `vpsIp = (0 << 24)`). Update both. A
future revision should parse the string once at DLL load.

## Build

```cmd
build.bat
```

Output: `texture_hook.dll`. Then build the proxy of your choice:

```cmd
g++ -shared -O2 -o LightFX.dll lightfx_proxy.cpp -static -std=c++17
```

The build.bat covers `texture_hook.dll` only. Proxy DLLs need their own
commands (one-liners, see source headers).

## Setup in the game folder

```
<game>/
├── BadBloodGame.exe
├── LightFX.dll              ← our proxy
├── LightFX_original.dll     ← original Techland LightFX (renamed)
├── texture_hook.dll         ← built here
└── mods/
    └── textures/
        ├── player_9_jacket_gold_fpp_dif.dds
        └── ...
```

DDS files must be `1024×1024 R8G8B8A8_UNORM`, standard 128-byte DDS header,
filename matches the in-game texture name exactly.

## What gets replaced

Currently the hook intercepts every `RCreateTexture2D` and checks the
texture name (read from `descriptor+0x38`) against the contents of
`mods/textures/`. Any match → pixel data at `descriptor+0x50` is overwritten
with the file's pixels.

This is per-texture, not per-skin. You can replace any 1024×1024 RGBA
texture the engine loads. Different per-skin glove colors work because
each skin uses a different texture name.

For mesh-level changes (e.g. the leather glove overlay on Player_09 Agent),
the hook can't help — those need real rpack editing.

## Anti-cheat

`texture_hook.dll` is injected via DLL proxy and is detectable by any
serious anti-cheat. EAC and BattlEye are stubbed in this project (see
`stubs/`) so detection isn't an issue here. If you ever re-enable AC,
expect bans.
