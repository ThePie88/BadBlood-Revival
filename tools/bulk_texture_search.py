#!/usr/bin/env python3
"""
Bulk texture search: find ALL Crigrey DDS textures in rpack files.
Single-pass scan through each rpack for maximum efficiency.
"""
import sys
import os
import struct
import time

def get_dds_header_size(data):
    """Determine DDS header size (128 for standard, 148 for DX10)."""
    if len(data) < 128:
        return 0
    magic = data[:4]
    if magic != b'DDS ':
        return 0
    # Check for DX10 extended header
    # dwFlags at offset 8, pixel format at offset 76
    pf_flags = struct.unpack_from('<I', data, 80)[0]
    pf_fourcc = data[84:88]
    if pf_fourcc == b'DX10':
        return 148  # 128 + 20 byte DX10 header
    return 128

def load_search_patterns(dds_dir, pattern_size=512):
    """Load search patterns from all DDS files in directory."""
    patterns = {}
    for fname in sorted(os.listdir(dds_dir)):
        if not fname.lower().endswith('.dds'):
            continue
        fpath = os.path.join(dds_dir, fname)
        with open(fpath, 'rb') as f:
            data = f.read()

        hdr_size = get_dds_header_size(data)
        if hdr_size == 0:
            print(f"  SKIP {fname}: not a valid DDS")
            continue

        raw_data = data[hdr_size:]
        raw_size = len(raw_data)
        pattern = raw_data[:pattern_size]

        # Also get format info from header
        if hdr_size == 148:
            dxgi_format = struct.unpack_from('<I', data, 128)[0]
            fmt = f"DX10/DXGI={dxgi_format}"
        else:
            pf_fourcc = data[84:88]
            if pf_fourcc == b'DXT1':
                fmt = "DXT1"
            elif pf_fourcc == b'DXT5':
                fmt = "DXT5"
            else:
                pf_flags = struct.unpack_from('<I', data, 80)[0]
                fmt = f"RAW(flags={pf_flags:#x})"

        width = struct.unpack_from('<I', data, 16)[0]
        height = struct.unpack_from('<I', data, 12)[0]

        name = fname.replace('.dds', '')
        patterns[name] = {
            'pattern': pattern,
            'raw_size': raw_size,
            'format': fmt,
            'width': width,
            'height': height,
            'full_raw': raw_data,  # keep full data for verification
        }
        print(f"  Loaded {name}: {width}x{height} {fmt} ({raw_size} bytes)")

    return patterns

def search_rpack(rpack_path, patterns, verify_size=64):
    """
    Single-pass search through rpack for all patterns.
    Uses first `verify_size` bytes of each pattern for initial match,
    then verifies with full pattern.
    """
    print(f"\nSearching {rpack_path} ({os.path.getsize(rpack_path)/1e9:.1f} GB)...")

    # Build lookup from first N bytes
    initial_size = min(32, min(len(p['pattern']) for p in patterns.values()))
    lookup = {}  # first N bytes -> list of pattern names
    for name, info in patterns.items():
        key = info['pattern'][:initial_size]
        if key not in lookup:
            lookup[key] = []
        lookup[key].append(name)

    results = {}
    chunk_size = 64 * 1024 * 1024  # 64MB chunks
    overlap = 1024  # overlap between chunks to catch patterns at boundaries

    file_size = os.path.getsize(rpack_path)
    found_names = set()

    t0 = time.time()
    with open(rpack_path, 'rb') as f:
        offset = 0
        prev_tail = b''

        while offset < file_size:
            chunk = f.read(chunk_size)
            if not chunk:
                break

            # Search in this chunk
            search_data = prev_tail + chunk
            base_offset = offset - len(prev_tail)

            for pos in range(len(search_data) - initial_size):
                key = search_data[pos:pos + initial_size]
                if key in lookup:
                    abs_pos = base_offset + pos
                    for name in lookup[key]:
                        if name in found_names:
                            continue
                        pattern = patterns[name]['pattern']
                        # Verify full pattern
                        if search_data[pos:pos + len(pattern)] == pattern:
                            results[name] = {
                                'offset': abs_pos,
                                'raw_size': patterns[name]['raw_size'],
                                'format': patterns[name]['format'],
                                'width': patterns[name]['width'],
                                'height': patterns[name]['height'],
                            }
                            found_names.add(name)
                            elapsed = time.time() - t0
                            print(f"  FOUND {name} at offset {abs_pos} ({abs_pos:#x}) [{elapsed:.0f}s]")

            prev_tail = chunk[-overlap:] if len(chunk) > overlap else chunk
            offset += len(chunk)

            # Progress
            pct = offset / file_size * 100
            if offset % (256 * 1024 * 1024) < chunk_size:
                elapsed = time.time() - t0
                print(f"  ... {pct:.0f}% ({offset/1e9:.1f}/{file_size/1e9:.1f} GB) [{elapsed:.0f}s]")

            # Early exit if all found
            if len(found_names) == len(patterns):
                print(f"  All {len(patterns)} patterns found!")
                break

    elapsed = time.time() - t0
    print(f"  Done in {elapsed:.0f}s. Found {len(results)}/{len(patterns)} textures.")

    # Report not found
    for name in patterns:
        if name not in results:
            print(f"  NOT FOUND: {name}")

    return results

def main():
    # Configure these for your environment, or pass via CLI args:
    #   python bulk_texture_search.py <game-dir> <dds-reference-dir>
    if len(sys.argv) >= 3:
        game_dir = sys.argv[1]
        dds_dir = sys.argv[2]
    else:
        print("Usage: python bulk_texture_search.py <game-dir> <dds-reference-dir>")
        print("  game-dir          path to a Dying Light: Bad Blood install")
        print("  dds-reference-dir folder with DDS files to search for")
        return 2

    rpack1 = os.path.join(game_dir, "DW", "Data", "common_cod_1_PC.rpack")
    rpack2 = os.path.join(game_dir, "DW", "Data", "common_cod_2_PC.rpack")

    # Only search for _dif textures (diffuse maps are what we need to yellow-ify)
    print("Loading DDS search patterns...")
    all_patterns = load_search_patterns(dds_dir)

    # Filter to only _dif textures (skip _nrm, _spc)
    dif_patterns = {k: v for k, v in all_patterns.items() if '_dif' in k}
    print(f"\nFiltered to {len(dif_patterns)} diffuse (_dif) textures")

    # Search cod_2 (FPP)
    results_cod2 = search_rpack(rpack2, dif_patterns)

    # Search cod_1 (TPP)
    results_cod1 = search_rpack(rpack1, dif_patterns)

    # Summary
    print("\n" + "="*80)
    print("SUMMARY - Texture Offsets Found")
    print("="*80)

    print(f"\n--- common_cod_2_PC.rpack (FPP) ---")
    for name, info in sorted(results_cod2.items()):
        print(f"  {name}: offset={info['offset']} ({info['offset']:#x}), size={info['raw_size']}, fmt={info['format']}, {info['width']}x{info['height']}")

    print(f"\n--- common_cod_1_PC.rpack (TPP) ---")
    for name, info in sorted(results_cod1.items()):
        print(f"  {name}: offset={info['offset']} ({info['offset']:#x}), size={info['raw_size']}, fmt={info['format']}, {info['width']}x{info['height']}")

    # Save results to file
    out_path = os.path.join(base, "artifacts", "texture_offsets.txt")
    with open(out_path, 'w') as f:
        f.write("# Texture offsets found via brute-force pixel search\n")
        f.write(f"# Generated: {time.strftime('%Y-%m-%d %H:%M:%S')}\n\n")

        f.write("# common_cod_2_PC.rpack (FPP)\n")
        for name, info in sorted(results_cod2.items()):
            f.write(f"{name}\t{info['offset']}\t{info['raw_size']}\t{info['format']}\t{info['width']}x{info['height']}\n")

        f.write("\n# common_cod_1_PC.rpack (TPP)\n")
        for name, info in sorted(results_cod1.items()):
            f.write(f"{name}\t{info['offset']}\t{info['raw_size']}\t{info['format']}\t{info['width']}x{info['height']}\n")

    print(f"\nResults saved to {out_path}")

if __name__ == '__main__':
    main()
