# Engine patches — reference

Detailed breakdown of every byte modified by `patcher/apply_patches.py` on
`engine_x64_rwdi.dll`. These are based on the build of the engine shipped
with the final Steam release of `Dying Light: Bad Blood`. A different
build would have different offsets.

---

## 0x6F73DE — EAC launcher gate bypass

**Length:** 6 bytes
**Before:** `0F 85 BE 00 00 00`  (`JNZ rel32 → +0xBE → 0x006F74A2`)
**After:**  `E9 01 01 00 00 90`  (`JMP rel32 → +0x101 → 0x006F74E4` + `NOP`)

### Why
The engine has a function that:
1. `LoadLibrary("EasyAntiCheat_x64.dll")` at `0x006F73A0`
2. `GetProcAddress` for a factory function at `0x006F73B8`
3. Calls the factory at `0x006F73CD`
4. Tests the result at `0x006F73DE` with the `JNZ` we patch

If the factory returned a non-NULL pointer (= EAC initialized OK), the
jump fires and execution proceeds normally. If NULL (= our stub returned
NULL, or EAC failed), execution falls through to a `MessageBox` that says
"Start the game using BadBloodGameLauncher.exe", followed by `ExitProcess`.

Our EAC stub *does* return non-NULL from `CreateGameClient` (and the other
factories), so in theory the original jump would work. But making the
test unconditional removes any dependency on the stub's correctness.

The new `JMP +0x101` lands at `0x006F74E4`, which is the clean-return
prologue of the function (no MessageBox, no ExitProcess).

### How it was found
Searched for the ASCII string "Start the game using BadBloodGameLauncher.exe"
in the DLL (file offset 0x008BB0E8 / RVA 0x008BC4E8), then traced LEA-RIP+
references from `.text` to find the code that loads its pointer (= the
error path). Backed up from there to find the conditional jump.

See `tools/patch-launcher-check.py` for a standalone implementation of
just this patch — useful as a minimal example of the PE-RVA-to-file-offset
dance.

---

## 0x6F95AA — CURLOPT_SSL_VERIFYPEER = 0

**Length:** 4 bytes
**Before:** instruction was `SETNZ R8B` (4 bytes) — sets R8B to 1 if the
   "verify peer" config bool is enabled.
**After:**  `45 33 C0 90`  (`XOR R8D, R8D` + `NOP`) — sets R8 to 0
   regardless of config.

### Why
libcurl's `curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0)` disables
certificate verification. Without this we'd need a real CA-signed cert
for the patched hostname, and the engine's cert store doesn't include
common public CAs.

Setting the third argument (R8) to 0 forces `VERIFYPEER = off`.

### How it was found
By searching the .rdata for the libcurl options enum integer constants:
`CURLOPT_SSL_VERIFYPEER = 0x40 = 64`, `CURLOPT_SSL_VERIFYHOST = 0x51 = 81`.
These constants are pushed into ECX/EDX as the 2nd `curl_easy_setopt` arg
right before the `SETNZ R8B` we patch. So once you find the `0x40`
constant being moved into the right register near a `curl_easy_setopt`
call site, the `SETNZ` two instructions before that move is your target.

---

## 0x6F95C3 — CURLOPT_SSL_VERIFYHOST = 0

Same instruction swap as VERIFYPEER, 25 bytes later. Same rationale.

---

## 0x7152C6 — Force HTTP transport gate open

**Length:** 12 bytes
**After:** `48 C7 83 80 00 00 00  01 00 00 00  90`
   (`MOV qword ptr [RBX + 0x80], 1` + `NOP`)

### Why
At `0x7152C6` the engine reads a flag at `[transport_obj + 0x80]` to decide
whether the HTTP response is "ready" to be delivered up the stack. In our
emulated path this flag never gets set to 1 — the auth response is
received and parsed, but the gate stays at 0 and the response is dropped.

Forcing the move-immediate-to-memory instruction (which writes the
"transport ready" state) **before** the check, instead of after a
condition we couldn't satisfy, lets the response through.

This was the longest patch to find. Triton symbolic execution against
the auth flow eventually identified `[+0x80]` as the gate field. See
[`docs/recon-findings.md`](recon-findings.md) for the dump.

---

## 0x5B7C72 — rpacz filename whitelist NOP

**Length:** approximate (5 bytes of NOPs)
**Before:** unknown — but it's a `JE`/`JNE` after a `cmp` against a
   whitelist of allowed rpacz filenames.
**After:**  `90 90 90 90 90`

### Why
The engine accepts `.rpacz` patches (a variant of `.rpack` with prefix
deltas) but only for filenames it recognizes from a hardcoded list. To
ship custom addon skins we need it to accept any `.rpacz` name. NOPing
the conditional jump (or the `cmp` itself) makes the whitelist check
unconditionally pass.

The exact pre-patch bytes aren't documented because this was the last
patch added and we worked backwards from a known-good post-patch DLL.
Future work: derive `before` bytes for safer verification (see the
`expected_before: null` entries in `patcher/patches.json`).

---

## String patch — `pls.dlbb.com` → your hostname

**Operation:** in-place ASCII string replace, exactly 12 bytes,
   every occurrence.

### Why
The Techland PLS hostname is compiled into multiple places in the engine
(URL templates, header builders, log strings). Replacing it in-place with
a same-length string lets the game make HTTPS requests to your server
without needing any code-level intercept.

The 12-character constraint is what limits replacement hostnames.

The patcher counts occurrences, requires at least 1, and writes them all.
Common DLBB engine builds have 3-5 occurrences.

---

## What's NOT patched in this recipe

- **`BadBloodGame.exe`** (beyond Steamless unpacking, which the user does
  manually) — the auth bypass at the exe level mentioned in some older
  notes was redundant once the engine SSL patches + HTTP gate patch were
  in. The exe still gets its `pls.dlbb.com` string replaced if any are
  found, but no byte patches.

- **`gamedll_x64_rwdi.dll`** — gameplay DLL, no edits. The min-players=6
  hardcode that prevents starting small matches lives here, but we haven't
  found and patched it yet.

- **`rd3d11_x64_rwdi.dll`** — D3D11 wrapper. Not patched; intercepted at
  runtime by `texture_hook.dll` via API hook on `RCreateTexture2D`
  (function at RVA 0x48840).

---

## Reproducibility

`patcher/patches.json` carries the offsets and bytes; the script in
`patcher/apply_patches.py` parses them. If a future game update shifts
offsets, the script will refuse to patch (mismatched expected_before),
preventing breakage.

To reverse-engineer new offsets, the workflow is:

1. Diff `engine_x64_rwdi.dll.original` vs. a known-good patched copy
   to identify changed bytes. Tool: `cmp -l` (Linux) / `fc /b` (Windows)
   pipelined into a grouper.
2. For each changed range, disassemble around it (IDA, Ghidra, or
   `objdump -d --start-address=0xXXXXX --stop-address=0xYYYYY`).
3. Decide whether the change can be reproduced with a smaller patch (NOP
   the conditional? swap a constant?) and update `patches.json`.

The historical workflow used Triton for symbolic execution along the
auth flow; see `tools/triton_path_to_token.py` (in the source repo
workspace, not shipped here because it depended on game files).
