"""
RP6L rpack texture tool - extract and replace textures by name
Built for Dying Light / DLBB Chrome Engine 6 rpack files (v1)
Filename descriptors are 12-byte structs, field 'a' = name offset in string data
"""
import struct, zlib, sys, os, shutil

def parse_rpack(path):
    f = open(path, 'rb')
    magic = f.read(4)
    assert magic == b'RP6L', f"Not RP6L: {magic}"

    hdr = struct.unpack('<8I', f.read(32))
    version, compression, part_count, section_count, file_count, fn_chunk_len, fn_count, block_size = hdr
    offset_mult = 16 if version == 4 else 1

    # Section table
    sections = []
    for i in range(section_count):
        data = f.read(16)
        ft = data[0]
        offset, unp, pak = struct.unpack_from('<III', data, 4)
        sections.append({'filetype': ft, 'offset': offset, 'unpacked_size': unp, 'packed_size': pak})

    # Part table
    parts = []
    for i in range(part_count):
        data = f.read(16)
        sec_idx = data[0]
        file_idx = struct.unpack_from('<H', data, 2)[0]
        offset, size = struct.unpack_from('<II', data, 4)
        parts.append({'section': sec_idx, 'file_index': file_idx, 'offset': offset, 'size': size})

    # File table
    files = []
    for i in range(file_count):
        data = f.read(12)
        part_cnt = data[0]
        filetype = data[2]
        file_idx, first_part = struct.unpack_from('<II', data, 4)
        files.append({'part_count': part_cnt, 'filetype': filetype, 'file_index': file_idx, 'first_part': first_part})

    # Filename descriptors (12 bytes each: name_offset, ?, ?)
    fn_descs = []
    for i in range(file_count):
        a, b, c = struct.unpack('<III', f.read(12))
        fn_descs.append(a)  # 'a' = offset into string data

    # String data
    str_data = f.read(fn_chunk_len)

    # Build filenames
    filenames = []
    for off in fn_descs:
        if off < len(str_data):
            end = str_data.find(b'\x00', off)
            if end == -1: end = len(str_data)
            filenames.append(str_data[off:end].decode('ascii', errors='ignore'))
        else:
            filenames.append('')

    return {
        'handle': f, 'path': path,
        'version': version, 'offset_mult': offset_mult,
        'sections': sections, 'parts': parts, 'files': files,
        'filenames': filenames
    }


def extract_file(rpack, file_idx):
    f = rpack['handle']
    fi = rpack['files'][file_idx]
    mult = rpack['offset_mult']

    if not hasattr(extract_file, '_cache'):
        extract_file._cache = {}

    result = bytearray()
    current_part = fi['first_part']

    for p in range(fi['part_count']):
        part = rpack['parts'][current_part]
        sec_idx = part['section']
        sec = rpack['sections'][sec_idx]
        data_len = part['size']

        if sec['packed_size'] > 0:
            if sec_idx not in extract_file._cache:
                f.seek(sec['offset'] * mult)
                compressed = f.read(sec['packed_size'])
                extract_file._cache[sec_idx] = zlib.decompress(compressed)

            section_data = extract_file._cache[sec_idx]
            off = part['offset'] * mult
            result.extend(section_data[off:off + data_len])
        else:
            off = (sec['offset'] + part['offset']) * mult
            f.seek(off)
            result.extend(f.read(data_len))

        current_part += 1

    return bytes(result)


def cmd_list(path, filt=None):
    rpack = parse_rpack(path)
    rpack['handle'].close()
    type_names = {0x10:'mesh', 0x12:'skin', 0x20:'texture', 0x30:'material', 0xFF:'reslist'}
    count = 0
    for i, name in enumerate(rpack['filenames']):
        if filt and filt not in name:
            continue
        fi = rpack['files'][i]
        tn = type_names.get(fi['filetype'], f'0x{fi["filetype"]:02x}')
        print(f"  [{i:3d}] ({tn:>10s}) {name}")
        count += 1
    print(f"\n{count} entries" + (f" matching '{filt}'" if filt else ""))


def cmd_extract(path, name, outdir):
    rpack = parse_rpack(path)
    matches = [(i, n) for i, n in enumerate(rpack['filenames']) if name in n]
    if not matches:
        print(f"Not found: '{name}'")
        rpack['handle'].close()
        return
    os.makedirs(outdir, exist_ok=True)
    for idx, fn in matches:
        data = extract_file(rpack, idx)
        outpath = os.path.join(outdir, fn)
        with open(outpath, 'wb') as of:
            of.write(data)
        print(f"  [{idx}] {fn} -> {outpath} ({len(data)} bytes)")
    rpack['handle'].close()


def cmd_replace(rpack_path, texture_name, new_file):
    rpack = parse_rpack(rpack_path)
    mult = rpack['offset_mult']

    # Find exact match
    matches = [(i, n) for i, n in enumerate(rpack['filenames']) if n == texture_name]
    if not matches:
        matches = [(i, n) for i, n in enumerate(rpack['filenames']) if texture_name in n]
    if len(matches) != 1:
        print(f"Expected 1 match for '{texture_name}', got {len(matches)}:")
        for i, n in matches:
            print(f"  [{i}] {n}")
        rpack['handle'].close()
        return

    idx, name = matches[0]
    fi = rpack['files'][idx]

    # Extract current data to get size
    current = extract_file(rpack, idx)
    print(f"File: {name}")
    print(f"Current size: {len(current)} bytes")

    # Read new data
    with open(new_file, 'rb') as nf:
        new_data = nf.read()

    # Strip DDS header if present
    if new_data[:4] == b'DDS ':
        hdr_size = 128
        if new_data[84:88] == b'DX10':
            hdr_size = 148
        new_data = new_data[hdr_size:]
        print(f"Stripped DDS header ({hdr_size} bytes)")

    print(f"New size: {len(new_data)} bytes")

    if len(new_data) > len(current):
        print(f"ERROR: New data ({len(new_data)}) > current ({len(current)}). Cannot replace in-place.")
        rpack['handle'].close()
        return

    if len(new_data) < len(current):
        print(f"Padding {len(current) - len(new_data)} bytes")
        new_data = new_data + b'\x00' * (len(current) - len(new_data))

    # Backup
    rpack['handle'].close()
    backup = rpack_path + '.pre_inject'
    if not os.path.exists(backup):
        print(f"Backup: {backup}")
        shutil.copy2(rpack_path, backup)

    # Re-parse for writing
    rpack = parse_rpack(rpack_path)
    fi = rpack['files'][idx]
    rpack['handle'].close()

    f = open(rpack_path, 'r+b')
    data_offset = 0
    current_part = fi['first_part']

    for p in range(fi['part_count']):
        part = rpack['parts'][current_part]
        sec_idx = part['section']
        sec = rpack['sections'][sec_idx]
        data_len = part['size']

        if sec['packed_size'] > 0:
            # Compressed section
            f.seek(sec['offset'] * mult)
            compressed = f.read(sec['packed_size'])
            decompressed = bytearray(zlib.decompress(compressed))

            part_off = part['offset'] * mult
            decompressed[part_off:part_off + data_len] = new_data[data_offset:data_offset + data_len]

            recompressed = zlib.compress(bytes(decompressed))
            if len(recompressed) <= sec['packed_size']:
                padded = recompressed + b'\x00' * (sec['packed_size'] - len(recompressed))
                f.seek(sec['offset'] * mult)
                f.write(padded)
                print(f"  Part {p}: recompressed {len(recompressed)} <= {sec['packed_size']} OK")
            else:
                print(f"  Part {p}: ERROR recompressed {len(recompressed)} > {sec['packed_size']}")
                f.close()
                return
        else:
            off = (sec['offset'] + part['offset']) * mult
            f.seek(off)
            f.write(new_data[data_offset:data_offset + data_len])
            print(f"  Part {p}: wrote {data_len} bytes at {off}")

        data_offset += data_len
        current_part += 1

    f.close()
    print(f"\nDone! '{name}' replaced.")


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    if len(sys.argv) < 3:
        print("RP6L rpack tool")
        print("  list <rpack> [filter]")
        print("  extract <rpack> <name> [outdir]")
        print("  replace <rpack> <name> <newfile>")
        sys.exit(1)

    cmd, rpack_path = sys.argv[1], sys.argv[2]
    if cmd == 'list':
        cmd_list(rpack_path, sys.argv[3] if len(sys.argv) > 3 else None)
    elif cmd == 'extract':
        cmd_extract(rpack_path, sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else '.')
    elif cmd == 'replace':
        cmd_replace(rpack_path, sys.argv[3], sys.argv[4])
