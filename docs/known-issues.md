# Known Issues

The honest list of what doesn't work, what's broken, and what's not
implemented. Cross-referenced from the README status table.

---

## Game features that don't work

### Leaderboards in-game show "Waiting for data" forever

**Where:** in-game leaderboard panel
**Root cause:** Suspected. The endpoint `/dlbb/leaderboards/{b}/{c}`
   returns real database rows, but `/dlbb/leaderboards` (the index)
   serves a Techland fixture capture that may be missing a field the
   current client requires.
**Reproduction:** Open the leaderboards screen in-game. The "Waiting for
   data" spinner never resolves.
**Status:** Open. Need a more complete Techland capture (theirs is now
   broken on `/auth/login/steam/` so we can't easily get one).
**Workaround:** None client-side.

### Profile stats panel shows all zeros

**Where:** in-game profile screen
**Root cause:** `db.get_playerdata()` returns `stats: []`. The wire shape
   of the `stats` array is unknown — every Techland capture we have is
   from fresh accounts with empty stats. Probably a typed-document array
   ("kills": int, "wins": int, "matches": int) but the keys aren't
   confirmed.
**Status:** Open. Needs a capture from an account with stats, or a
   reverse engineering pass through `gamedll_x64_rwdi.dll` to find
   the parser.
**Workaround:** None.

### Custom shop slots can't be added

**Where:** in-game shop
**Root cause:** The shop client reads its slot layout from
   `playerappearances.scr` inside `Data0.pak`. We can REPLACE a slot's
   skin (works — Crigrey skins do this), but adding a NEW slot (e.g. a
   13th outfit for a character that has 12) requires the client to allocate
   a new UI tile, which it refuses to do — the slot count appears to be
   capped client-side.
**Status:** Open. May require patching the gameplay DLL where the slot
   array is sized. Not investigated.
**Workaround:** Re-skin existing slots instead of adding new ones.

### Min players hardcoded to 6

**Where:** match start
**Root cause:** Hardcoded check in `gamedll_x64_rwdi.dll` that refuses
   to spin up a match with `< 6` players in the lobby.
**Status:** Open. Not patched. The check needs reverse engineering.
**Workaround:** Recruit 5+ friends via Discord.

### Post-match rewards untested

**Status:** Untested rather than broken. The `/dlbb/gameresults` endpoint
   computes and persists SC reward, but we've never run a real 6-player
   match to verify the end-of-match reward flow client-side.

---

## Cosmetic / minor bugs

### SCARS currency conversion sign error

**Where:** any place SC totals are displayed after a conversion
**Root cause:** Off-by-sign in the server's conversion logic.
**Status:** Known bug, easy fix, not yet done.

### Loot box purchase doesn't add item to inventory

**Where:** shop loot box purchase
**Root cause:** `POST /dlbb/shop/loot` returns `{pls_result:0, items:[]}`.
   The server doesn't currently generate an item drop. Should pick a
   random `oid` from a loot pool and add it to `player_items`.
**Status:** Known bug. Loot box workflow stub returns success-but-no-item.

---

## Launcher

### Friends list not pressure-tested

**Where:** launcher friends sidebar
**Root cause:** The server side is implemented (`/api/friends/heartbeat`,
   `/api/friends/list`, `/api/friends/requests`, `/api/friends/add`,
   `/api/friends/accept`, `/api/friends/decline`, `/api/friends/cancel`,
   `/api/friends/remove`, `/api/lobby/invite`, `/api/lobby/invites/poll`).
   The launcher has the UI and a polling thread. End-to-end concurrent
   testing with multiple real users hasn't been done.
**Status:** Likely working but unverified. Edge cases (player goes offline,
   game crashes, race on accept) are probably buggy.

### No auto-update for the launcher itself

The launcher updates the PATCH-PIE payload when the server's
`/api/version` changes, but it doesn't update its own .exe. To ship a
new launcher build you have to redistribute the .exe.

### No proxy config

Launcher uses WinINet, which honours the system-wide proxy settings but
doesn't expose its own dialog. People behind corporate proxies that
require auth may have trouble.

---

## Build system

### EAC x86 stub built as x64

`stubs/build.bat` produces `EasyAntiCheat_x86.dll` from the x64 toolchain,
so it's actually a 64-bit DLL. The 32-bit EAC launcher won't load it.
The game itself loads the x64 EAC stub and is fine — this only matters
if you're trying to make the EAC launcher (not the game) start.
**Fix:** install a 32-bit MinGW-w64 toolchain and adjust `build.bat`.

### Texture-hook `VPS_IP` duplicated

`texture-hook/texture_hook.cpp` has `VPS_IP` as a `#define` string AND
duplicated as four shift-ored bytes inside `Hooked_connect()`. Changing
the IP requires editing both. **Fix:** parse the string once at DLL load.

---

## Architectural debt

### Hostname must be exactly 12 characters

The patcher does an in-place ASCII replace of `pls.dlbb.com` (12 chars).
Replacement must also be 12 chars or binary alignment breaks. **Fix:**
PE section rebuild to relocate the string with a different length. Doable,
not done.

### `#define`s in C++ instead of runtime config

Launcher and texture-hook compile their server hostname / VPS IP in.
Changing servers requires a rebuild and redistribution.
**Fix:** runtime `launcher.cfg` / `texture_hook.cfg`. Discussed in TODO,
not implemented.

### Goldberg matchmaking depends on a single relay broadcast IP

`steam_settings/custom_broadcasts.txt` only takes one IP. If your VPS
goes down, matchmaking dies. **Fix:** Goldberg supports multiple, the
launcher just writes one.

### PIENUVO anti-tamper not implemented

The TODO in the original workspace describes a custom VM-based
anti-tamper. Nothing is implemented. The client is unprotected — by
design for a preservation project, but if abusers start ruining the
experience, this is the gap they'll exploit.

---

## Not bugs, just missing features

- **No web admin panel** — manage players from the SQLite CLI.
- **No mod workshop / browser** — players install texture packs manually.
- **No in-game chat** — Goldberg's built-in chat works in the lobby.
- **No party system** — friends invite to lobby, that's it.
- **No Discord rich presence** — `discord-rpc.dll` is forwarded by the
  hook but we don't drive it.
- **No telemetry recording** — endpoint just returns 200.
- **No crash reporter** — game crashes are silent.

If any of these get implemented, please send a PR.
