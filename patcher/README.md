# patcher/

Applies the byte-level patches that turn a vanilla `Dying Light: Bad Blood` install
into a client that talks to your own emulated server.

## What's here

- **`apply_patches.py`** — the patcher. Reads `patches.json` and edits the
  user's own game files in place. No Techland binaries are shipped with this
  repo; you bring your own from a legitimate Steam install.
- **`patches.json`** — declarative patch recipe (offsets, before/after bytes,
  hostname replacements). Edit this if a game update ever changes offsets.

## Prerequisites

1. You own `Dying Light: Bad Blood` on Steam (purchased before delisting).
2. The game is installed somewhere you can write to.
3. `BadBloodGame.exe` is **Steamless-unpacked**. The Steam DRM stub must be removed
   before any other patches will work. See [`docs/setup-game.md`](../docs/setup-game.md).
4. Python 3.10+ installed.

## Usage

### Local play (single PC or LAN, no domain)

```bash
python apply_patches.py --game-dir "C:/Games/DLBB-copy" --local
```

The `--local` flag keeps the hostname as `pls.dlbb.com` inside the binaries.
The patcher only applies the 5 byte patches (EAC bypass, SSL verify off,
HTTP gate, rpacz whitelist) and adds the hosts entry. Your local server
listens on 127.0.0.1 and the hosts file routes the traffic to it.

### Public hosting (your own 12-char domain)

```bash
python apply_patches.py \
    --game-dir "C:/Games/DLBB-copy" \
    --server-host pls.example.it
```

`--server-host` is constrained to 12 ASCII characters. The game has
`pls.dlbb.com` (12 chars) compiled into multiple places. We replace it
in-place without shifting bytes, so the new hostname **must** also be
12 chars.

Examples:
- `pls.foobar.it` → 13 chars ❌
- `pls.foo12.it`  → 12 chars ✅
- `dlbb.host.io`  → 12 chars ✅
- `pls.dlbb.eu`   → 11 chars ❌

If you can't fit your domain in 12 chars, either use `--local` and rely
on the hosts file, or pick a 12-char subdomain on a domain you own.

### Other flags

- `--dry-run` — show what would be patched without writing
- `--skip-hosts` — don't touch the system hosts file
- `--patches PATH` — use a custom patches.json

## What it does, in order

1. Validates `--server-host` is exactly 12 ASCII characters.
2. For each target file (`engine_x64_rwdi.dll`, `BadBloodGame.exe`):
   - Backs up to `.original` if no backup exists.
   - Applies every byte patch in `patches.json`. Each patch is checked against
     either "vanilla before" or "already patched after" — mismatch = abort.
   - Replaces `pls.dlbb.com` with the new hostname (every occurrence).
3. Updates the system hosts file with `127.0.0.1 pls.dlbb.com` (so stunnel can
   intercept on localhost). Requires admin/sudo. Skip with `--skip-hosts`.

## Patches applied (engine_x64_rwdi.dll)

| Offset    | Purpose                                                          |
|-----------|------------------------------------------------------------------|
| 0x6F73DE  | EAC launcher gate bypass (lets the game run without EAC)         |
| 0x6F95AA  | CURLOPT_SSL_VERIFYPEER = 0 (accept self-signed certs)            |
| 0x6F95C3  | CURLOPT_SSL_VERIFYHOST = 0 (accept hostname mismatch)            |
| 0x7152C6  | Force HTTP transport gate open (otherwise responses are dropped) |
| 0x5B7C72  | NOP rpacz filename whitelist (allows custom addon skins)         |
| string    | `pls.dlbb.com` → `<server-host>`                                 |

See [`docs/engine-patches.md`](../docs/engine-patches.md) for the reverse
engineering work that found these.

## Reverting

Restore the `.original` backups:

```bash
# On Windows (PowerShell)
Copy-Item engine_x64_rwdi.dll.original engine_x64_rwdi.dll -Force
Copy-Item BadBloodGame.exe.original BadBloodGame.exe -Force

# Remove hosts entry
# Edit C:\Windows\System32\drivers\etc\hosts and delete the BadBlood-Revival line.
```

## Troubleshooting

- **"bytes at offset 0xXXXXXX don't match either vanilla or patched"**: your
  `engine_x64_rwdi.dll` is a different version than the one this recipe was
  built against. Compare your SHA-256 to the one in `patches.json` (when filled
  in). The offsets need to be re-derived on a new build.

- **"replacement is N bytes, must be exactly 12"**: pick a 12-character hostname.

- **"hosts file not writable"**: run your terminal as Administrator (Windows)
  or with sudo (Linux/macOS).

- **Game still shows "Start using BadBloodGameLauncher.exe"**: the EAC bypass
  patch didn't take. Confirm `engine_x64_rwdi.dll.original` exists and the
  file you're patching is the right one.
