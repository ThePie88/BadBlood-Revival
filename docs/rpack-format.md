# RP6L Rpack Format Research — DLBB (Chrome Engine 6, v1)

## Header (36 bytes)
```
Offset  Type     Field              Value (common_cod_2_PC.rpack)
0x00    char[4]  magic              "RP6L"
0x04    uint32   version            1
0x08    uint32   compression        1
0x0C    uint32   partCount          938
0x10    uint32   sectionCount       9
0x14    uint32   fileCount          310
0x18    uint32   fnChunkLen         8436
0x1C    uint32   fnCount            310
0x20    uint32   blockSize          1
```

## Table Layout in File
```
Offset    Size           Table
36        9 * 16         Section table (144 bytes, ends at 180)
180       938 * 16       Part table (15008 bytes, ends at 15188)
15188     310 * 12       File table (3720 bytes, ends at 18908)
18908     310 * 12       Filename descriptors (3720 bytes, ends at 22628)
22628     8436           String data (ends at 31064)
~28620    ...            Actual texture/resource data begins
```

## IMPORTANT: DLBB v1 differs from DL2
- Lua extractor (Qivex/rpack-extract) was made for DL2
- DL2 uses 20-byte sectionInfo (has extra uint32 unknown4) — DLBB uses 16-byte
- DL2 uses 4-byte filename offsets (uint32 array) — DLBB uses 12-byte filename descriptors
- The Lua extractor will NOT work correctly on DLBB rpack files

## Section Table (16 bytes each, 9 entries)
```c
struct sectionInfo {   // DLBB v1 — 16 bytes, NOT 20
    uint8_t  filetype;
    uint8_t  unknown1;
    uint8_t  unknown2;
    uint8_t  unknown3;
    uint32_t offset;
    uint32_t unpackedSize;
    uint32_t packedSize;
};
```
### Verified sections:
- Section[0]: ft=0x12(skin), off=28620, unp=12292, pak=3718 — **DECOMPRESSES OK** (zlib)
- Section[5]: ft=0x20(texture), off=2908258, unp=45600, pak=747 — **DECOMPRESSES OK** (zlib)
- Other sections have implausible values — may be a different struct or mixed format

## Part Table (16 bytes each, 938 entries)
```c
struct partInfo {      // DLBB v1 — 16 bytes
    uint8_t  sectionIndex;
    uint8_t  unknown1;
    uint16_t fileIndex;
    uint32_t offset;
    uint32_t size;
    uint32_t unknown2;
};
```
### Example: Part[308] (for file[103] = player_6_torso_mask_b_dif)
```
Raw hex: 00 00 00 00 05 00 63 00 9C AC D6 28 54 55 55 01
Standard parse: sec=0, file=0, offset=6488069, size=685157532, unk=22369620
```
- `size=685157532` is implausible (653MB for one part)
- `unk=22369620` = exact BGRA mip chain size for 2048x2048 texture
- `offset=6488069` (0x630005) — upper bytes may encode an index

### Alternative interpretation of Part[308]:
```
00 00 00 00   — padding/flags
05 00         — data type? (5 = texture base mip?)
63 00         — index 99 into something?
9C AC D6 28   — file offset 685157532 — VERIFIED: has pixel data, but WRONG TEXTURE
54 55 55 01   — size 22369620 = total BGRA with all mips for 2048x2048
```
**PROBLEM**: offset 685157532 points to a UV map texture, NOT the mask_b grey t-shirt.
The part struct parsing is incorrect — we have NOT decoded the correct offset mapping.

## File Table (12 bytes each, 310 entries)
```c
struct fileInfo {      // DLBB v1 — 12 bytes
    uint8_t  partCount;
    uint8_t  unknown1;
    uint8_t  filetype;
    uint8_t  unknown2;
    uint32_t fileIndex;
    uint32_t firstPart;
};
```
### VERIFIED file[103]:
- partCount=3, filetype=0x20 (texture), fileIndex=100, firstPart=308
- 3 parts: [308], [309], [310]
- Part sizes (unk2 field): 22369620, 21844, 151 — match BGRA mip chain sizes

## Filename Descriptors (12 bytes each, 310 entries)
**NOT uint32 offsets like DL2!** Each descriptor is 12 bytes:
```c
struct fnDescriptor {  // DLBB v1 — 12 bytes
    uint32_t nameOffset;  // offset into string data -> gives FULL texture name
    uint32_t field_b;     // gives partial/truncated name — purpose unknown
    uint32_t field_c;     // gives another partial name — purpose unknown
};
```
### VERIFIED:
- Descriptor[103].nameOffset = 3250 → "player_6_torso_mask_b_dif" at file offset 25878 ✓
- The `nameOffset` field (first uint32) is the correct name pointer

## String Data (8436 bytes at offset 22628)
- Null-terminated ASCII strings
- Contains all texture/resource names
- Indexed by fnDescriptor.nameOffset

## Texture Storage Format
**CORRECTION: Textures are stored as raw DXT5 data (same as inside DDS, without header)**

### DXT5 storage verified by pixel search:
- Searched for raw DXT5 block data from known texture (tiger glove) in rpack
- Found EXACT match at offset 2444781533 for player_9_jacket_tiger_fpp_dif
- Extracted as DDS → correct glove shape visible
- Injection = copy DDS data (skip 128-byte header) directly to rpack offset

### Size field (22369620) mystery:
- 22369620 = 4 × DXT5_size (5592405) for 2048x2048 with mips
- Possibly multiple textures packed per entry, or different meaning
- The actual DXT5 data per texture is ~5592432 bytes (= DDS file minus 128-byte header)

### Previous BGRA assumption was WRONG:
- Data at the 'c' field offset looked like BGRA pixels but was coincidental
- Actual match found by searching for DXT5 compressed block data

## The 9-Entry Table (after 938 parts)
This table at offset 15044 has 9 entries of 16 bytes. Purpose unclear.
```
Entry 0: 0, 20054021 (0x1320005), 2871264992, 5592432
Entry 1: 0, 20054273 (0x1320101), 4384480,    5488
Entry 2: 0, 20119556 (0x1330004), 45144,       151
```
- Value 5592432 = DXT5 DDS data size (texture without header)
- The second field has packed format: upper bits = index into 938-table?
  - 0x0132 = 306, 0x0133 = 307, 0x0134 = 308, 0x0135 = 309
  - Lower bits: 0x0005, 0x0101, 0x0004 — data type flags?

## optimized_dx11.mp File
- Header: "ABDM" (materials database)
- 13.9MB, contains material definitions
- Has texture filenames like "player_6_torso_mask_b_dif.dds"
- Does NOT contain rpack file offsets
- CANNOT be deleted — game crashes without it
- Purpose: maps material names to texture names and shader parameters

## What Works
- ✅ Filename listing (rpack_tool.py list)
- ✅ File table parsing (part_count, filetype, first_part)
- ✅ BGRA texture format identified with mip chain
- ✅ Texture injection mechanics (write raw BGRA at correct offset)

## BREAKTHROUGH: Field 'c' = actual file offset
The `c` field (bytes 8-11) of the 938-entry part table IS the file offset of the raw texture data.

**Proof**: Entry[308].c = 0x24D6ACA0 = 618048672
- Data at this offset = valid BGRA pixel data
- Extracted as PNG = exact match for the mask_b torso texture visible in-game
- Entry[311].c - Entry[308].c = 22369620 = exact BGRA mip chain size ✓

### Part entry struct (CORRECTED):
```
Bytes: 00 00 00 00 05 00 63 00 9C AC D6 28 54 55 55 01
       ├─ a ─────┤ ├b─┤ ├c─┤ ├─ file_offset ─┤ ├─ data_size ──┤
       padding     type  idx   0x24D6ACA0       22369620
```
- a (uint32): always 0 (padding)
- b (uint16): data type (5 = base mip texture, 1 = reduced mip, 4 = tiny mip?)
- c (uint16): some index (99 for entry 308)
- file_offset (uint32): **ACTUAL offset in the rpack file** ← THIS IS THE KEY
- data_size (uint32): size of raw data at that offset

### 92 textures of size 2048x2048 found (22369620 bytes BGRA each)

## TEXTURE INJECTION VERIFIED WORKING
- Overwrote `player_9_jacket_mural_fpp_dif` (RGBA, 16MB) at offset 2400042313
- In-game result: arm skin color changed, confirming the texture was applied
- `jacket_*_fpp` textures = ARM/HAND SKIN, not the leather gloves
- The black leather gloves are a SEPARATE mesh material, not skin-controlled

## optimized_dx11.mp Structure (ABDM format)
```
Header: ABDM + 12 bytes
5 sections, each with 32-byte name + 16 bytes (count1, count2, data_size, pad):

Section              Count    Data Size    Offset      Bytes/Entry
hl_shaders           3887     385696       256         ~99
strings              20708    1007648      385952      variable (null-terminated)
templates_0000       2272     1948576      1393600     ~857
expressions          4181     3201376      3342176     ~765
exp_fixups           4181     3742336      6543552     ~895
```

- strings = all asset filenames (.mat, .dds, etc.)
- templates_0000 = material definitions (2272 materials, ~857 bytes each)
- Templates reference strings by INDEX, not by name directly
- Material templates control which textures a material uses and shader parameters
- The glove appearance is likely controlled by a template that overrides the skin texture

## What's Still Unknown
- ❌ Template binary format (how to read which texture a template references)
- ❌ Which template controls the black leather gloves
- ❌ Whether the glove material ignores per-skin textures or uses a hardcoded default
- ❌ Whether sections (first 9 entries in rpack) serve a separate purpose
- ❌ Exact meaning of the 'b' (type) and 'c' (index) fields in part entries

## Key Files
- Rpack: `DW/Data/common_cod_2_PC.rpack` (2.69 GB, 310 files, 938 parts, 9 sections)
- Backup: `common_cod_2_PC.rpack.pre_inject` and `.bak`
- Material DB: `DW/Data/optimized_dx11.mp` (13.9MB, DO NOT DELETE)
- Tool: `tools/rpack_tool.py` (list command works, extract/replace need correct offsets)
- Target texture: file[103] "player_6_torso_mask_b_dif", 3 parts starting at part[308]

## References
- Qivex/rpack-extract (Lua, DL2): https://github.com/Qivex/rpack-extract
- hhrhhr RP6L gist (Lua): https://gist.github.com/hhrhhr/c270fa8dd41abcc08f0cab652164130b
- RenaissancePack (C++, hardcoded offsets): github.com/MrMemes20/RenaissancePack
- DL-rpack-dat-tool (QuickBMS wrapper): github.com/Parapando/DL-rpack-dat-tool
- 010 Editor scripts: nexusmods.com/dyinglight2/mods/583
