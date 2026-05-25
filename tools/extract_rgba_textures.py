#!/usr/bin/env python3
"""
Extract all RGBA 2048x2048 textures from rpack as viewable DDS files.
Auto-identifies Agent textures by comparing with Crigrey's DDS files.
"""
import struct, sys, os, hashlib

def make_dds_header_rgba(width, height):
    header = bytearray(148)
    header[0:4] = b'DDS '
    struct.pack_into('<I', header, 4, 124)
    struct.pack_into('<I', header, 8, 0x1 | 0x2 | 0x4 | 0x1000)
    struct.pack_into('<I', header, 12, height)
    struct.pack_into('<I', header, 16, width)
    struct.pack_into('<I', header, 20, width * 4)
    struct.pack_into('<I', header, 76, 32)
    struct.pack_into('<I', header, 80, 0x4)
    header[84:88] = b'DX10'
    struct.pack_into('<I', header, 108, 0x1000)
    struct.pack_into('<I', header, 128, 28)  # DXGI_FORMAT_R8G8B8A8_UNORM
    struct.pack_into('<I', header, 132, 3)
    struct.pack_into('<I', header, 140, 1)
    return bytes(header)

def main():
    # CLI args: <rpack-path> <output-dir> [<reference-dir>]
    if len(sys.argv) < 3:
        print("Usage: python extract_rgba_textures.py <rpack-path> <output-dir> [reference-dds-dir]")
        return 2
    rpack_path = sys.argv[1]
    out_dir = sys.argv[2]
    crigrey_dir = sys.argv[3] if len(sys.argv) > 3 else None

    os.makedirs(out_dir, exist_ok=True)
    file_size = os.path.getsize(rpack_path)

    # Load Crigrey reference hashes (from +7MB where data is unique)
    crigrey_refs = {}
    if crigrey_dir and os.path.isdir(crigrey_dir):
        for fname in os.listdir(crigrey_dir):
            if not fname.endswith('.dds') or '_dif' not in fname:
                continue
            fpath = os.path.join(crigrey_dir, fname)
            with open(fpath, 'rb') as df:
                dds = df.read()
            hdr_sz = 148 if dds[84:88] == b'DX10' else 128
            raw = dds[hdr_sz:]
            if len(raw) >= 8*1024*1024:
                # Hash bytes at +7MB (unique region)
                sample = raw[7*1024*1024:7*1024*1024 + 256]
                h = hashlib.md5(sample).hexdigest()
                name = fname.replace('.dds', '')
                crigrey_refs[h] = name
        print(f"Loaded {len(crigrey_refs)} Crigrey reference textures")

    # Parse rpack parts
    with open(rpack_path, 'rb') as f:
        f.seek(4)
        hdr = struct.unpack('<8I', f.read(32))
        part_count = hdr[2]
        section_count = hdr[3]

        f.seek(36 + section_count * 16)
        parts = []
        for i in range(part_count):
            raw = f.read(16)
            foff = struct.unpack_from('<I', raw, 8)[0]
            dsz = struct.unpack_from('<I', raw, 12)[0]
            parts.append((i, foff, dsz))

    # Find all 22369620-byte parts (RGBA 2048x2048 with mipmaps)
    rgba_parts = [(i, off, sz) for i, off, sz in parts if sz == 22369620]
    print(f"Found {len(rgba_parts)} RGBA 2048x2048 composite textures")

    dds_header = make_dds_header_rgba(2048, 2048)

    with open(rpack_path, 'rb') as f:
        for idx, (part_i, part_off, part_sz) in enumerate(rgba_parts):
            # Determine RGBA start offset within the part
            # Check if RGBA starts at 0 or at 2909005
            f.seek(part_off)
            first_bytes = f.read(16)

            # RGBA pixels have format: RR GG BB FF (alpha=0xFF usually)
            # Check if first bytes look like RGBA (alpha byte at position 3, 7, 11, 15)
            looks_rgba_at_0 = all(first_bytes[i] == 0xFF for i in [3, 7, 11, 15] if i < len(first_bytes))

            if looks_rgba_at_0:
                rgba_offset = 0
            else:
                rgba_offset = 2909005  # DXT data first, then RGBA

            # Read 16MB of RGBA data
            abs_rgba = part_off + rgba_offset
            if abs_rgba + 16777216 > file_size:
                continue

            f.seek(abs_rgba)
            rgba_data = f.read(16777216)

            # Try to identify using Crigrey refs
            sample = rgba_data[7*1024*1024:7*1024*1024 + 256]
            h = hashlib.md5(sample).hexdigest()

            crigrey_name = crigrey_refs.get(h)

            if crigrey_name:
                out_name = f"{crigrey_name}.dds"
                label = f"AGENT: {crigrey_name}"
            else:
                out_name = f"texture_{idx:03d}_part{part_i}.dds"
                label = "unknown"

            out_path = os.path.join(out_dir, out_name)
            with open(out_path, 'wb') as of:
                of.write(dds_header + rgba_data)

            print(f"  [{idx:3d}] Part[{part_i:3d}] off={part_off:12d} rgba@+{rgba_offset} -> {out_name} ({label})")

    print(f"\nDone! DDS files saved to: {out_dir}")
    print("Open them in any DDS viewer (e.g., Paint.NET, GIMP with DDS plugin, Windows Texture Viewer)")

if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
