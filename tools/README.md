# tools/

Research and one-off helper scripts. Not part of the deployable system —
kept here because they're useful for further reverse engineering, debugging,
or making new skin packs.

| Script                       | What it does |
|------------------------------|--------------|
| `rpack_tool.py`              | List entries in a `.rpack` archive. Extracting still has TODO around offset resolution (brute-force pixel search in some sections). |
| `rpack_extract_all.py`       | Batch RP6L extraction across a folder of rpacks. |
| `bulk_texture_search.py`     | Scan rpacks for a known texture name across the install. |
| `extract_rgba_textures.py`   | Convert raw RGBA blobs found in rpacks into viewable DDS. |
| `patch-launcher-check.py`    | Stand-alone PE-aware patcher for just the EAC launcher bypass at 0x6F73DE. Superseded by `patcher/apply_patches.py` but useful as a minimal example of the PE-RVA-to-file-offset dance. Edit the hardcoded `ENGINE_PATH` at the top before running. |
| `memscan.py`                 | Walk process memory looking for a needle. Used while hunting the gate-flag offset that became patch 0x7152C6. |

None of these are required for normal operation of the server, launcher,
or patcher. They're here for people who want to dig further into the
client's file formats.

## A note on third-party tools

The original workspace included copies of [QuickBMS](https://aluigi.altervista.org/quickbms.htm),
[Steamless](https://github.com/atom0s/Steamless), and Goldberg Steam Emu.
These are NOT redistributed here — install them from their original sources:

- **Steamless**: <https://github.com/atom0s/Steamless/releases>
- **QuickBMS**: <https://aluigi.altervista.org/quickbms.htm>
- **Goldberg Steam Emu**: <https://gitlab.com/Mr_Goldberg/goldberg_emulator>
  (or the maintained fork at <https://github.com/Detanup01/gbe_fork>)
