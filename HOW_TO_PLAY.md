# HOW TO PLAY — Step-by-step guide for players

This guide gets you from **"I own Dying Light: Bad Blood on Steam"** to
**"I'm playing the game"** with no assumed knowledge. You don't need to
know how to compile code, use a terminal, or edit configuration files.

If you get stuck at any step, see [Troubleshooting](#troubleshooting) at
the bottom — every common error is listed with the exact fix.

> If you want to host a server for others, see [HOW_TO_HOST.md](HOW_TO_HOST.md).
> If you want to build / modify the code, see [HOW_TO_BUILD.md](HOW_TO_BUILD.md).

---

## What you'll end up with

A patched copy of `Dying Light: Bad Blood` that:
- Doesn't need Techland's servers (they're offline)
- Talks to your own local server emulator (or someone else's public server)
- Lets you play single-player and LAN multiplayer

You will need to **own a legitimate Steam copy of the game** purchased
before it was removed from sale. This project does not bypass DRM or
provide pirated game files.

---

## Before you start — checklist

| Need                                     | Have it? |
|------------------------------------------|----------|
| Windows 10 or 11                         | ☐        |
| A Steam account that owns DLBB           | ☐        |
| ~20 GB free disk space                   | ☐        |
| Administrator access on your PC          | ☐        |
| Internet connection                      | ☐        |

If any of these are missing, stop here — the rest won't work.

---

## Two paths

### Path A — Easy (when binaries are released)

1. Go to <https://github.com/ThePie88/BadBlood-Revival/releases>
2. Download the latest `BadBloodRevival-vX.X.zip`
3. Extract to a folder (e.g. `C:\BadBloodRevival`)
4. Skip to [Step 3](#step-3--copy-your-game-files)

If the Releases page is empty, the maintainer hasn't published binaries
yet. Use Path B below.

### Path B — Build it yourself

Requires installing developer tools. See [HOW_TO_BUILD.md](HOW_TO_BUILD.md)
first, come back here when you have a built copy of everything.

---

## Step 1 — Install required software

You'll install **three** small programs. All free, no accounts needed.

### 1a. Python 3.10 or newer

Used to run the patcher script that modifies the game.

1. Open this link in your browser: <https://www.python.org/downloads/>
2. Click the big yellow **"Download Python 3.X.X"** button
3. Run the installer that downloads
4. **IMPORTANT**: tick the checkbox **"Add Python to PATH"** at the
   bottom of the first installer screen. If you miss this, nothing
   below will work.
5. Click "Install Now"
6. Wait for it to finish, click "Close"

**Verify it worked:**
- Press `Win + R`, type `cmd`, press Enter (this opens a black terminal window)
- Type `python --version` and press Enter
- You should see something like `Python 3.12.7`

If you see "command not found" or "python is not recognized", you didn't
tick "Add Python to PATH". Reinstall and make sure to tick it.

### 1b. stunnel

Used to handle the encrypted HTTPS connection between the game and the
server. Without it, the game can't talk to the emulator.

**Easy way (Windows 10/11):**
1. Press `Win + R`, type `cmd`, press Enter
2. Type this command exactly and press Enter:
   ```
   winget install MichalTrojnara.Stunnel
   ```
3. If it asks "agree to source agreements", press `Y` and Enter
4. Wait for it to finish

**Manual way (if winget doesn't work):**
1. Go to <https://www.stunnel.org/downloads.html>
2. Download "stunnel-X.XX-win64-installer.exe"
3. Run it, accept all defaults

**Verify it worked:**
- Open `C:\Program Files (x86)\stunnel\bin\` in File Explorer
- You should see `stunnel.exe` in there

### 1c. OpenSSL (for generating a certificate)

Used to create a security certificate. The game's certificate validation
is disabled by the patches, but stunnel still needs *some* cert to start.

1. Press `Win + R`, type `cmd`, press Enter
2. Type:
   ```
   winget install ShiningLight.OpenSSL.Light
   ```
3. Press `Y` if asked
4. Wait

**Verify it worked:**
- Close that terminal, open a new one (this picks up the new PATH)
- Type `openssl version` and press Enter
- You should see something like `OpenSSL 3.X.X`

---

## Step 2 — Get BadBlood-Revival

You need the project's files on your PC.

### Easy way — download the ZIP

1. Open <https://github.com/ThePie88/BadBlood-Revival>
2. Click the green **"<> Code"** button
3. Click **"Download ZIP"**
4. Save the file (e.g. to your Downloads folder)
5. Right-click the downloaded ZIP → "Extract All..."
6. Extract to `C:\BadBloodRevival` (or wherever you want, but remember
   the path)
7. Inside, you'll see folders: `server/`, `launcher/`, `patcher/`,
   `stubs/`, `texture-hook/`, `docs/`, plus `README.md`

### Power-user way — git clone

If you already have git installed:
```cmd
git clone https://github.com/ThePie88/BadBlood-Revival.git C:\BadBloodRevival
```

From here on, this guide assumes the folder is at `C:\BadBloodRevival`.
If you put it somewhere else, adjust the paths.

---

## Step 3 — Copy your game files

Steam will re-verify and replace files we modify if we patch the game
directly inside the Steam folder. So we make a copy elsewhere.

### 3a. Find your Steam install

1. Open Steam
2. Right-click "Dying Light: Bad Blood" in your library
3. Click **"Properties"**
4. Click **"Installed Files"** (left sidebar)
5. Click **"Browse"** — this opens the game folder
6. Note the full path at the top (e.g.
   `C:\Program Files (x86)\Steam\steamapps\common\Dying Light Bad Blood`)

### 3b. Make a working copy

1. Press `Win + E` to open File Explorer
2. Navigate to one level above your DLBB folder (the `common\` folder)
3. Right-click **"Dying Light Bad Blood"** → **Copy**
4. Navigate to `C:\` (or wherever you have space)
5. Right-click empty space → **Paste**
6. The copy is being made — wait. It's ~15 GB, takes a few minutes.
7. Rename the copy to `DLBB-Revival` so you don't confuse it with the
   Steam original

From here on, **`C:\DLBB-Revival` is your working game folder.**

---

## Step 4 — Remove the Steam DRM stub from BadBloodGame.exe

The game's executable has a Steam-specific protection that prevents it
from running outside Steam. We need to remove that wrapper.

### 4a. Download Steamless

1. Open <https://github.com/atom0s/Steamless/releases>
2. In the latest release (top of the page), find
   **"Steamless.v3.X.X.X.-.by.atom0s.zip"**
3. Click that filename to download
4. Right-click the downloaded zip → "Extract All..."
5. Extract to e.g. `C:\Tools\Steamless`

### 4b. Unpack BadBloodGame.exe

1. Open the folder where you extracted Steamless
2. Double-click `Steamless.exe`
3. The Steamless window opens — it has a "Browse" button or a place to drop a file
4. Click "..." (the file picker) and navigate to `C:\DLBB-Revival`
5. Select `BadBloodGame.exe` and click "Open"
6. Click **"Unpack File"** (or similar — the main button)
7. Wait a few seconds — you'll see a green success message
8. A new file appears: `C:\DLBB-Revival\BadBloodGame.exe.unpacked`

### 4c. Swap the files

1. Open `C:\DLBB-Revival` in File Explorer
2. Right-click `BadBloodGame.exe` → **Rename** → `BadBloodGame.exe.packed`
3. Right-click `BadBloodGame.exe.unpacked` → **Rename** → `BadBloodGame.exe`

You now have:
- `BadBloodGame.exe` — the unpacked, runnable version (~975 KB)
- `BadBloodGame.exe.packed` — the original (~1.15 MB), kept as a backup

---

## Step 5 — Set up the server (one-time)

The server runs locally on your PC and pretends to be Techland's backend.

### 5a. Run the setup script

1. Open File Explorer at `C:\BadBloodRevival\server`
2. Double-click `setup-local.bat`
3. A black window opens and runs through checks. You should see:
   ```
   [1/4] Checking Python...      [OK]
   [2/4] Checking stunnel...     [OK]
   [3/4] Self-signed certificate... [OK]
   [4/4] Installing Python dependencies... [OK]
   SETUP COMPLETE
   ```
4. Press any key to close

If any step shows `[!!]`, see [Troubleshooting](#troubleshooting).

### 5b. Edit the hosts file (one-time)

The hosts file is a Windows system file that tells your PC how to find
servers. We need to tell it that `pls.dlbb.com` (the original Techland
server) is actually your own PC.

1. Press the Windows key
2. Type `notepad`
3. **IMPORTANT**: right-click "Notepad" → **"Run as administrator"**
4. Click "Yes" if Windows asks for permission
5. In Notepad: File → Open
6. In the "File name" box at the bottom, paste:
   ```
   C:\Windows\System32\drivers\etc\hosts
   ```
7. Click "Open"
8. Add this line at the very end of the file (on its own line):
   ```
   127.0.0.1 pls.dlbb.com
   ```
9. File → Save (or `Ctrl + S`)
10. Close Notepad

**Verify it worked:**
- Open a regular Command Prompt (`Win + R`, type `cmd`, Enter)
- Type:
  ```
  ping pls.dlbb.com
  ```
- You should see "Reply from 127.0.0.1" — that means it's pointing at
  your own PC now.

---

## Step 6 — Start the server

The server has two parts that must both be running. Open **two**
separate Command Prompt windows.

### 6a. Terminal 1 — the backend (regular Command Prompt)

1. Press `Win + R`, type `cmd`, Enter
2. In the new black window, type:
   ```
   cd C:\BadBloodRevival\server
   ```
3. Then type:
   ```
   run.bat
   ```
4. You should see something like:
   ```
   ========================================
     BadBlood-Revival Server
   ========================================
   2026-XX-XX [INFO] [goldberg-udp] relay listening on :47584
   2026-XX-XX [INFO] PLS Emulator running — Created by MrPie
   INFO:     Started server process [12345]
   INFO:     Application startup complete.
   INFO:     Uvicorn running on http://0.0.0.0:80 (Press CTRL+C to quit)
   ```
5. **Leave this window open.** Closing it stops the server.

### 6b. Terminal 2 — stunnel (Command Prompt as ADMINISTRATOR)

stunnel needs admin rights because port 443 is "privileged" on Windows.

1. Press the Windows key
2. Type `cmd`
3. Right-click "Command Prompt" → **"Run as administrator"**
4. Click "Yes"
5. In the admin black window, type:
   ```
   cd C:\BadBloodRevival\server
   ```
6. Then type (one long line — copy-paste it):
   ```
   "C:\Program Files (x86)\stunnel\bin\stunnel.exe" stunnel.conf
   ```
7. You should see stunnel starting. It might minimize itself to the
   system tray (look for an "S" icon near the clock).
8. **Leave this running too.** Right-click the tray icon → "Exit" only
   when you're done playing.

### 6c. Test the server works

1. Open a **third** Command Prompt (normal, not admin)
2. Type:
   ```
   curl -k https://pls.dlbb.com/auth/login/steam/ -X POST -d "auth_session_ticket=test"
   ```
3. You should see:
   ```
   {"token":"...","enabled":3071}
   ```

If you see "Couldn't resolve host" → step 5b didn't work. Re-edit hosts file.
If you see "Connection refused" → stunnel isn't running. Step 6b.
If you see the JSON response → you're golden, move on.

---

## Step 7 — Patch the game files

This applies the byte-level modifications that let the game work with
our server.

1. Open Command Prompt (`Win + R`, `cmd`, Enter)
2. Type:
   ```
   cd C:\BadBloodRevival\patcher
   ```
3. Then type (one line — copy the whole thing):
   ```
   python apply_patches.py --game-dir "C:\DLBB-Revival" --local
   ```
4. You should see output like:
   ```
   ======================================================================
   BadBlood-Revival — Game Client Patcher
   ======================================================================
     game-dir:    C:\DLBB-Revival
     mode:        LOCAL (hostname kept as 'pls.dlbb.com', routed via hosts file)
     server-host: pls.dlbb.com
     patches:     ...\patches.json

   >>> Patching engine_x64_rwdi.dll
       size before: ... bytes
       sha256 before: ...
       [backup] engine_x64_rwdi.dll.original created
       [byte ] eac_launcher_check_bypass        applied
       [byte ] ssl_verify_peer_off              applied
       [byte ] ssl_verify_host_off              applied
       [byte ] http_transport_gate              applied
       [byte ] rpacz_whitelist_nop              applied
       [strng] pls_hostname                     skipped (local setup)
       sha256 after: ...

   >>> Updating system hosts file (local mode)
     [hosts] entry already present (marker 'BadBlood-Revival' found)

   DONE.
   ```
5. If any patch shows `[ERROR]`, see [Troubleshooting](#troubleshooting).

---

## Step 8 — Drop in the DLL stubs and hook

These DLLs disable the anti-cheat (which can't validate anyway because
Techland's servers are dead) and let us replace textures.

You need pre-built copies of these files. If you used the Release ZIP
from Path A, they're already in your BadBloodRevival folder under
`prebuilt/`. If you built them yourself per HOW_TO_BUILD.md, you know
where they are.

Copy these files into `C:\DLBB-Revival\`:

| Source                              | Destination                                       |
|-------------------------------------|---------------------------------------------------|
| `BEClient_x64.dll`                  | `C:\DLBB-Revival\BEClient_x64.dll`                |
| `BEServer_x64.dll`                  | `C:\DLBB-Revival\BEServer_x64.dll`                |
| `EasyAntiCheat_x64.dll`             | `C:\DLBB-Revival\EasyAntiCheat\EasyAntiCheat_x64.dll` |
| `LightFX.dll` (the proxy)           | `C:\DLBB-Revival\LightFX.dll`                     |
| `texture_hook.dll`                  | `C:\DLBB-Revival\texture_hook.dll`                |

Before you copy `LightFX.dll`, **rename the existing one**:
1. Open `C:\DLBB-Revival`
2. Find `LightFX.dll`
3. Right-click → Rename → `LightFX_original.dll`
4. NOW copy the new `LightFX.dll` into that folder

Confirm dialog "Replace or skip files?" — choose **Replace the files
in the destination**.

---

## Step 9 — Add Goldberg Steam Emu

Goldberg is what makes the game think you're logged into Steam without
needing the real Steam.

1. Open <https://github.com/Detanup01/gbe_fork/releases>
2. Download the latest **experimental** Windows build (named like
   `emu-win-experimental-XXXX.zip`)
3. Extract it somewhere temporary (e.g. Desktop)
4. From the extracted folder, copy these files into `C:\DLBB-Revival\`:
   - `steam_api64.dll`
   - `steam_api.dll`
   - `steamclient64.dll`
   - `steamclient.dll`
   - `steam_interfaces.txt`

Choose **Replace the files in the destination** when asked.

5. Open `C:\DLBB-Revival\steam_appid.txt` in Notepad (NOT as admin, regular)
6. Delete everything in it
7. Type `480` (just those three characters)
8. Save and close

(This makes Goldberg use Steam's free public AppID for compatibility.)

---

## Step 10 — Build & run the launcher

The launcher is what you click to register, log in, and start the game.

If you used the Release ZIP, the launcher (`PIE-LAUNCHER.exe`) is in
that ZIP — just copy it somewhere convenient. Skip ahead to step 10c.

If you're building yourself, see [HOW_TO_BUILD.md](HOW_TO_BUILD.md) for
how to build `PIE-LAUNCHER.exe`, then come back.

### 10c. First-time login

1. Double-click `PIE-LAUNCHER.exe`
2. The launcher window opens
3. Click **"Register"**
4. Choose a username and password
5. Click **"Create account"** — should succeed
6. Click **"Login"** with the same credentials
7. The launcher will:
   - Tell the server you're logging in
   - Write `steam_settings/force_account_name.txt` (your username)
   - Write `steam_settings/force_steamid.txt` (your session ID)
   - Write `steam_settings/custom_broadcasts.txt` (the matchmaking relay)

### 10d. PLAY

1. Click **"PLAY"** on the launcher
2. The game should start within a few seconds
3. You should reach the main menu

You're playing! :tada:

---

## Step 11 — Subsequent sessions

Once everything is set up, every time you want to play:

1. Open `C:\BadBloodRevival\server\` and double-click `run.bat`
2. Open a **second** Command Prompt as Administrator and run:
   ```
   "C:\Program Files (x86)\stunnel\bin\stunnel.exe" C:\BadBloodRevival\server\stunnel.conf
   ```
3. Double-click `PIE-LAUNCHER.exe`
4. Login → PLAY

You can make a `play.bat` on your desktop that starts both (the server
script + stunnel) in one click. Example contents:
```cmd
@echo off
start "" cmd /k "cd C:\BadBloodRevival\server && run.bat"
timeout /t 3 /nobreak >nul
start "" cmd /k "\"C:\Program Files (x86)\stunnel\bin\stunnel.exe\" \"C:\BadBloodRevival\server\stunnel.conf\""
timeout /t 3 /nobreak >nul
start "" "C:\BadBloodRevival\launcher\PIE-LAUNCHER.exe"
```

Save as `play.bat` on your desktop, double-click to start everything.

---

## Troubleshooting

### "Application load error V:0000065432"
Steam is open. Close Steam (right-click tray icon → Exit), try again.

### "Application load error 3:0000065432"
You skipped Step 4 (Steamless). The Steam DRM stub is still in place.
Redo Step 4.

### "Start the game using BadBloodGameLauncher.exe" popup
The engine patch at offset `0x6F73DE` didn't take effect.
- Open Command Prompt
- Run: `cd C:\BadBloodRevival\patcher` then
  `python apply_patches.py --game-dir "C:\DLBB-Revival" --local --dry-run`
- Look at the line `[byte ] eac_launcher_check_bypass` — if it says
  "skipped (already patched)" you're fine. If it says "ERROR", your
  engine_x64_rwdi.dll has different bytes than expected.
- Restore from backup: in `C:\DLBB-Revival\`, copy
  `engine_x64_rwdi.dll.original` over `engine_x64_rwdi.dll`, then re-run
  the patcher.

### "Retrieving data from server" hangs forever (in-game)
- stunnel isn't running, or the hosts file entry is wrong, or the
  server backend isn't running.
- Run the test in Step 6c and check which.

### "Dying Light: Bad Blood is already running" but it isn't
Game crashed previously and left a stale mutex.
- Restart your PC, or
- Open Task Manager (Ctrl+Shift+Esc), find any leftover BadBloodGame
  process, end it.

### Game crashes immediately when clicking PLAY
1. Did you do Step 9 (Goldberg)? The game can't start without it.
2. Did you put files in the **right** folder? `C:\DLBB-Revival\`, not
   the Steam folder.
3. Is `steam_appid.txt` content exactly `480` with no spaces or newlines?

### "Empty inventory" crash at main menu
Your account doesn't have the 30 default items the game expects. This
should be automatic for new accounts. Try:
1. Stop the launcher and game
2. Delete `C:\BadBloodRevival\server\data\dlbb.db`
3. Restart the server (Step 6a)
4. Register a fresh account in the launcher

### Multi-instance test on the same PC
The game refuses to launch twice because of a mutex (a OS-level "I'm
already running" flag). For testing only, edit the mutex string:
1. Open a SECOND copy of the game folder, e.g. `C:\DLBB-Revival-2`
2. In that copy's `BadBloodGame.exe`, find the ASCII string
   `ChromeEngine4DIMutex` (use a hex editor like HxD)
3. Change the last character (e.g. to `ChromeEngine4DIMute1`)
4. Save
5. Launch from both folders with different launcher accounts

### Patcher says "bytes at offset 0xXXXXXX don't match"
Your `engine_x64_rwdi.dll` is a different version than the patcher
expects. This shouldn't happen if you used the Steam version — but if
it does:
1. Verify game files via Steam (Properties → Installed Files → Verify)
2. Re-copy the game folder per Step 3
3. Re-run the patcher

### Hosts file change "Access Denied"
You opened Notepad without right-clicking → "Run as administrator".
Close it and re-open as admin (Step 5b sub-step 3).

### Goldberg matchmaking — friends on different networks can't see each other
You need a public IP relay for that — see [HOW_TO_HOST.md](HOW_TO_HOST.md)
for setting up a public VPS.

### My antivirus deletes texture_hook.dll
Some AVs flag DLL-injection patterns. Add the BadBloodRevival folder to
your AV's exceptions list, then restore the deleted files.

### I get "Permission denied" running stunnel
Step 6b said admin terminal. You're using a regular one. Close it,
open Command Prompt as Administrator, redo.

### Launcher won't open, says "VCRUNTIME140.dll missing"
Install the Visual C++ runtime:
1. Open <https://aka.ms/vs/17/release/vc_redist.x64.exe>
2. Run the downloaded installer
3. Try the launcher again.

### Anything else
Open an issue at
<https://github.com/ThePie88/BadBlood-Revival/issues> with:
- What you did (which step you were on)
- What you expected to happen
- What actually happened (copy any error messages exactly)
- The contents of `C:\BadBloodRevival\server\pls-emu.log`

---

## FAQ

**Q: Is this legal?**
A: You patch your own legitimately-owned game with our scripts. We
don't distribute Techland's files. The legal status of running an
emulated server for a discontinued game is murky in some jurisdictions;
the project is for game preservation. Your call.

**Q: Will I get banned from Steam?**
A: Goldberg replaces the Steam API in memory. Steam itself doesn't see
anything different. EAC for this game is dead so there's nothing to
ban from. We've never heard of anyone being banned for this, but no
guarantees.

**Q: Can I play with friends?**
A: Yes — same network (LAN), straightforward. Different networks
(internet), you or someone needs a public VPS — see
[HOW_TO_HOST.md](HOW_TO_HOST.md).

**Q: Why exactly 6 players to start a match?**
A: Hardcoded in the game by Techland. Nobody has reverse engineered
that check yet. PRs welcome.

**Q: Will my custom skins show in-game for other players?**
A: No — textures are replaced locally on your machine only. Other
players see the default textures.

**Q: How do I update when the project changes?**
A: Re-download the ZIP from GitHub (Step 2), keep your `server/data/`
folder (your accounts). For the game-side files, the patcher is
idempotent — re-run it on a fresh game copy.

**Q: Server is offline / I can't reach the public server**
A: This guide is for **local play** — you run the server on your own
PC. If you were trying to connect to someone else's public server,
that's their problem. Set up local play (this guide) as a fallback.

**Q: My question isn't here**
A: Open an issue at <https://github.com/ThePie88/BadBlood-Revival/issues>
or check [docs/known-issues.md](docs/known-issues.md).
