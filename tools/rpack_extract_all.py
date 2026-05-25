#!/usr/bin/env python3
"""
RP6L rpack full extractor — extracts ALL files with correct offsets.
Corrected part entry format:
  bytes 0-3: flags
  bytes 4-5: type (uint16)
  bytes 6-7: index (uint16)
  bytes 8-11: file_offset (uint32) — absolute offset in rpack
  bytes 12-15: data_size (uint32) — size of raw data
"""
import struct
import sys
import os

# DDS header templates
def make_dds_header_rgba(width, height):
    """Create a DX10 DDS header for RGBA 2048x2048."""
    # Standard DDS header (128 bytes) + DX10 extension (20 bytes) = 148 bytes
    header = bytearray(148)
    header[0:4] = b'DDS '
    struct.pack_into('<I', header, 4, 124)  # dwSize
    struct.pack_into('<I', header, 8, 0x1 | 0x2 | 0x4 | 0x1000)  # dwFlags: CAPS|HEIGHT|WIDTH|PIXELFORMAT
    struct.pack_into('<I', header, 12, height)
    struct.pack_into('<I', header, 16, width)
    struct.pack_into('<I', header, 20, width * 4)  # pitch
    struct.pack_into('<I', header, 76, 32)  # pixel format size
    struct.pack_into('<I', header, 80, 0x4)  # DDPF_FOURCC
    header[84:88] = b'DX10'
    struct.pack_into('<I', header, 108, 0x1000)  # DDSCAPS_TEXTURE
    # DX10 extension
    struct.pack_into('<I', header, 128, 28)  # DXGI_FORMAT_R8G8B8A8_UNORM
    struct.pack_into('<I', header, 132, 3)  # D3D10_RESOURCE_DIMENSION_TEXTURE2D
    struct.pack_into('<I', header, 140, 1)  # arraySize
    return bytes(header)

def make_dds_header_dxt(width, height, fourcc):
    """Create a DDS header for DXT1 or DXT5."""
    header = bytearray(128)
    header[0:4] = b'DDS '
    struct.pack_into('<I', header, 4, 124)
    struct.pack_into('<I', header, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000)  # +LINEARSIZE
    struct.pack_into('<I', header, 12, height)
    struct.pack_into('<I', header, 16, width)
    bpp = 8 if fourcc == b'DXT1' else 16
    struct.pack_into('<I', header, 20, max(1, width // 4) * max(1, height // 4) * bpp)
    struct.pack_into('<I', header, 76, 32)
    struct.pack_into('<I', header, 80, 0x4)  # DDPF_FOURCC
    header[84:88] = fourcc
    struct.pack_into('<I', header, 108, 0x1000)
    return bytes(header)

def guess_texture_format(data_size):
    """Guess texture format from data size."""
    # Common sizes for 2048x2048 textures
    known = {
        22369620: ('RGBA', 2048, 2048, 'mips'),
        16777216: ('RGBA', 2048, 2048, 'base'),
        5592432:  ('DXT5', 2048, 2048, 'mips'),
        5592404:  ('DXT5', 2048, 2048, 'mips'),
        4194304:  ('DXT5', 2048, 2048, 'base'),
        2796200:  ('DXT1', 2048, 2048, 'mips'),
        2097152:  ('DXT1', 2048, 2048, 'base'),
        699064:   ('DXT1', 1024, 1024, 'mips'),
        699088:   ('DXT1', 1024, 1024, 'mips'),
        524288:   ('DXT1', 1024, 1024, 'base'),
        349524:   ('DXT1', 512, 512, 'mips'),
        174760:   ('DXT1', 256, 256, 'mips'),
        1398128:  ('DXT5', 1024, 1024, 'mips'),
        1048576:  ('DXT5', 1024, 1024, 'base'),
    }
    return known.get(data_size)

def main():
    if len(sys.argv) < 3:
        print("Usage: rpack_extract_all.py <rpack_file> <output_dir>")
        sys.exit(1)

    rpack_path = sys.argv[1]
    out_dir = sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    file_size = os.path.getsize(rpack_path)
    f = open(rpack_path, 'rb')

    # Header
    magic = f.read(4)
    assert magic == b'RP6L', f"Not RP6L: {magic}"
    hdr = struct.unpack('<8I', f.read(32))
    version, compression, part_count, section_count, file_count, fn_chunk_len, fn_count, block_size = hdr

    print(f"RP6L v{version}: {part_count} parts, {section_count} sections, {file_count} files")
    print(f"String data: {fn_chunk_len} bytes, {fn_count} names")

    # Skip section table
    f.seek(36 + section_count * 16)

    # Read part table with CORRECTED format
    parts = []
    for i in range(part_count):
        raw = f.read(16)
        flags = struct.unpack_from('<I', raw, 0)[0]
        dtype = struct.unpack_from('<H', raw, 4)[0]
        didx = struct.unpack_from('<H', raw, 6)[0]
        file_off = struct.unpack_from('<I', raw, 8)[0]
        data_sz = struct.unpack_from('<I', raw, 12)[0]
        parts.append({
            'flags': flags, 'type': dtype, 'idx': didx,
            'offset': file_off, 'size': data_sz
        })

    # Read file table
    files = []
    for i in range(file_count):
        raw = f.read(12)
        pcnt = raw[0]
        ftype = raw[2]
        fidx = struct.unpack_from('<I', raw, 4)[0]
        fpart = struct.unpack_from('<I', raw, 8)[0]
        files.append({
            'part_count': pcnt, 'filetype': ftype,
            'file_index': fidx, 'first_part': fpart
        })

    # Read filename descriptors
    fn_descs = []
    for i in range(file_count):
        a, b, c = struct.unpack('<III', f.read(12))
        fn_descs.append(a)

    # Read string data
    str_data = f.read(fn_chunk_len)

    # Build filenames
    filenames = []
    for i, off in enumerate(fn_descs):
        if off < len(str_data):
            end = str_data.find(b'\x00', off)
            if end == -1:
                end = len(str_data)
            name = str_data[off:end].decode('ascii', errors='ignore')
            if name and all(c.isprintable() for c in name):
                filenames.append(name)
            else:
                filenames.append(f'unnamed_{i:03d}')
        else:
            filenames.append(f'unnamed_{i:03d}')

    type_names = {0x10: 'mesh', 0x12: 'skin', 0x20: 'texture', 0x30: 'material', 0xFF: 'reslist'}

    # Extract each file
    extracted = 0
    errors = 0

    for i in range(file_count):
        fi = files[i]
        name = filenames[i]
        tname = type_names.get(fi['filetype'], f'type_{fi["filetype"]:02x}')

        # Collect all parts' data
        total_data = bytearray()
        first_part_info = None
        valid = True

        for p in range(fi['part_count']):
            pi = fi['first_part'] + p
            if pi >= part_count:
                valid = False
                break
            part = parts[pi]
            if first_part_info is None:
                first_part_info = part

            off = part['offset']
            sz = part['size']

            if off + sz > file_size or off < 0 or sz < 0 or sz > file_size:
                valid = False
                break

            f.seek(off)
            total_data.extend(f.read(sz))

        if not valid or len(total_data) == 0:
            errors += 1
            continue

        # Determine output filename and add DDS header for textures
        ext = '.raw'
        dds_header = b''

        if fi['filetype'] == 0x20:  # texture
            fmt_info = guess_texture_format(len(total_data))
            if fmt_info:
                fmt, w, h, mip_type = fmt_info
                if fmt == 'RGBA':
                    dds_header = make_dds_header_rgba(w, h)
                elif fmt == 'DXT1':
                    dds_header = make_dds_header_dxt(w, h, b'DXT1')
                elif fmt == 'DXT5':
                    dds_header = make_dds_header_dxt(w, h, b'DXT5')
                ext = '.dds'
            else:
                ext = '.raw_tex'
        elif fi['filetype'] == 0x10:
            ext = '.msh'
        elif fi['filetype'] == 0x30:
            ext = '.mat'

        # For the big 22MB composite files, extract sub-textures too
        if len(total_data) == 22369620:
            # This contains: DXT data (first ~2.9MB) + RGBA base (16MB) + RGBA mips
            # Save the whole thing AND extract the RGBA base as a separate DDS
            sub_dir = os.path.join(out_dir, f'{name}_parts')
            os.makedirs(sub_dir, exist_ok=True)

            # Find where RGBA starts (look for transition from DXT to RGBA)
            # RGBA section: 16777216 bytes, starts where the last 16MB begins
            rgba_start = len(total_data) - 16777216  # simplification: RGBA at the end
            # Actually try to detect: scan for consistent 4-byte RGBA pixel patterns
            # For now, save at common offsets
            for rgba_off in [0, 2209941, 2909005]:
                chunk = total_data[rgba_off:rgba_off + 16777216]
                if len(chunk) == 16777216:
                    hdr = make_dds_header_rgba(2048, 2048)
                    with open(os.path.join(sub_dir, f'rgba_at_{rgba_off}.dds'), 'wb') as of:
                        of.write(hdr + chunk)

            # Also save first 2909005 bytes as DXT
            dxt_chunk = total_data[:2909005]
            with open(os.path.join(sub_dir, f'dxt_section.raw'), 'wb') as of:
                of.write(dxt_chunk)

        # Save main file
        out_path = os.path.join(out_dir, f'{name}{ext}')
        with open(out_path, 'wb') as of:
            of.write(dds_header + total_data)

        extracted += 1
        sz_kb = len(total_data) / 1024
        print(f"  [{i:3d}] {tname:>10s} {name}{ext} ({sz_kb:.0f} KB)")

    f.close()
    print(f"\nExtracted {extracted}/{file_count} files ({errors} errors)")
    print(f"Output: {out_dir}")

if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
