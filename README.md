# BadBlood-Revival

**Bring `Dying Light: Bad Blood` back from the dead.**

A clean-room server emulator, account-managing launcher, and client patcher
that restore online play to a game whose servers were shut down years ago.
Buy the game on Steam (if you still can) and play it again.

> ⚠ Bring your own legitimate copy of the game. This project does NOT
> redistribute any Techland binary, asset, or data file. You patch your
> own install. See the [Legal](#legal-game) section.

---

## 🎯 Which guide do you need?

This project has three audiences. Pick the one that matches you and
follow that guide — don't try to read everything at once.

### 🎮 I just want to PLAY the game
👉 **[HOW_TO_PLAY.md](HOW_TO_PLAY.md)** — Ultra-detailed step-by-step
walkthrough. Zero prior knowledge assumed. Every download link, every
command, every common error and its fix. ~15-30 minutes to set up.

### 🏠 I want to HOST a server (for myself, friends on LAN, or publicly)
👉 **[HOW_TO_HOST.md](HOW_TO_HOST.md)** — Three scenarios documented:
local single-PC, LAN with friends, public VPS with your own domain.
Covers DNS, TLS, firewall, backups, monitoring.

### 🛠️ I want to BUILD / modify / contribute to the code
👉 **[HOW_TO_BUILD.md](HOW_TO_BUILD.md)** — Toolchain setup
(MinGW-w64), component-by-component build instructions, troubleshooting,
project layout, contribution guidelines. Or run `build_all.bat` at the
repo root and let it do everything.

### 🎨 I only want CUSTOM TEXTURES / skins (the texture hook)
👉 **[texture-hook/TEXTURE_MODDING_GUIDE.md](texture-hook/TEXTURE_MODDING_GUIDE.md)**
— Complete copy-paste walkthrough for installing custom textures, from
"I own the game on Steam" to "my image is in-game." Note: seeing textures
in-game needs the full local setup (the hook routes the game through the
local server), so this guide includes that too.

The rest of this README is high-level overview + status. **Open the
guide above first.**

---

## Status

| Component                 | State            | Notes                                                          |
|---------------------------|------------------|----------------------------------------------------------------|
| Auth (Steam ticket → token) | ✅ Working      | Format reverse-engineered from live Techland proxy capture     |
| Profile + inventory       | ✅ Working       | SQLite-persisted, 30 default items + 30k SC for new accounts   |
| Shop + purchases          | ✅ Working       | Full Techland catalog (48KB JSON), persistent across sessions  |
| Matchmaking (P2P)         | ✅ Working       | Goldberg Steam Emu, UDP+TCP relay for CGNAT traversal          |
| Multi-instance (same PC)  | ✅ 4 tested      | Per-copy mutex patch needed                                    |
| Launcher (register/login) | ✅ Working       | Win32 + ImGui, single .exe                                     |
| Client patcher            | ✅ Working       | Python script, applies byte patches to user's vanilla install  |
| Texture replacement       | ✅ Working       | DLL hook on `RCreateTexture2D`, drop-in DDS in `mods/textures/`|
| Custom addon skins        | ✅ Working       | rpacz texture delivery + material injection (Crigrey skin)     |
| EAC / BattlEye            | ✅ Stubbed       | Empty DLLs returning success — no real anti-cheat              |
| Friends list              | ⚠ Server done, launcher polling | UI in launcher, endpoints in server, needs concurrent-user test |
| Telemetry                 | ⚠ Stubbed       | Endpoints return 200, nothing is recorded                      |
| Post-match rewards        | ⚠ Untested      | Requires ≥6 real players to trigger a match                    |
| Leaderboards in-game      | ❌ Stuck         | Game shows "Waiting for data" — index payload format unknown   |
| Profile stats panel       | ❌ Empty         | `playerdata.stats` array shape unknown                         |
| Custom shop slots         | ❌ Can't add     | Only existing slots can be re-skinned; new slot UI is locked   |
| Min players               | ❌ 6 hardcoded   | Can't start a match with fewer; in the game binary             |
| SCARS currency display    | 🐛 Sign error    | Conversion bug, cosmetic                                       |
| Loot box → inventory      | 🐛 Doesn't add   | Purchase goes through, item doesn't appear                     |

Legend: ✅ working — ⚠ partial / needs test — ❌ not working — 🐛 known bug

---

## Architecture

```
                                                    Player A (patched)
                                                    BadBloodGame.exe
                                                          │
                                                          │ HTTPS to pls.dlbb.com
                                                          │ (hosts → 127.0.0.1)
                                                          ▼
                                              ┌──────────────────────┐
                                              │ stunnel (TLS term)   │
                                              │ :443  ►  :80         │
                                              └──────────┬───────────┘
                                                         │ plain HTTP
                                                         ▼
                          ┌─────────────────────────────────────────────┐
                          │ FastAPI server (server/main.py)             │
                          │ - /auth/login/steam/                        │
                          │ - /dlbb/playerdata, /dlbb/shop/...          │
                          │ - /api/register, /api/login (launcher)      │
                          │ - /api/friends/*, /api/lobby/invites/*      │
                          │ - /api/patch (zip of patched client files)  │
                          │ - Goldberg relay (UDP+TCP :47584)           │
                          └──────────────────┬──────────────────────────┘
                                             │
                                             ▼
                                       SQLite (data/dlbb.db)
                                       accounts, items, friends, sessions

         Player A ◄═══════════════ P2P (Goldberg / Steam) ═══════════════► Player B
         (matchmaking is entirely peer-to-peer; server only provides the relay)
```

- **PLS Server** (this repo) — Profile / Login / Shop. Python/FastAPI.
- **Matchmaking** — 100% peer-to-peer via Goldberg Steam Emu.
- **Gameplay netcode** — P2P between players. The server is never in the
  hot loop of a match.

---

## Repository layout

```
release/
├── server/         FastAPI backend. Cross-platform: runs on Windows or Linux.
├── server-linux/   Linux public-hosting helpers (systemd, certbot, ufw).
├── server-windows/ Windows public-hosting helpers (Task Scheduler, win-acme).
├── launcher/       Win32 launcher (single C++ file + ImGui). Build with MinGW.
├── stubs/          Empty replacement DLLs for EAC + BattlEye.
├── texture-hook/   DLL that intercepts RCreateTexture2D and rewrites textures.
├── patcher/        Python script that applies byte patches to your vanilla game.
├── docs/           Reverse-engineering notes, rpack format, recon findings.
├── tools/          Research helpers (rpack_tool, memscan, patch verifier).
├── website/        Optional static landing page (HTML/CSS/JS).
└── README.md       you are here
```

Each folder has its own `README.md` with build / run / configure details.

---

## Requirements

### For everyone
- A **legitimate copy** of `Dying Light: Bad Blood` purchased on Steam before
  the game was delisted. We never redistribute Techland's files. Owning the
  game is on you.

### For local play (single PC or LAN — no domain needed)
- Python 3.10+
- stunnel (Windows: `winget install MichalTrojnara.Stunnel`; Linux: `apt install stunnel4`)
- openssl (Windows: `winget install ShiningLight.OpenSSL.Light`; Linux: usually preinstalled)
- MinGW-w64 GCC to build the launcher and DLL stubs (Windows: from [winlibs.com](https://winlibs.com/) or MSYS2)

### For public hosting (internet multiplayer with your own domain)
- All of the above, plus:
- A 12-character ASCII domain pointing to your server — see
  [the 12-character hostname constraint](#the-12-character-hostname-constraint)
- A TLS certificate (Let's Encrypt recommended)
- Ports 80, 443, 47584 open to the internet

---

## 30-second overview

The 9 high-level steps. **Don't try to do this from the README** — open
the relevant guide above for the detailed walkthrough. This is just to
show you what's involved before you commit.

1. Buy / install Dying Light: Bad Blood from your Steam library
2. Run Steamless on `BadBloodGame.exe` (removes DRM stub)
3. Set up a local server (`server/setup-local.bat` does this in one click)
4. Edit your hosts file: `127.0.0.1 pls.dlbb.com`
5. Start the server + stunnel
6. Run the patcher on your game folder
7. Drop pre-built DLLs (or build them yourself) into the game folder
8. Add Goldberg Steam Emu files
9. Run the launcher → register → PLAY

Total time from scratch: about 30 minutes. Most of it is downloads.

---

## The 12-character hostname constraint

**Only matters if you're hosting publicly** with a custom domain. For
local play, the hostname stays `pls.dlbb.com` and this section doesn't
apply.

The game has the string `pls.dlbb.com` (12 chars) compiled into multiple
places in `engine_x64_rwdi.dll`. The patcher replaces it **in place** —
no byte shifting — so the replacement must be **exactly 12 ASCII characters**.

Examples:
- `pls.foobar.it` → 13 chars ❌
- `pls.foo12.it`  → 12 chars ✅
- `dlbb.host.io`  → 12 chars ✅
- `pls.dlbb.eu`   → 11 chars ❌

Pick a (sub)domain that fits, or skip the hostname patch entirely (use
`--local`) and rely on the hosts file. Future work could rewrite PE
sections to lift the constraint; not done yet.

---

## Reverting

```cmd
:: Game files
cd "C:\Games\DLBB-copy"
copy engine_x64_rwdi.dll.original engine_x64_rwdi.dll /Y
copy BadBloodGame.exe.original BadBloodGame.exe /Y

:: Hosts file: edit C:\Windows\System32\drivers\etc\hosts and remove
:: the "BadBlood-Revival" marked line.
```

Restoring the original Steam install: just verify game files via Steam
(right-click in library → Properties → Installed Files → Verify), it'll
re-download anything you modified.

---

## Reverse engineering & the patches

The full byte-patch list and the reasoning behind it:

| Offset (engine_x64_rwdi.dll) | Purpose                                              |
|------------------------------|------------------------------------------------------|
| 0x6F73DE                     | EAC launcher gate bypass                             |
| 0x6F95AA                     | CURLOPT_SSL_VERIFYPEER = 0                           |
| 0x6F95C3                     | CURLOPT_SSL_VERIFYHOST = 0                           |
| 0x7152C6                     | Force HTTP transport gate open                       |
| 0x5B7C72                     | NOP rpacz filename whitelist                         |
| ASCII string                 | `pls.dlbb.com` → your 12-char hostname               |

How these were found, and the broader story of the auth-response format
reverse engineering via live Techland proxy capture: see
[`docs/technical-notes.md`](docs/technical-notes.md) and
[`docs/recon-findings.md`](docs/recon-findings.md). For the rpack file
format (skin / texture archives): [`docs/rpack-format.md`](docs/rpack-format.md).

---

## Configuration matrix

Every component has a small set of values you'll want to change.
**Defaults are tuned for local play** — if all you want is to run the
game on your own PC, you can leave most of these alone.

| Component       | What to set                       | Default (local play)        | Where                                        |
|-----------------|-----------------------------------|-----------------------------|----------------------------------------------|
| server          | `DLBB_DOMAIN`, ports, DB path     | `pls.dlbb.com`, 80, 443     | `server/.env` (copy from `.env.example`)     |
| stunnel         | cert paths, accept/connect ports  | `certs/`, 443→80            | `server/stunnel.conf` (auto-created by setup-local) |
| launcher        | `SERVER_HOST`, `VPS_IP`           | `127.0.0.1`, `127.0.0.1`    | `launcher/src/main.cpp` (top of file)        |
| texture-hook    | `VPS_IP` (twice — string + ints)  | `127.0.0.1`                 | `texture-hook/texture_hook.cpp`              |
| stubs           | `NEW_HOST`                        | `pls.dlbb.com` (= no-op)    | `stubs/src/eac_x64.c`, `stubs/src/dns_redirect.c` |
| patcher         | (CLI only)                        | `--local`                   | `--game-dir`, `--server-host`                |
| website         | `BB_API_URL` (optional override)  | same-origin                 | `window.BB_API_URL` global                   |

The C++ ones are compile-time constants today. For LAN play with friends,
edit `VPS_IP` to your LAN IP (e.g. `192.168.1.50`) and rebuild the launcher
+ texture-hook. For public hosting, set both to your VPS IP / domain.

Moving these to runtime configs is on the wishlist.

---

## Known issues — in detail

### Leaderboards in-game show "Waiting for data"

The endpoint `/dlbb/leaderboards/{board_id}/{category_id}` returns real
data from the SQLite DB, but the index endpoint (`/dlbb/leaderboards`) is
served from a Techland fixture capture that may be missing fields the
current client expects. The "Waiting" message is the game refusing to
populate the list. Format diffing across Techland captures hasn't resolved
the missing field.

### Profile stats panel always empty

`db.get_playerdata()` returns `stats: []`. The real Techland captures we
have are all from fresh accounts and also have `stats: []`. We never
captured a server response from an account with played matches. The wire
format is unknown — could be a flat array, could be MongoDB-style typed
documents.

### Custom shop slots not addable

The shop client UI parses the static catalog from `playerappearances.scr`
(inside `Data0.pak`). Adding a `Replace()` block with a new ID works and
the new skin shows up in shop. Adding a *new slot* (e.g. a 13th outfit
slot for a character that only has 12) isn't accepted by the client —
the slot UI is locked client-side. Workaround is to re-skin an existing
slot.

### Min players = 6 hardcoded

The game won't start a match with fewer than 6 players. Hardcoded in the
gameplay DLL, not the engine. Reverse engineering the check hasn't been
done. Players have run sessions by recruiting via Discord.

### Friends — server done, launcher integration in flight

`/api/friends/*` and `/api/lobby/invites/*` are implemented in
`server/main.py` and `server/db.py` (heartbeat, list, requests, add,
accept, decline, cancel, remove, invite, poll). The launcher has the UI
(sidebar, tabs, popup, toasts) and a polling thread, but end-to-end with
concurrent real users hasn't been pressure-tested. Edge cases around
"player is in match" detection (which currently relies on a temp-file
bridge between launcher and game) likely have bugs.

### EAC x86 stub built as x64

`stubs/build.bat` produces `EasyAntiCheat_x86.dll` from the x64 toolchain,
so the resulting DLL is actually 64-bit. The 32-bit EAC launcher won't
load it. The game itself loads the x64 stub and is fine. Only a problem
if you're trying to make the EAC launcher (not the game) start. Fix:
install a 32-bit MinGW toolchain.

### Anti-tamper (PIENUVO)

The parent project's TODO describes a custom VM-based anti-tamper layer.
None of it is implemented. The client is not protected against
modification; this is by design for a community preservation project.

---

## Credits

- **MrPie (Filip Otto)** — server, launcher, patcher, hooks, reverse engineering
- **Crigrey** — addon skin pipeline, texture pack creation
- **The community** behind Dying Light reverse engineering tools (rpack
  research, mesh format docs)
- Built on the shoulders of:
  - [Goldberg Steam Emulator](https://gitlab.com/Mr_Goldberg/goldberg_emulator) (LGPL)
  - [Steamless](https://github.com/atom0s/Steamless) by atom0s (MIT)
  - [QuickBMS](https://aluigi.altervista.org/quickbms.htm) by Luigi Auriemma
  - [stunnel](https://www.stunnel.org/) (GPL)
  - [Dear ImGui](https://github.com/ocornut/imgui) by Omar Cornut (MIT)
  - [FastAPI](https://fastapi.tiangolo.com/) by Sebastián Ramírez (MIT)
  - [stb_image](https://github.com/nothings/stb) by Sean Barrett (Public Domain)

See [docs/credits.md](docs/credits.md) for the full attribution list.

---

## License

This project is licensed under the **Apache License 2.0** — see
[`LICENSE`](LICENSE) and [`NOTICE`](NOTICE).

You're free to use, modify, fork, and redistribute the code. The license
requires that you:

- Keep the copyright notices and the `NOTICE` file
- State if you've modified any file
- Don't use "BadBlood-Revival" or "MrPie" as your project's name or
  endorsement without permission

Contributions back to the canonical repo at
<https://github.com/ThePie88/BadBlood-Revival> are encouraged — see
[`CONTRIBUTING.md`](CONTRIBUTING.md).

---

## Legal (game)

`Dying Light: Bad Blood`, the Dying Light franchise, all associated assets,
and the original PLS backend protocol are property of **Techland S.A.**
This project is not affiliated with, endorsed by, or sponsored by Techland.

This repository contains **only**:
- Original code (server, launcher, patcher, hooks, stubs) — Apache 2.0 licensed.
- Documentation of byte offsets and protocol shapes that we discovered by
  observing our own legitimately-purchased game running and by passive
  traffic capture against the (still-up-but-broken) Techland endpoints.

This repository does **NOT** contain:
- Any Techland binary (game executable, engine DLL, asset archive).
- Any Techland data file (.pak, .rpack, .rpacz, .scr, .mp, .msh, .dat).
- Any modified Techland binary.

Users of this project must already own a legitimate copy of the game
through their own Steam library purchase made prior to the game's delisting.
The patcher operates exclusively on files already on the user's disk.

For takedown requests, please open a GitHub issue.

This project exists for **game preservation and educational purposes**.
The game's official servers have been offline since approximately 2018.
This emulator restores the ability to play a game that the original
publisher has stopped supporting and removed from sale, while requiring
that users still hold valid licenses to the underlying software.

---

## Contributing

Pull requests welcome — see [`CONTRIBUTING.md`](CONTRIBUTING.md) for the
process and the "help wanted" list.

Quick summary of areas where help is especially wanted:
- Leaderboard index format reverse engineering
- Player stats format reverse engineering
- Mesh-level Player_09 glove overlay removal
- 32-bit MinGW build for the x86 EAC stub
- PE section expansion to lift the 12-character hostname constraint
- Launcher runtime config (move the `#define`s into `launcher.cfg`)
- Server endpoint tests

Open an issue first if you want to land something big.

---

*BadBlood-Revival — created by MrPie. Not affiliated with Techland.*
