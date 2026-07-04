# Dying Light: Bad Blood — Custom Texture Guide (Beginner Edition)

## 1. What this is — and an honest scope warning

This guide shows you how to put your own images into *Dying Light: Bad Blood* as in-game textures. Once it is set up, you drop a specially-formatted image file into a folder, launch the game, and see your image painted onto a character's jacket/arms (first-person view). Nothing is permanent, and you can undo everything by deleting a few files.

**Please read this before you start — it will save you from getting stuck:**

Your goal is textures. But the texture hook this project ships **cannot run by itself**. When the game loads the hook, the hook automatically redirects the game's network calls to a local server, disables SSL certificate checks, and (if launched from the launcher) tries to join a lobby. On top of that, the safe game-patch step below writes a line into your Windows `hosts` file that sends `pls.dlbb.com` to your own PC (`127.0.0.1`). **If nothing is listening there, the game hangs on a "retrieving data" screen and you cannot get in-game to see any texture.**

So the honest truth is: **to reach a screen where your texture is visible, you must bring up the small local server too.** This guide therefore walks you through the **full local setup** (game patch + Steam emulator + anti-cheat stubs + local server + launcher) and *then* the texture part. It is more steps than a "textures only" guide would promise, but it is the setup that actually works. Every step is copy-paste, in strict order.

Two things this guide deliberately keeps simple:

- **You do not need to compile anything for the normal path.** The project already ships the two DLLs you need (`LightFX.dll` and `texture_hook.dll`), pre-built. You will copy them, not build them. (If you *want* to build them yourself, that is in the optional Appendix A.)
- **You still need Python once**, only to run the small, safe patch script. That is the only tool you truly must install.

---

## 2. What you need first

**You must already own:**

- **Dying Light: Bad Blood** on Steam, installed on your PC.
- A **Windows PC** (Windows 10 or 11).

**Free tools you will download (each step below tells you exactly where):**

| Tool | What it is for |
|------|----------------|
| **Python 3.10 or newer** | Runs one small script that safely edits the game so it can start on its own. |
| **Steamless** | Unwraps the game's `.exe` so it can launch without Steam's protection. **Mandatory.** |
| **gbe_fork (Goldberg Steam Emu)** | A tiny stand-in for Steam so the game can start outside Steam. **Mandatory.** |
| **texconv** | Converts a normal image (PNG) into the exact `.dds` texture format the game needs. |
| **The project files** | You already have these — you are reading this from the project folder. The two DLLs, the patch script, the server, and example textures are all inside it. |

You do **not** need a compiler for the normal path. Take the tools one at a time, in the order below. Do not skip ahead.

**Turn on file extensions first (do this once, now).** This makes the rename steps later safe:

1. Open **File Explorer**.
2. Click the **View** menu at the top.
3. Tick **File name extensions** (on Windows 11 it's under **View → Show → File name extensions**).

Now you can see and correctly edit `.exe`, `.dll`, and `.dds` endings, so you won't accidentally create something like `BadBloodGame.exe.exe`.

---

## 3. Install Python (the only tool you must install)

1. Go to **https://www.python.org/downloads/** and download the latest Python for Windows.
2. Run the installer. **Very important:** on the first screen, tick the checkbox **"Add python.exe to PATH"** before clicking **Install**.
3. Open a Command Prompt (**Windows key + R**, type `cmd`, **Enter**) and verify:

```
python --version
```

You should see something like `Python 3.12.x`. If instead you see `'python' is not recognized`, reinstall Python and make sure you ticked **"Add python.exe to PATH."**

---

## 4. Find the project files (you already have them)

You are reading this from the project folder, so you already have everything the project provides. You do **not** need to download or clone anything from GitHub for the normal path.

Two locations inside the project matter. Open File Explorer and confirm they exist:

- The pre-built texture DLLs and example textures:

```
release\texture-hook
```

  This folder must contain `LightFX.dll`, `texture_hook.dll`, `build.bat`, and a `mods\textures` folder full of example `.dds` files.

- The patch script:

```
release\patcher
```

  This folder must contain `apply_patches.py` and `patches.json`.

If you only have a stray copy of the guide and not the whole project folder, get the full project folder first — the DLLs, script, server, and example textures all live inside it. The whole project is at **https://github.com/ThePie88/BadBlood-Revival** (green **Code** button → **Download ZIP**), and a pre-built copy of the binaries is attached to the latest release at **https://github.com/ThePie88/BadBlood-Revival/releases**.

---

## 5. Make a working copy of the game — never touch the Steam original

You will modify game files. **Never** modify the copy Steam manages: Steam's file check would undo your work and could re-download files.

1. In Steam, right-click **Dying Light Bad Blood** → **Manage** → **Browse local files**. This opens the game folder (it contains `BadBloodGame.exe`).
2. Go up one level, copy the whole **Dying Light Bad Blood** folder, and paste it somewhere simple. For the rest of this guide the copy lives at:

```
C:\DLBB-Revival
```

3. **Confirm the copy is a full game folder, not just the exe.** Open `C:\DLBB-Revival` and check that this file is present:

```
C:\DLBB-Revival\engine_x64_rwdi.dll
```

   This DLL is the file the game patch actually edits. If it isn't there, you copied the wrong folder (or only the exe) — redo the copy so `C:\DLBB-Revival` contains the entire game, including `engine_x64_rwdi.dll`.

4. From now on, everything happens in `C:\DLBB-Revival`, never in the Steam folder.

---

## 6. Unpack the game executable with Steamless (mandatory)

Without this step the game refuses to start (you get an "Application load error"), so no texture can ever appear.

1. Download **Steamless v3.1.0.5** by atom0s from **https://github.com/atom0s/Steamless/releases** (get the file named like `Steamless.v3.1.0.5.-.by.atom0s.zip`). Extract the ZIP anywhere, e.g. `C:\Tools\Steamless`.
2. Run `Steamless.exe`.
3. Click the **"..."** / browse button and select your working-copy executable:

```
C:\DLBB-Revival\BadBloodGame.exe
```

4. Click **Unpack File**. Wait for the green success message. A new file appears next to the original named exactly:

```
BadBloodGame.exe.unpacked
```

   (Steamless appends `.unpacked` — it does **not** add a second `.exe`. If you're looking for a `.unpacked.exe`, that file does not exist.)

5. In File Explorer, go to `C:\DLBB-Revival` and do this exact rename, in order (you turned on file extensions in Section 2, so you can see the endings):
   - Rename `BadBloodGame.exe` → `BadBloodGame.exe.packed` (keeps a backup of the original).
   - Rename `BadBloodGame.exe.unpacked` → `BadBloodGame.exe`.

You now have:
- `BadBloodGame.exe` — the unpacked, runnable version (~975 KB).
- `BadBloodGame.exe.packed` — the original (~1.15 MB), kept as a backup.

The unpacked `BadBloodGame.exe` being smaller is expected.

---

## 7. Apply the game patch (lets it boot and reach the server)

The project includes a Python script that safely edits the game so it can start standalone and talk to the local server. It makes a backup first, refuses to run if the bytes don't match, and can be run again harmlessly.

**What this patch does, and why the full set:** the patch script applies **five** small edits to `engine_x64_rwdi.dll`:

- **The launcher-check bypass** (`eac_launcher_check_bypass`, offset `0x6F73DE`) is the one that lets the game start at all instead of demanding `BadBloodGameLauncher.exe`. This is the only edit strictly required just to reach a rendering screen.
- The other four (two SSL-verify-off edits, the HTTP transport gate, and the rpacz whitelist NOP) plus the `hosts`-file entry are for the game to reach and trust your **local server**. You need them because the hook redirects the game to that server anyway — without the server reachable and trusted, the game hangs. So apply the full set.

Steps:

1. You must run this as **Administrator** (it edits the Windows `hosts` file). Press the **Windows key**, type `cmd`, right-click **Command Prompt**, choose **Run as administrator**.
2. Go to the patcher folder. Type `cd` followed by a space, then paste the full path to your `release\patcher` folder, then press **Enter**. For example:

```
cd "C:\path\to\project\release\patcher"
```

3. (Optional but reassuring) Do a dry run first — it checks everything and changes nothing:

```
python apply_patches.py --game-dir "C:\DLBB-Revival" --local --dry-run
```

   **What you should see:** for `engine_x64_rwdi.dll` the script prints one `[byte ]` status line per edit — **five** of them — plus one `[strng]` line, then a hosts-file note, then a line beginning with `DONE.`. In `--local` mode a couple of lines will say things like `skipped (already patched)` or `skipped (local setup: hostname kept ...)` — that is normal, not an error. The only thing that means trouble is a line containing **`ERROR`** or `[skip] engine_x64_rwdi.dll not found`. If you see `not found`, your game copy is missing that DLL — go back to Section 5, step 3.

4. Now apply the patch for real:

```
python apply_patches.py --game-dir "C:\DLBB-Revival" --local
```

   Confirm it finishes with a `DONE.` line and no `ERROR`. This is your known-good baseline; if you experiment later and break something, re-running this restores it.

---

## 8. Install the Steam emulator and anti-cheat stubs

The game will not start outside Steam without a Steam stand-in, and it expects the anti-cheat modules to be present. Both are included/handled here.

### 8.1 Goldberg Steam Emu (gbe_fork)

1. Download **gbe_fork** from its releases page: **https://github.com/Detanup01/gbe_fork/releases**. Get the Windows release archive (named like `emu-win-release-*.7z` or `.zip`). Extract it.
2. Inside the extracted files, open the **release** folder, then the **experimental** subfolder, then the **x64** folder. Copy these files from there into `C:\DLBB-Revival` (right next to `BadBloodGame.exe`), overwriting when asked:

```
steam_api64.dll
steamclient64.dll
steamclient.dll
```

   (If your download also contains `steam_api.dll` and `steam_interfaces.txt` in the same x64 folder, copy those too. All of these go into `C:\DLBB-Revival` — the game root.)

3. Create a plain text file in `C:\DLBB-Revival` named exactly `steam_appid.txt` containing exactly one line:

```
480
```

   `480` is intentional here — this project's Steam emulator uses Steam's public test app id (Spacewar) rather than the retail id, and the local server is built to match. Leave it as `480`.

### 8.2 Anti-cheat stubs

The game expects BattlEye/EAC modules to be present. This project ships stubbed versions (they satisfy the check without enforcing anything) in the project's `release\stubs\out` folder (they appear there after the stubs are built; a pre-built copy is also in the project's Release ZIP under `stubs\`).

1. Open `release\stubs\out` (or the `stubs\` folder from the Release ZIP). Copy every `.dll` it contains — for example:

```
BEClient_x64.dll
BEServer_x64.dll
EasyAntiCheat_x64.dll
```

2. Paste them into `C:\DLBB-Revival` (the game root, next to `BadBloodGame.exe`), overwriting when asked. `EasyAntiCheat_x64.dll` should go in `C:\DLBB-Revival\EasyAntiCheat\` if that subfolder exists; otherwise the game root is fine.

If a filename differs slightly from the list above, copy whatever `.dll` files are actually there — they are the stubs you need.

---

## 9. Install the texture hook (pre-built DLLs)

You do not build anything here — the project ships both DLLs ready to use (in `release\texture-hook`, or in the Release ZIP under `hooks\`). **Order matters:** do the rename before the copy.

### 9.1 Rename the game's real LightFX.dll first

In `C:\DLBB-Revival`, find the existing file:

```
LightFX.dll
```

Rename it to:

```
LightFX_original.dll
```

Do this **before** copying anything in. (The design requires it: our proxy takes the `LightFX.dll` name, and the original must stay reachable under the new name.)

### 9.2 Copy in the two pre-built DLLs

From the project's `release\texture-hook` folder (or the Release ZIP's `hooks\` folder), copy **both** of these into `C:\DLBB-Revival`:

```
LightFX.dll
texture_hook.dll
```

After this, `C:\DLBB-Revival` must contain all of:

```
BadBloodGame.exe
LightFX.dll              (the project's proxy)
LightFX_original.dll     (the renamed original)
texture_hook.dll         (the project's hook)
```

### 9.3 Create the textures folder

Inside `C:\DLBB-Revival`, create this exact folder path (a folder named `mods`, and inside it a folder named `textures`):

```
C:\DLBB-Revival\mods\textures
```

This is where your custom images go.

---

## 10. Start the local server (required — otherwise the game hangs)

The patch and hook both point the game at a local server. You must bring it up **before** launching the game, or the game will hang on "retrieving data." The server ships in the project's `release\server` folder.

### 10.1 One-time setup

1. Open File Explorer at the project's `release\server` folder.
2. Double-click `setup-local.bat`.
3. A black window runs through checks. You should see it finish with something like:

```
[1/4] Checking Python...      [OK]
[2/4] Checking stunnel...     [OK]
[3/4] Self-signed certificate... [OK]
[4/4] Installing Python dependencies... [OK]
SETUP COMPLETE
```

   If any line shows `[!!]`, follow the message it prints (usually "install stunnel from the link shown", e.g. `winget install MichalTrojnara.Stunnel`).

### 10.2 Start it (two windows, both stay open)

The server has two parts. Leave **both** windows running the whole time you play.

**Window 1 — the backend (normal Command Prompt):**

```
cd "C:\path\to\project\release\server"
run.bat
```

You should see lines ending with `Uvicorn running on http://0.0.0.0:80`. Leave this window open.

**Window 2 — stunnel (Command Prompt as Administrator; port 443 needs admin):**

Open a Command Prompt **as administrator**, then:

```
cd "C:\path\to\project\release\server"
"C:\Program Files (x86)\stunnel\bin\stunnel.exe" stunnel.conf
```

stunnel may minimize to the system tray (an "S" icon near the clock). Leave it running.

### 10.3 Quick check that the server answers

Open a third normal Command Prompt and run:

```
curl -k https://pls.dlbb.com/auth/login/steam/ -X POST -d "auth_session_ticket=test"
```

You want to see a JSON reply like `{"token":"...","enabled":3071}`. If instead you see:
- **"Couldn't resolve host"** → the patcher's hosts entry didn't apply; re-run Section 7 as administrator.
- **"Connection refused"** → stunnel isn't running; redo Window 2 in Section 10.2.

---

## 11. Launch the game

The easiest way is the project launcher, which registers your account against your local server and starts the game with the right Steam-emulator settings.

1. The project launcher is `PIE-LAUNCHER.exe`. If you have the project's Release ZIP it is already built and included; otherwise it sits in `release\launcher\PIE-LAUNCHER.exe` after the project is built. Run it.
2. With the local server running (Section 10), click **Register**, pick a username and password, then **Login**. The launcher writes the Steam-emulator account files into `C:\DLBB-Revival\steam_settings\` and then launches the game.
3. Alternatively, once you have logged in once (so `steam_settings\` is populated) and the server is running, you can launch the game directly:

```
C:\DLBB-Revival\BadBloodGame.exe
```

The game should boot past the loading screen (because the server is answering) rather than hanging on "retrieving data."

---

## 12. Add your first custom texture

Prove the pipeline with an example skin **first**, then make your own art.

### 12.1 Beginner shortcut — use an example skin first (do this before anything else)

The project already includes ready-made example textures in `release\texture-hook\mods\textures`. Copying one in proves your whole setup works before you touch any image tools.

1. Open `release\texture-hook\mods\textures`.
2. Copy any one of the example files — for example `player_9_jacket_mural_fpp_dif.dds` — into your game's textures folder:

```
C:\DLBB-Revival\mods\textures\player_9_jacket_mural_fpp_dif.dds
```

3. Launch the game (Section 11) and check that the skin appears in first-person view (Section 13). If it does, your setup is correct and you can move on to making your own image. If it doesn't, fix that before converting your own art — the problem is in setup, not in your image.

### 12.2 The DDS format the game expects

When you make your own image, the final `.dds` must be:

- **Size:** exactly **1024 × 1024** pixels.
- **Format:** uncompressed 32-bit **R8G8B8A8_UNORM** (byte order Red, Green, Blue, Alpha).
- **Header:** the **legacy 128-byte** DDS header (not the newer "DX10" variant).
- **Mipmaps:** none.
- **Resulting file size:** about **4,194,432 bytes** (1024 × 1024 × 4 = 4,194,304 pixel bytes + a 128-byte header).

The `texconv` command below produces exactly this. If your result is a byte or two off from 4,194,432 due to a different tool version, it is still usually fine — the hook only writes as many bytes as the game's texture buffer holds and ignores the rest.

### 12.3 Get texconv

Download `texconv.exe` from Microsoft's DirectXTex releases: **https://github.com/microsoft/DirectXTex/releases** (look for `texconv.exe` in the assets — pick a recent stable release). Put `texconv.exe` in a folder you can easily reach, e.g. the same folder as your source image.

### 12.4 Prepare and name your image

1. Make (or edit) a **1024 × 1024** PNG — the picture you want on the jacket.
2. Name the PNG **exactly** after a known-good in-game texture name, with a `.png` ending. **Do not retype the name — copy-paste it** from the list in Section 14 to avoid a typo. We'll use:

```
player_9_jacket_mural_fpp_dif.png
```

   The `.dds` you produce must be named after a texture the game actually loads. The safe way to get that right is to reuse one of the known-good names in Section 14, copied verbatim — that is the whole reason the list exists. (Engine texture names are lowercase, so if you ever discover a new name via the tool in Section 14, keep it lowercase.)

### 12.5 Convert PNG → DDS straight into the game folder

Open a Command Prompt in the folder that has both `texconv.exe` and your PNG (address-bar trick: click File Explorer's address bar, type `cmd`, Enter). Run this single command:

```
texconv -f R8G8B8A8_UNORM -m 1 -dx9 -w 1024 -h 1024 -y -o "C:\DLBB-Revival\mods\textures" player_9_jacket_mural_fpp_dif.png
```

What the parts do: `-f R8G8B8A8_UNORM` sets the exact 32-bit RGBA pixel format; `-m 1` means no mipmaps; `-dx9` asks for the legacy (non-DX10) 128-byte header; `-w 1024 -h 1024` forces the size; `-y` overwrites without asking; `-o` sets the output folder. `-dx9` produces the legacy header here **because `R8G8B8A8_UNORM` has a legacy DDS equivalent** — the flag avoids the extended DX10 header only for formats that have a legacy form, and this one does. Keep all three of `-f R8G8B8A8_UNORM -m 1 -dx9` together; that combination matches the exact 4,194,432-byte layout the game buffer expects, and the hook's DDS loader accepts that legacy header.

`texconv` names the output from the input, so you get:

```
C:\DLBB-Revival\mods\textures\player_9_jacket_mural_fpp_dif.dds
```

Expected console output: a line confirming it read your PNG, a line like `writing ...player_9_jacket_mural_fpp_dif.dds`, and no error. If texconv prints an error, fix that before launching.

### 12.6 (Optional) Verify the file with a double-click .bat

Rather than a fragile one-line command, save this as `check-dds.bat` in your textures folder and double-click it:

```bat
@echo off
setlocal
set "F=C:\DLBB-Revival\mods\textures\player_9_jacket_mural_fpp_dif.dds"
if not exist "%F%" ( echo NOT FOUND: %F% & pause & exit /b )
for %%A in ("%F%") do set SIZE=%%~zA
echo File:  %F%
echo Size:  %SIZE% bytes   (expected about 4194432)
if "%SIZE%"=="4194432" ( echo RESULT: exact match - good. ) else ( echo RESULT: not exactly 4194432. If it is close, it is usually still fine - the hook truncates to the game buffer. Try launching and check texture_hook.log. )
pause
```

A size of exactly `4194432` is ideal. If it's close but not exact, don't loop on re-running texconv — just launch and check the log (Section 13); the hook writes only what fits.

---

## 13. Launch and verify the texture

1. Make sure the server is running (Section 10), then start the game (Section 11).
2. Get into first-person view where the Agent character's jacket/arms are visible — the `player_9_jacket_..._fpp_dif` textures are the first-person arm/jacket skin. Your image should be painted on.
3. **Confirm the hook fired.** Open this log file in Notepad:

```
C:\DLBB-Revival\texture_hook.log
```

   Search (Ctrl+F) for your texture name first: `player_9_jacket_mural_fpp_dif`. You'll find a line like:

```
[REPLACE] #NN "player_9_jacket_mural_fpp_dif" 1024x1024 fmt=... origData=...
```

   Then, a few lines **below** that, look for:

```
[REPLACE] SUCCESS! Wrote 4194304 bytes
```

   The name and the `SUCCESS!` are on **separate** lines — the SUCCESS line reports the bytes written (the pixel payload, ~4,194,304), not the file size, and does not repeat the name. Finding your name followed shortly by a `SUCCESS!` line is proof the swap happened. If you see the hook loaded but no line mentioning your texture name, the game never loaded a texture by that name — re-check that your filename is copied verbatim from Section 14.

---

## 14. How to make more textures

The rule: **the on-disk filename, minus `.dds`, must exactly match the name the game uses for that texture.** Put the file in `C:\DLBB-Revival\mods\textures\` and the hook swaps it in at load time. You cannot see the game's internal names, so the reliable approach is to reuse a known-good name below — **copy-paste it, don't retype it.**

**The naming pattern** for the Agent's first-person jacket/arm skins is:

```
player_9_jacket_<variant>_fpp_dif.dds
```

- `player_9` = the Agent character.
- `jacket` = the body-part material slot.
- `<variant>` = the skin name (e.g. `mural`, `gold`, `tiger`, `velvet`, `crown`).
- `fpp` = first-person view. `dif` = the diffuse (color) map.

**Known working names you can drop in right now** (each a 1024×1024 R8G8B8A8 `.dds`):

```
player_9_jacket_biznesmen_fpp_dif.dds
player_9_jacket_casual_fpp_dif.dds
player_9_jacket_cow_fpp_dif.dds
player_9_jacket_crown_fpp_dif.dds
player_9_jacket_eye_fpp_dif.dds
player_9_jacket_flowers_fpp_dif.dds
player_9_jacket_fpp_legendary_dif.dds
player_9_jacket_gold_fpp_dif.dds
player_9_jacket_hand_fpp_dif.dds
player_9_jacket_jeans_fpp_dif.dds
player_9_jacket_leather_fpp_dif.dds
player_9_jacket_mask_a_fpp_dif.dds
player_9_jacket_mask_b_fpp_dif.dds
player_9_jacket_mural_fpp_dif.dds
player_9_jacket_panther_fpp_dif.dds
player_9_jacket_tiger_fpp_dif.dds
player_9_jacket_tulips_fpp_dif.dds
player_9_jacket_velvet_fpp_dif.dds
player_9_jacket_waiter_fpp_dif.dds
```

Pick any listed name, make a 1024×1024 PNG, run the `texconv` command from Section 12.5 (swapping in that name), and drop the result into `mods\textures`.

**Finding more texture names (optional / advanced — safe to skip).** The names above are the confirmed first-person Agent jacket skins. To discover other real texture names, use the project's `rpack_tool.py`. **You must substitute your own paths** in the placeholders below — pasting them verbatim will fail with "file not found":

```
python "<YOUR-PROJECT-FOLDER>\release\tools\rpack_tool.py" list "<YOUR-STEAM-DLBB-FOLDER>\DW\Data\common_cod_2_PC.rpack" player_9
```

Replace `<YOUR-PROJECT-FOLDER>` with the folder this project lives in, and `<YOUR-STEAM-DLBB-FOLDER>` with your Steam install of the game. Entries listed as texture type are candidate names (they're lowercase). Format your DDS to the exact spec in Section 12, name it `<thatname>.dds`, and drop it in `mods\textures`.

**Two honest limits:**
- This hook only re-colors textures the game **already loads**. It cannot add a brand-new skin that doesn't exist in the game.
- The third-person (menu) Agent gloves will **not** change color with this method — those black gloves are a separate mesh piece layered on top, which a texture swap can't touch. First-person jacket/arm skins are what this reliably changes.

**Tip:** before overwriting a working file, keep a backup with a `.bak` ending.

---

## 15. Troubleshooting

| Problem | Cause | Fix |
|--------|-------|-----|
| "Application load error V:0000065432" or "3:0000065432" | Game exe still packed (Steamless not applied), or Steam is open | Redo Section 6. The `BadBloodGame.exe` you launch must be the **unpacked** one (~975 KB). Also close Steam. |
| Game shows "Start the game using BadBloodGameLauncher.exe" and closes | The game patch wasn't applied | Run Section 7 against `C:\DLBB-Revival`. Confirm it ended with `DONE.` and no `ERROR`. |
| Game launches but **hangs on a "retrieving data" / loading screen** | **The local server isn't running** (this is the #1 cause) | Bring up the server: Section 10 (both windows), then confirm Section 10.3 returns the JSON reply, then launch. |
| `curl` check shows "Couldn't resolve host" | The patcher's hosts entry isn't present | Re-run Section 7 **as administrator**; it writes `127.0.0.1 pls.dlbb.com`. |
| `curl` check shows "Connection refused" | stunnel (port 443) not running | Redo Window 2 in Section 10.2, as administrator. |
| `python` not recognized | Python not on PATH | Reinstall Python and tick **"Add python.exe to PATH."** Verify with `python --version`. |
| Patcher says "bytes don't match" / refuses | Already patched, or wrong `--game-dir` | That's the safety check working. Ensure `--game-dir` is `C:\DLBB-Revival`. If already patched, you're fine. |
| Patcher prints `[skip] engine_x64_rwdi.dll not found` | Your game copy is missing that DLL | You copied only the exe. Redo Section 5 so the full game folder (including `engine_x64_rwdi.dll`) is at `C:\DLBB-Revival`. |
| Game runs but texture doesn't change | The `.dds` name doesn't match a texture the game loaded | Use a name from Section 14, **copied verbatim** (don't retype). Then check `texture_hook.log` for your name followed by a `SUCCESS!` line (Section 13). |
| Texture appears but looks scrambled / half-blank | Wrong size or format | Re-run the exact `texconv` command in Section 12.5. Check size with the `.bat` in 12.6 (~4,194,432 bytes, 1024×1024, legacy header). |
| Colors look swapped (red/blue) | Channel order wrong in your source | Make sure the PNG is normal RGBA and you used `-f R8G8B8A8_UNORM` exactly. |
| `LightFX`-related crash on launch | Wrong drop order | The original must be renamed to `LightFX_original.dll` **before** copying the project's `LightFX.dll` in (Section 9.1). |
| No `texture_hook.log` at all | `LightFX.dll` proxy not loading | Confirm both `LightFX.dll` and `texture_hook.dll` sit next to `BadBloodGame.exe`, and that you renamed the original to `LightFX_original.dll`. |

**Golden rule when something breaks:** the full setup in Sections 5–11 is the known-good baseline. If you experimented and it stopped working, put every file back and re-run the Section 7 patch.

---

## Appendix A — Build the DLLs yourself (optional, advanced)

You do **not** need this for the normal path — the project ships `LightFX.dll` and `texture_hook.dll` pre-built in `release\texture-hook`, and Section 9 just copies them. Only do this if you want to compile from source. It requires installing a compiler and editing environment variables, which is where most people get stuck — the pre-built DLLs avoid all of that.

### A.1 Install MinGW-w64

1. Open **https://winlibs.com/**, scroll to **"Release versions,"** find the newest **"GCC ... + MinGW-w64 (UCRT) - release,"** pick the **64-bit** build, and download the **7-Zip archive** (`.7z`). If you can't open `.7z`, install **7-Zip** from **https://www.7-zip.org/** first.
2. Extract it. Move the inner `mingw64` folder to the root of C: so this file exists exactly:

```
C:\mingw64\bin\g++.exe
```

   If you have `C:\mingw64\mingw64\bin`, you extracted one level too deep — move the inner folder up.

3. Add it to PATH: **Windows key + R** → `sysdm.cpl` → **Advanced** tab → **Environment Variables...** → under **User variables** select **Path** → **Edit...** → **New** → type `C:\mingw64\bin` → **OK** on every window.
4. Open a **new** Command Prompt and run `g++ --version`. You should see version text. If you see `'g++' is not recognized`, recheck the PATH entry and open a fresh window.

### A.2 (Only if you build) Check the hook's server address before building

The network redirect target is set in source. Before building, open `release\texture-hook\texture_hook.cpp` and confirm the `VPS_IP` define reads:

```c
#define VPS_IP        "127.0.0.1"          // 127.0.0.1 = local play.
```

For local play this must be `127.0.0.1`. If a copy you're building has anything else (the project README documents `"0.0.0.0"` as a placeholder example), change it to `127.0.0.1`. The same value is duplicated as four ints inside `Hooked_connect()` — search for `vpsIp =` and make sure it reads:

```c
unsigned long vpsIp = (127UL << 24) | (0UL << 16) | (0UL << 8) | 1UL;
```

Keep both in sync, or the hook will redirect traffic to the wrong place with no error.

### A.3 Build

Open a Command Prompt inside `release\texture-hook` (address-bar trick), then:

```
build.bat
```

The shipped `build.bat` builds **both** `texture_hook.dll` and `LightFX.dll`. When it finishes, confirm both `.dll` files exist in that folder. If `LightFX.dll` is missing, run these two commands manually:

```
g++ -shared -O2 -o texture_hook.dll texture_hook.cpp -luser32 -lpsapi -lws2_32 -lcrypt32 -static -std=c++17
```

```
g++ -shared -O2 -o LightFX.dll lightfx_proxy.cpp -static -std=c++17
```

Success looks like each command returning to the prompt with no errors and the two `.dll` files present. Then use them exactly as in Section 9. (Do not run any whole-project build script here — that builds the launcher/server/stubs and needs extra tooling; those are handled separately in this guide.)
