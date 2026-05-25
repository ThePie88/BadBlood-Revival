#!/usr/bin/env python3
"""
BadBlood-Revival — Game Client Patcher

Applies byte-level patches to a legitimately-owned, vanilla Dying Light: Bad Blood
installation to redirect the game's PLS calls to a self-hosted server emulator.

USAGE
-----
  python apply_patches.py --game-dir "C:/Steam/steamapps/common/Dying Light Bad Blood" --server-host pls.example.it

REQUIREMENTS
------------
- A legitimate, vanilla install of Dying Light: Bad Blood (purchased on Steam before delisting).
- BadBloodGame.exe must be Steamless-unpacked first (see docs/setup-game.md).
- New server hostname MUST be exactly 12 ASCII characters (same length as 'pls.dlbb.com').

WHAT THIS DOES
--------------
- Backs up engine_x64_rwdi.dll and BadBloodGame.exe to .original (only if no backup exists).
- Applies byte patches from patches.json (EAC bypass, SSL verify off, HTTP gate, rpacz whitelist).
- Replaces 'pls.dlbb.com' → <server-host> in the patched files.
- Idempotent: running twice does nothing the second time.

This script does NOT redistribute any Techland binary. It only modifies your own files.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path
from typing import Optional

SCRIPT_DIR = Path(__file__).parent.resolve()
PATCHES_FILE = SCRIPT_DIR / "patches.json"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

class PatchError(Exception):
    """A patch could not be applied — exit cleanly with a useful message."""


def parse_hex_bytes(s: Optional[str]) -> Optional[bytes]:
    """Parses a hex string like '0F 85 BE 00 00 00' (whitespace tolerant) into bytes."""
    if s is None:
        return None
    return bytes.fromhex(s.replace(" ", "").replace("\t", "").replace("\n", ""))


def parse_offset(s: str) -> int:
    """Parses an offset string like '0x6F73DE' or '7242718'."""
    s = s.strip()
    return int(s, 16) if s.lower().startswith("0x") else int(s)


def fmt_hex(b: bytes, sep: str = " ") -> str:
    return sep.join(f"{x:02X}" for x in b)


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def backup_if_needed(target: Path) -> Path:
    """Creates target.original next to target if it doesn't exist. Returns backup path."""
    backup = target.with_suffix(target.suffix + ".original")
    if not backup.exists():
        shutil.copy2(target, backup)
        print(f"  [backup] {backup.name} created ({backup.stat().st_size} bytes)")
    else:
        print(f"  [backup] {backup.name} already exists, kept as-is")
    return backup


# ---------------------------------------------------------------------------
# Patch logic
# ---------------------------------------------------------------------------

def apply_byte_patch(data: bytearray, patch: dict) -> str:
    """Applies a single byte patch in place. Returns a status string: 'applied' / 'skipped' / 'error'."""
    pid = patch["id"]
    offset = parse_offset(patch["offset"])
    expected_before = parse_hex_bytes(patch.get("before"))
    expected_after = parse_hex_bytes(patch.get("after"))

    if expected_after is None:
        raise PatchError(f"[{pid}] missing 'after' bytes in patches.json")

    length = len(expected_after)
    current = bytes(data[offset:offset + length])

    if expected_before is not None and current == expected_before:
        data[offset:offset + length] = expected_after
        return f"applied (was: {fmt_hex(current)} -> now: {fmt_hex(expected_after)})"

    if current == expected_after:
        return "skipped (already patched)"

    if expected_before is None:
        # Trust mode: we don't know the vanilla bytes. Warn and apply.
        data[offset:offset + length] = expected_after
        return f"applied (trust mode — original bytes were: {fmt_hex(current)})"

    raise PatchError(
        f"[{pid}] bytes at offset 0x{offset:X} don't match either vanilla or patched.\n"
        f"        expected vanilla: {fmt_hex(expected_before)}\n"
        f"        expected patched: {fmt_hex(expected_after)}\n"
        f"        actual:           {fmt_hex(current)}\n"
        f"        This file may be a different game version, already differently patched, "
        f"or corrupted. Aborting to avoid breaking it further."
    )


def apply_string_patch(data: bytearray, patch: dict, server_host: str) -> str:
    """Replaces every occurrence of 'find' with replacement (same byte length)."""
    pid = patch["id"]
    encoding = patch.get("encoding", "ascii")
    find = patch["find"].encode(encoding)
    max_length = patch.get("max_length", len(find))
    replacement_token = patch.get("replace_token", "")

    # Substitute {server_host} -> actual host
    replacement_str = replacement_token.replace("{server_host}", server_host)
    replacement = replacement_str.encode(encoding)

    # Local setup: replacement == original means no replacement needed.
    # The hosts-file redirect handles 'pls.dlbb.com' -> 127.0.0.1 instead.
    if replacement == find:
        count_before = data.count(find)
        return (f"skipped (local setup: hostname kept as '{patch['find']}', "
                f"{count_before} occurrence(s) intact)")

    if len(replacement) != len(find):
        raise PatchError(
            f"[{pid}] replacement '{replacement_str}' is {len(replacement)} bytes, "
            f"must be exactly {len(find)} bytes (same as '{patch['find']}'). "
            f"Pick a hostname with exactly {max_length} ASCII characters."
        )

    occ_min = patch.get("occurrences_min", 1)
    occ_max = patch.get("occurrences_max", 9999)

    count_before = data.count(find)
    count_after_already = data.count(replacement)

    if count_before == 0:
        if count_after_already > 0:
            return f"skipped ({count_after_already} occurrences already replaced)"
        if occ_min == 0:
            return "skipped (no occurrences found, but patch is optional)"
        raise PatchError(f"[{pid}] no occurrences of '{patch['find']}' found in file")

    if count_before < occ_min:
        raise PatchError(
            f"[{pid}] found {count_before} occurrences, expected at least {occ_min}"
        )
    if count_before > occ_max:
        raise PatchError(
            f"[{pid}] found {count_before} occurrences, expected at most {occ_max}"
        )

    # Do the replace
    new_data = bytes(data).replace(find, replacement)
    data[:] = new_data
    return f"applied ({count_before} occurrence(s) of '{patch['find']}' -> '{replacement_str}')"


def patch_file(file_path: Path, file_spec: dict, server_host: str, dry_run: bool) -> bool:
    """Patches a single file in place. Returns True on success."""
    if not file_path.exists():
        print(f"  [skip] {file_path.name} not found")
        return True

    print(f"\n>>> Patching {file_path.name}")
    print(f"    size before: {file_path.stat().st_size} bytes")
    print(f"    sha256 before: {sha256_of(file_path)}")

    prereq = file_spec.get("prerequisite")
    if prereq:
        print(f"    [!] prerequisite: {prereq}")

    if not dry_run:
        backup_if_needed(file_path)

    with file_path.open("rb") as f:
        data = bytearray(f.read())

    # Byte patches
    for patch in file_spec.get("byte_patches", []):
        try:
            status = apply_byte_patch(data, patch)
            print(f"    [byte ] {patch['id']:32s}  {status}")
        except PatchError as e:
            print(f"    [ERROR] {patch['id']}: {e}")
            return False

    # String patches
    for patch in file_spec.get("string_patches", []):
        try:
            status = apply_string_patch(data, patch, server_host)
            print(f"    [strng] {patch['id']:32s}  {status}")
        except PatchError as e:
            print(f"    [ERROR] {patch['id']}: {e}")
            return False

    if dry_run:
        print(f"    [dry-run] would have written {file_path.name} ({len(data)} bytes)")
        return True

    with file_path.open("wb") as f:
        f.write(data)
    print(f"    sha256 after:  {sha256_of(file_path)}")
    return True


# ---------------------------------------------------------------------------
# Hosts file
# ---------------------------------------------------------------------------

def update_hosts(entry_block: dict, dry_run: bool) -> None:
    """Adds the pls.dlbb.com -> 127.0.0.1 entry to the system hosts file (if not already present).

    Windows: C:\\Windows\\System32\\drivers\\etc\\hosts (requires admin)
    Linux:   /etc/hosts (requires sudo)
    """
    if sys.platform == "win32":
        hosts = Path(r"C:\Windows\System32\drivers\etc\hosts")
    else:
        hosts = Path("/etc/hosts")

    if not hosts.exists():
        print(f"  [skip] hosts file at {hosts} not found")
        return

    entry = entry_block["entry"]
    marker = entry_block["marker"]

    try:
        content = hosts.read_text(encoding="utf-8", errors="replace")
    except PermissionError:
        print(f"  [skip] hosts file {hosts} not readable (run as admin/sudo to update)")
        return

    if marker in content:
        print(f"  [hosts] entry already present (marker '{marker}' found)")
        return

    new_content = content.rstrip() + "\n" + entry + "\n"

    if dry_run:
        print(f"  [dry-run] would append to {hosts}: {entry}")
        return

    try:
        hosts.write_text(new_content, encoding="utf-8")
        print(f"  [hosts] appended: {entry}")
    except PermissionError:
        print(f"  [!!] could not write to {hosts}. Run as admin/sudo or add manually:")
        print(f"       {entry}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(
        description="Patch a vanilla Dying Light: Bad Blood install to talk to a BadBlood-Revival server.",
        epilog=(
            "Examples:\n"
            "  Local play (server on the same PC or a friend's LAN):\n"
            "    python apply_patches.py --game-dir 'C:/Games/DLBB' --local\n"
            "  Public server with your own 12-char domain:\n"
            "    python apply_patches.py --game-dir 'C:/Games/DLBB' --server-host pls.example.it\n"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "--game-dir", required=True, type=Path,
        help="Path to your DLBB install folder (contains BadBloodGame.exe, engine_x64_rwdi.dll, etc.)",
    )
    ap.add_argument(
        "--server-host", default=None,
        help=("Your server hostname. MUST be exactly 12 ASCII characters "
              "(e.g. pls.example.it). Omit (or use --local) for a local-only "
              "setup that keeps 'pls.dlbb.com' in the binaries and relies on "
              "the hosts file to route it to 127.0.0.1."),
    )
    ap.add_argument(
        "--local", action="store_true",
        help=("Local-play preset. Same as not passing --server-host: the "
              "hostname stays 'pls.dlbb.com' inside the binaries and the "
              "hosts file routes it to 127.0.0.1 where stunnel listens. "
              "Perfect for single-player or LAN with friends."),
    )
    ap.add_argument(
        "--patches", type=Path, default=PATCHES_FILE,
        help=f"Path to patches.json (default: {PATCHES_FILE})",
    )
    ap.add_argument(
        "--skip-hosts", action="store_true",
        help="Don't touch the system hosts file (useful in CI or if you manage DNS differently).",
    )
    ap.add_argument(
        "--dry-run", action="store_true",
        help="Show what would be done without writing anything.",
    )
    args = ap.parse_args()

    # Resolve --server-host. Default = original Techland hostname,
    # which means "no string replacement, just trust the hosts file."
    if args.server_host is None or args.local:
        args.server_host = "pls.dlbb.com"
        mode = "LOCAL (hostname kept as 'pls.dlbb.com', routed via hosts file)"
    else:
        mode = f"PUBLIC ('{args.server_host}')"

    # Validate server host length
    if len(args.server_host) != 12:
        print(f"ERROR: --server-host '{args.server_host}' is {len(args.server_host)} characters.")
        print(f"       It must be exactly 12 ASCII chars (same length as 'pls.dlbb.com')")
        print(f"       to avoid breaking binary alignment in the patched files.")
        return 2
    if not all(0x20 <= ord(c) < 0x7F for c in args.server_host):
        print(f"ERROR: --server-host '{args.server_host}' contains non-ASCII characters.")
        return 2

    game_dir: Path = args.game_dir.resolve()
    if not game_dir.is_dir():
        print(f"ERROR: --game-dir '{game_dir}' does not exist or is not a directory.")
        return 2

    if not args.patches.exists():
        print(f"ERROR: patches.json not found at {args.patches}")
        return 2

    print("=" * 70)
    print("BadBlood-Revival — Game Client Patcher")
    print("=" * 70)
    print(f"  game-dir:    {game_dir}")
    print(f"  mode:        {mode}")
    print(f"  server-host: {args.server_host}")
    print(f"  patches:     {args.patches}")
    if args.dry_run:
        print(f"  dry-run:     YES — nothing will be modified")

    with args.patches.open(encoding="utf-8") as f:
        recipe = json.load(f)

    # Patch each file
    all_ok = True
    for filename, spec in recipe["files"].items():
        file_path = game_dir / filename
        if not patch_file(file_path, spec, args.server_host, args.dry_run):
            all_ok = False

    if not all_ok:
        print("\n>>> Some patches failed. The game files have been left in a partially-modified state.")
        print(">>> Restore from the .original backups created in the same directory.")
        return 1

    # Hosts file. Only useful in LOCAL mode (route pls.dlbb.com to 127.0.0.1).
    # In PUBLIC mode, the hostname has been replaced inside the binary, so the
    # entry would point a nonexistent hostname at localhost — skip it.
    is_local = args.server_host == "pls.dlbb.com"
    if args.skip_hosts:
        print("\n>>> Skipping hosts file (--skip-hosts)")
        if is_local:
            print(f"    For local play you'll need to add this entry manually:")
            print(f"      {recipe['hosts_entry']['entry']}")
    elif is_local:
        print("\n>>> Updating system hosts file (local mode)")
        update_hosts(recipe["hosts_entry"], args.dry_run)
    else:
        print("\n>>> Skipping hosts file (public mode — set up DNS instead)")
        print(f"    Point your DNS for '{args.server_host}' at your server's public IP.")

    print("\n" + "=" * 70)
    print("DONE. Next steps:")
    if is_local:
        print("  1. Make sure the BadBlood-Revival server is running on this PC")
        print("     (release/server/setup-local.bat once, then run.bat + stunnel)")
        print("  2. Build and drop in the stub DLLs (BattlEye, EAC)")
        print("     — from release/stubs/  →  game folder")
        print("  3. Build and drop in texture_hook.dll + LightFX proxy")
        print("     — from release/texture-hook/  →  game folder")
        print("  4. Add Goldberg Steam Emu files to <game-dir>/")
        print("     (steam_api64.dll, steamclient64.dll, ...)")
        print("  5. Build the launcher (release/launcher/build.bat) and run it")
    else:
        print("  1. Ensure your server at " + args.server_host + " is running with stunnel")
        print("  2. Build + distribute stub DLLs to players")
        print("  3. Build + distribute texture_hook.dll + LightFX proxy")
        print("  4. Build + distribute the launcher (with SERVER_HOST/VPS_IP set to match)")
        print("  5. Each player drops the files into their patched game folder")
    print("\n  Full guide: see docs/setup-local.md (or docs/setup-game.md)")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
