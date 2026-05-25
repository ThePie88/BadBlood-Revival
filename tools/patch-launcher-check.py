"""
patch-launcher-check.py — Standalone EAC launcher-check bypass for engine_x64_rwdi.dll

The full patcher is patcher/apply_patches.py (data-driven, multi-patch, idempotent).
This script does ONE patch only — the EAC launcher gate at 0x6F73DE — but it
finds the patch site dynamically by string reference instead of relying on a
hardcoded offset. Useful as a minimal example of PE-aware patching, and as a
fallback if a future game build shifts offsets and `apply_patches.py` rejects
the file.

Usage:
    python patch-launcher-check.py <path-to-engine_x64_rwdi.dll>
"""
import struct
import sys
import shutil
import os

if len(sys.argv) < 2:
    print("Usage: python patch-launcher-check.py <path-to-engine_x64_rwdi.dll>")
    sys.exit(2)

ENGINE_PATH = sys.argv[1]
BACKUP_PATH = ENGINE_PATH + ".bak"

if not os.path.exists(ENGINE_PATH):
    print(f"ERROR: {ENGINE_PATH} not found")
    sys.exit(1)

# Read the DLL
with open(ENGINE_PATH, "rb") as f:
    data = bytearray(f.read())

# Find the string
needle = b"Start the game using BadBloodGameLauncher.exe"
str_offset = data.find(needle)
if str_offset < 0:
    print("ERROR: Launcher check string not found!")
    sys.exit(1)

print(f"String found at file offset: 0x{str_offset:08X}")

# Parse PE to find the RVA of this string
# Read PE header
pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
num_sections = struct.unpack_from("<H", data, pe_offset + 6)[0]
opt_header_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
image_base = struct.unpack_from("<Q", data, pe_offset + 24 + 24)[0]
section_start = pe_offset + 24 + opt_header_size

sections = []
for i in range(num_sections):
    s_off = section_start + i * 40
    name = data[s_off:s_off+8].rstrip(b'\x00').decode('ascii', errors='replace')
    vsize = struct.unpack_from("<I", data, s_off + 8)[0]
    vaddr = struct.unpack_from("<I", data, s_off + 12)[0]
    raw_size = struct.unpack_from("<I", data, s_off + 16)[0]
    raw_addr = struct.unpack_from("<I", data, s_off + 20)[0]
    sections.append({
        'name': name, 'vaddr': vaddr, 'vsize': vsize,
        'raw_addr': raw_addr, 'raw_size': raw_size
    })

def file_to_rva(offset):
    for s in sections:
        if s['raw_addr'] <= offset < s['raw_addr'] + s['raw_size']:
            return offset - s['raw_addr'] + s['vaddr']
    return None

def rva_to_file(rva):
    for s in sections:
        if s['vaddr'] <= rva < s['vaddr'] + s['vsize']:
            return rva - s['vaddr'] + s['raw_addr']
    return None

str_rva = file_to_rva(str_offset)
print(f"String RVA: 0x{str_rva:08X}")
print(f"Image base: 0x{image_base:016X}")

# Find code that references this string using LEA instruction
# In x64, LEA uses RIP-relative addressing: 48 8D xx [4-byte displacement]
# The displacement is relative to the instruction AFTER the LEA
# Search for references in .text section
text_section = None
for s in sections:
    if s['name'] == '.text':
        text_section = s
        break

if not text_section:
    print("ERROR: .text section not found")
    sys.exit(1)

print(f"\nSearching .text section (0x{text_section['raw_addr']:X} - 0x{text_section['raw_addr']+text_section['raw_size']:X})...")

# Search for LEA reg, [rip+disp] that points to our string
# LEA patterns: 48 8D 0D/15/05/35/3D [disp32] or 4C 8D [disp32]
refs = []
text_start = text_section['raw_addr']
text_end = text_start + text_section['raw_size']

for i in range(text_start, text_end - 7):
    # Check for LEA with RIP-relative addressing
    # Common patterns: 48 8D [05/0D/15/1D/25/2D/35/3D] or 4C 8D [05/0D/15/1D/25/2D/35/3D]
    if data[i] in (0x48, 0x4C) and data[i+1] == 0x8D and (data[i+2] & 0xC7) == 0x05:
        disp = struct.unpack_from("<i", data, i + 3)[0]
        # RIP-relative: target = RVA_of_next_instruction + disp
        instr_rva = file_to_rva(i)
        if instr_rva is None:
            continue
        next_instr_rva = instr_rva + 7
        target_rva = next_instr_rva + disp
        if target_rva == str_rva:
            refs.append(i)
            print(f"  Found reference at file offset 0x{i:08X} (RVA 0x{instr_rva:08X})")

if not refs:
    print("No direct LEA references found, trying broader search...")
    # Try looking for the RVA as a 4-byte value anywhere in code
    rva_bytes = struct.pack("<I", str_rva)
    idx = text_start
    while idx < text_end:
        idx = data.find(rva_bytes, idx, text_end)
        if idx < 0:
            break
        print(f"  Found RVA reference at 0x{idx:08X}")
        refs.append(idx)
        idx += 1

if not refs:
    print("ERROR: Could not find code referencing the launcher check string!")
    sys.exit(1)

# For each reference, look backwards for a conditional jump we can patch
ref = refs[0]
print(f"\nAnalyzing code around reference at 0x{ref:08X}...")
print(f"Context (bytes around reference, -64 to +32):")

# Show disassembly context
start = max(ref - 80, text_start)
end = min(ref + 48, text_end)
context = data[start:end]

print(f"Hex dump from 0x{start:08X}:")
for row_start in range(0, len(context), 16):
    addr = start + row_start
    hex_str = " ".join(f"{context[row_start+j]:02X}" for j in range(min(16, len(context) - row_start)))
    print(f"  0x{addr:08X}: {hex_str}")

# Look for conditional jumps (Jcc) before the reference
# Short Jcc: 0x70-0x7F [1-byte disp]
# Near Jcc: 0x0F 0x80-0x8F [4-byte disp]
print(f"\nLooking for conditional jumps before 0x{ref:08X}...")
candidates = []
for i in range(ref - 80, ref):
    if i < text_start:
        continue
    # Short Jcc
    if 0x70 <= data[i] <= 0x7F:
        disp = struct.unpack_from("<b", data, i + 1)[0]
        target = i + 2 + disp
        target_rva_val = file_to_rva(i)
        print(f"  Short Jcc at 0x{i:08X} (opcode 0x{data[i]:02X}), disp={disp}, target=0x{target:08X}")
        candidates.append(('short', i, data[i]))
    # Near Jcc
    if data[i] == 0x0F and 0x80 <= data[i+1] <= 0x8F:
        disp = struct.unpack_from("<i", data, i + 2)[0]
        target = i + 6 + disp
        print(f"  Near Jcc at 0x{i:08X} (opcode 0x0F 0x{data[i+1]:02X}), disp={disp}, target=0x{target:08X}")
        candidates.append(('near', i, data[i+1]))

if candidates:
    # Patch the last (closest) conditional jump before the string reference
    jtype, jaddr, jopcode = candidates[-1]
    print(f"\n=== PATCHING: {jtype} Jcc at 0x{jaddr:08X} ===")

    # Backup first
    if not os.path.exists(BACKUP_PATH):
        shutil.copy2(ENGINE_PATH, BACKUP_PATH)
        print(f"Backup saved to: {BACKUP_PATH}")

    if jtype == 'short':
        # Convert conditional jump to unconditional: JMP short (0xEB)
        original = data[jaddr]
        data[jaddr] = 0xEB
        print(f"Patched 0x{original:02X} -> 0xEB (JMP short) at 0x{jaddr:08X}")
    else:
        # Convert near Jcc (0F 8x) to JMP near (E9) + NOP
        original = data[jaddr+1]
        disp = struct.unpack_from("<i", data, jaddr + 2)[0]
        # Near Jcc is 6 bytes, JMP near is 5 bytes, so adjust displacement +1
        new_disp = disp + 1
        data[jaddr] = 0xE9
        struct.pack_into("<i", data, jaddr + 1, new_disp)
        data[jaddr + 5] = 0x90  # NOP
        print(f"Patched 0F {original:02X} -> E9 (JMP near) + NOP at 0x{jaddr:08X}")

    with open(ENGINE_PATH, "wb") as f:
        f.write(data)
    print("DLL saved successfully!")
else:
    print("No conditional jumps found near the reference. Manual analysis needed.")
    print("Saving hex dump for review...")
