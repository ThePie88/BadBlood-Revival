# stubs/

Empty replacement DLLs for the anti-cheat layers that the game won't run
without. None of these do any actual integrity checking — they only export
the symbols the game / EAC launcher try to resolve, returning success.

## Stubs included

| File              | Replaces                | Exports |
|-------------------|-------------------------|---------|
| `beclient.c`      | `BEClient_x64.dll`      | `Init`, `GetVer` |
| `beserver.c`      | `BEServer_x64.dll`      | `Init`, `GetVer` |
| `eac_x64.c`       | `EasyAntiCheat_x64.dll` | 50 real EAC functions (Cerberus_*, GameClient_*, ClientAuth_*, ThirdPartyLauncher_*, NetProtectClient_*) |
| `eac_x86.c`       | `EasyAntiCheat_x86.dll` | (32-bit) |
| `dns_redirect.c`  | optional `version.dll` proxy | IAT-hooks `getaddrinfo` |

`eac_x64.c` also includes the DNS redirect hook for belt-and-braces coverage.

## Configure before building

Edit the top of `eac_x64.c` and `dns_redirect.c`:

```c
#define NEW_HOST "pls.example.it"   // your 12-char hostname
```

`OLD_HOST` should stay `pls.dlbb.com`.

## Build

### Windows (MinGW-w64)
```cmd
build.bat
```

### PowerShell
```powershell
.\build.ps1
```

Produces in `out/`:
- `BEClient_x64.dll`
- `BEServer_x64.dll`
- `EasyAntiCheat_x64.dll`
- `EasyAntiCheat_x86.dll` (if x86 toolchain available)

## Drop into game folder

```
<game>/
├── BEClient_x64.dll              ← stub
├── BEServer_x64.dll              ← stub
└── EasyAntiCheat/
    ├── EasyAntiCheat_x64.dll     ← stub
    └── EasyAntiCheat_x86.dll     ← stub
```

Keep `.original` backups of the real ones first.

## Known issue: eac_x86.c

The build script currently compiles `EasyAntiCheat_x86.dll` as x64 because
the workspace lacks a 32-bit GCC. If the EAC launcher needs the real 32-bit
file (it usually doesn't — the game loads x64 directly), you'll need to
install a mingw-w64 i686 toolchain.

## libconfig exports

The real `EasyAntiCheat_x64.dll` exports ~227 functions from an embedded
libconfig C++. The stub omits those — the game has never been observed
calling them. If a future game patch starts depending on them, you'd see
"missing export" errors at load and could append no-op stubs to `eac_x64.c`.

## Detection

These stubs WILL be detected by anyone running real EAC. The whole point
of BadBlood-Revival is that the game's central EAC infrastructure is dead;
there's no live attestation server to ban anyone from. Don't try to use
these against a working anti-cheat.
