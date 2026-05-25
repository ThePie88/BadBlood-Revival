# Credits

## Project

- **MrPie (Filip Otto)** — project lead. Server, launcher, patcher, hooks,
  reverse engineering, integration. Discovered the engine offsets, the
  auth-response format, the HTTP gate flag at `[+0x80]`, the SSL verify
  bypass, the EAC launcher-check bypass, and the `RCreateTexture2D` hook
  point. Designed the architecture.

- **Crigrey** — addon skin pipeline, custom texture pack creation. Did
  the artwork and rpack tooling for the first community-shipped skin
  that uses the texture hook end-to-end (mural / tiger / panther variants).

## Game and engine

- `Dying Light: Bad Blood` — **Techland S.A.**, 2018. Property of Techland.
- Chrome Engine 6 (the engine `Dying Light: Bad Blood` runs on) — **Techland**.

This project is not affiliated with, endorsed by, or sponsored by Techland.
We hope the game gets a re-release one day; until then this exists for
the players who already bought it.

## Third-party software we lean on

- **[Goldberg Steam Emulator](https://gitlab.com/Mr_Goldberg/goldberg_emulator)**
  by Nemirtingas / Mr_Goldberg. LGPL-3.0. Handles Steam P2P matchmaking
  without the real Steam network.
  Maintained fork: **[gbe_fork](https://github.com/Detanup01/gbe_fork)**
  by Detanup01 and contributors.

- **[Steamless](https://github.com/atom0s/Steamless)** by atom0s. MIT.
  Removes the SteamStub DRM wrapper from `BadBloodGame.exe`.

- **[QuickBMS](https://aluigi.altervista.org/quickbms.htm)** by Luigi Auriemma.
  Free for non-commercial use. Used during reverse engineering of `.pak`
  and `.rpack` formats.

- **[stunnel](https://www.stunnel.org/)** by Michal Trojnara. GPL-2.0.
  Terminates TLS in front of the FastAPI backend.

- **[Dear ImGui](https://github.com/ocornut/imgui)** by Omar Cornut + contributors.
  MIT. UI for the launcher.

- **[stb_image](https://github.com/nothings/stb)** by Sean Barrett.
  Public Domain / MIT. Loads PNG textures in the launcher.

- **[FastAPI](https://fastapi.tiangolo.com/)** by Sebastián Ramírez. MIT.
  Web framework for the server.

- **[Uvicorn](https://www.uvicorn.org/)** by Tom Christie. BSD-3.
  ASGI runtime under FastAPI.

- **[Pydantic](https://docs.pydantic.dev/)** by Samuel Colvin + team. MIT.
  Schema validation in FastAPI.

- **[httpx](https://www.python-httpx.org/)** by Encode + Tom Christie. BSD-3.
  Used by `proxy_to_techland.py`.

- **[Frida](https://frida.re/)** by Ole André V. Ravnås. wxWindows License.
  Runtime instrumentation during reverse engineering (see
  `tools/texture_hook/frida_*.py`).

- **[Triton](https://triton-library.github.io/)**. Apache 2.0.
  Symbolic execution that led to the discovery of the HTTP transport
  gate at `[+0x80]`.

- **[IDA Free](https://hex-rays.com/ida-free/)** / **[Ghidra](https://ghidra-sre.org/)**.
  Used for static disassembly of `engine_x64_rwdi.dll`.

## Community

People who tested early builds, reported bugs, and patiently helped find
edge cases — thank you. (Add your name here if you want, PRs welcome.)

## Special thanks

To the original Techland devs who built a fun game. We hope you don't
mind us keeping it alive.
