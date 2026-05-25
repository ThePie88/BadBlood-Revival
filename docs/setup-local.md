# Local setup — play without a domain

This guide gets `Dying Light: Bad Blood` running on your own PC with a
fully local server. No domain, no VPS, no port forwarding. Works for
single-player and for LAN multiplayer (you + friends on the same Wi-Fi).

For internet multiplayer across different networks you'll need a public
IP / domain — see [setup-vps](../server-linux/README.md) instead.

---

## What you'll have at the end

```
Your PC
├── BadBloodGame.exe  ◄─ patched, talks to "pls.dlbb.com"
│        │
│        │ hosts file: 127.0.0.1 pls.dlbb.com
│        ▼
├── stunnel  ◄─ listens on 127.0.0.1:443 with self-signed cert,
│        │     decrypts and forwards to plain HTTP
│        ▼
├── BadBlood-Revival server  ◄─ FastAPI on 127.0.0.1:80
│        │
│        ▼
└── SQLite DB  ◄─ accounts, items, friends, etc.
```

Goldberg Steam Emu handles matchmaking locally too — your LAN peers can
discover each other automatically.

---

## Prerequisites

- A legitimate Steam copy of `Dying Light: Bad Blood` (re-install it from
  your library if not on disk)
- Windows 10/11 (or Linux — instructions diverge slightly)
- Python 3.10+
- stunnel
- openssl

On Windows, install all three with winget:

```cmd
winget install Python.Python.3.12
winget install MichalTrojnara.Stunnel
winget install ShiningLight.OpenSSL.Light
```

On Linux:

```bash
sudo apt install python3 python3-venv python3-pip stunnel4 openssl
```

---

## Step 1 — Clone the repo

```cmd
git clone https://github.com/ThePie88/BadBlood-Revival.git
cd BadBlood-Revival
```

---

## Step 2 — Run setup-local

Windows:
```cmd
cd server
setup-local.bat
```

Linux/macOS:
```bash
cd server
./setup-local.sh
```

This:
- Verifies Python and stunnel are present
- Generates a self-signed certificate in `certs/`
- Installs Python dependencies (into a venv on Linux)
- Copies `stunnel.conf.template` → `stunnel.conf`

Re-running is idempotent — it won't overwrite existing certs.

---

## Step 3 — Add a hosts entry

The game has `pls.dlbb.com` baked into its binaries. We don't replace it;
we just point that hostname at localhost.

**Windows:**
1. Open Notepad **as Administrator**
2. File → Open → `C:\Windows\System32\drivers\etc\hosts`
   (choose "All files" filter if you can't see it)
3. Add this line at the end:
   ```
   127.0.0.1 pls.dlbb.com
   ```
4. Save

**Linux/macOS:**
```bash
echo '127.0.0.1 pls.dlbb.com' | sudo tee -a /etc/hosts
```

**Optional** — the patcher (step 5) can do this for you. If you'd rather
let it, skip this step.

---

## Step 4 — Start the server

Open two terminals.

**Terminal 1** (the FastAPI backend, plain user):

Windows:
```cmd
cd server
run.bat
```

Linux:
```bash
cd server
source venv/bin/activate
./run.sh
```

You should see `[goldberg-udp] relay listening on :47584` and uvicorn
listening on port 80.

**Terminal 2** (stunnel, run as Administrator/root for port 443):

Windows:
```cmd
cd server
"C:\Program Files (x86)\stunnel\bin\stunnel.exe" stunnel.conf
```

Linux:
```bash
cd server
sudo stunnel stunnel.conf
```

Test it works:

```cmd
curl -sk https://pls.dlbb.com/auth/login/steam/ -X POST -d "auth_session_ticket=test"
```

Expected:
```json
{"token":"<some 24-char hex>","enabled":3071}
```

---

## Step 5 — Patch your game

Make a copy of your DLBB install first (so Steam doesn't undo the patches):

```cmd
xcopy "C:\Program Files (x86)\Steam\steamapps\common\Dying Light Bad Blood" ^
      "C:\Games\DLBB-Revival" /E /I /H
```

Then unpack the Steam DRM stub on `BadBloodGame.exe` with
[Steamless v3.1.0.5](https://github.com/atom0s/Steamless/releases) — drop
the file onto Steamless.exe, replace the original with `.unpacked`.

Now run the patcher:

```cmd
cd patcher
python apply_patches.py --game-dir "C:\Games\DLBB-Revival" --local
```

The `--local` flag means:
- Hostname stays `pls.dlbb.com` (no string replacement)
- Only the byte patches are applied (EAC bypass, SSL verify off, HTTP gate, rpacz whitelist)
- The hosts entry is added if missing (skip with `--skip-hosts` if you already did step 3)

Backups go to `engine_x64_rwdi.dll.original` next to the patched file.
Re-run any time — the patcher is idempotent.

---

## Step 6 — Build the stubs and texture hook

These are tiny C/C++ files. Need MinGW-w64 GCC ([winlibs.com](https://winlibs.com/)
or MSYS2 `mingw-w64-x86_64-gcc`).

```cmd
cd stubs
build.bat
:: produces: BEClient_x64.dll, BEServer_x64.dll, EasyAntiCheat_x64.dll

cd ..\texture-hook
build.bat
:: produces: texture_hook.dll
:: also need lightfx_proxy.dll — see texture-hook/README.md
```

Drop into `C:\Games\DLBB-Revival\`:
- `BEClient_x64.dll`
- `BEServer_x64.dll`
- `EasyAntiCheat\EasyAntiCheat_x64.dll`
- `LightFX.dll` (the proxy you built from `lightfx_proxy.cpp`)
- `LightFX_original.dll` (rename the **real** LightFX.dll to this first)
- `texture_hook.dll`

---

## Step 7 — Get Goldberg Steam Emu

From <https://github.com/Detanup01/gbe_fork/releases> (maintained fork).
Use the **experimental** build. Drop in the game folder:

- `steam_api64.dll`
- `steam_api.dll`
- `steamclient64.dll`
- `steamclient.dll`
- `steam_interfaces.txt` (next to the dll — generate it via Goldberg's helper)

Also create/edit `steam_appid.txt` in the game folder:
```
480
```

(The original AppID 766370 is delisted; we use Spacewar/480 for compatibility.)

---

## Step 8 — Build and run the launcher

```cmd
cd launcher
build.bat
:: produces: PIE-LAUNCHER.exe
```

Copy `PIE-LAUNCHER.exe` to wherever you want — it doesn't need to live
inside the game folder. Run it, register an account, click PLAY.

The launcher writes Goldberg config files into `<game-dir>/steam_settings/`
based on your account and spawns `BadBloodGame.exe`.

---

## Playing solo

You can play with bots / practice mode without anything else. The
launcher writes `force_account_name.txt` and `force_steamid.txt`,
the game accepts you, you're in.

---

## Playing on LAN with friends

Each friend does steps 5-8 on their own PC (or you share the patched
files — but the patcher is the cleanest distribution model).

For matchmaking on the LAN, edit `launcher/src/main.cpp` and change:

```c
#define VPS_IP "127.0.0.1"
```

to your **LAN IP** (the one starting with `192.168.x.x` or `10.x.x.x`),
then rebuild. Distribute the new `PIE-LAUNCHER.exe` to friends.

This IP is written into `steam_settings/custom_broadcasts.txt` so
Goldberg can discover lobbies across the LAN.

The server itself only needs to run on **one** of the PCs (you can be
both the server host and a player). Friends point their hosts file at
that PC's LAN IP instead of 127.0.0.1:

```
192.168.1.50 pls.dlbb.com
```

---

## Troubleshooting

### `curl https://pls.dlbb.com/auth/login/steam/` returns "Connection refused"

stunnel isn't running. Step 4 Terminal 2.

### `curl` returns the response but the game still hangs at "Retrieving data from server"

- Confirm `engine_x64_rwdi.dll` was patched: SHA-256 it before and
  after the patcher run, they should differ.
- Check the patcher said `[byte] http_transport_gate  applied` —
  without that, the response is received but dropped.

### Hosts file edit "permission denied"

You opened Notepad without right-click → Run as Administrator. On
Linux you forgot sudo.

### Self-signed cert warning on `curl https://`

That's expected. The game's SSL verification is patched off, so it
accepts our self-signed cert. The `-k` flag in curl does the same on
the command line.

### Goldberg crashes / Steam appears / "Application load error"

Steamless step (`BadBloodGame.exe`) wasn't done. The Steam DRM stub
must be removed before anything else will work.

---

## Going public later

If you decide to host the server on a VPS for internet multiplayer:

1. Pick a 12-character domain (e.g. `pls.example.it`)
2. Re-run the patcher with `--server-host pls.example.it` (or restore
   from `.original` and re-patch fresh)
3. Edit launcher `SERVER_HOST` and `VPS_IP` and rebuild
4. Follow [`server-linux/README.md`](../server-linux/README.md)

Your local setup keeps working in parallel — they're independent.
