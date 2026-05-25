# Recon Findings

Verified facts from the static and DNS reconnaissance phase that preceded
the actual server reimplementation. Original observations from late 2026.

---

## DNS for dlbb.com

| Host | DNS resolution | HTTP behaviour | Notes |
|------|----------------|----------------|-------|
| `pls.dlbb.com`    | 54.224.159.9, 44.205.54.209 | 404 on `/`, **500 on `/auth/login/steam/`** | Backend is still on AWS, the endpoint exists but is broken |
| `plsdev.dlbb.com` | 54.204.34.57, 52.201.209.139 | Timeout | DNS resolves but the server doesn't respond |
| `api.dlbb.com`    | NXDOMAIN | — | Doesn't exist |
| `dlbb.com`        | Multiple IPs (AWS S3 range) | — | S3 bucket, likely legacy static hosting |

**Conclusion:** the production PLS server is still running but
non-functional. Irrelevant for our purposes — the client needs to
become standalone-from-Techland.

---

## Steam AppID 766370 — NOT hardcoded in binaries

Byte-pattern scan for `E2 B3 0B 00` (766370 little-endian) across:

- `engine_x64_rwdi.dll` → **0 hits**
- `gamedll_x64_rwdi.dll` → **0 hits**
- `BadBloodGame.exe`     → **0 hits**
- `BadBloodGameLauncher.exe` → **0 hits**

**Conclusion:** the AppID is read exclusively from `steam_appid.txt`.
Overwrite the file (we use `480`, Spacewar) — no binary patching needed
for AppID.

---

## EasyAntiCheat_x64.dll exports

277 named exports total. Breakdown:

### Real EAC functions (50) — stubbed in our replacement DLL

```
Cerberus_BeginFrame
Cerberus_EndFrame
Cerberus_GameRoundEnd
Cerberus_GameRoundStart
Cerberus_PlayerDespawn
Cerberus_PlayerRevive
Cerberus_PlayerSpawn
Cerberus_PlayerTakeDamage
Cerberus_PlayerTick
Cerberus_PlayerUseWeapon
ClientAuth_ClientWriteChallengeResponse
ClientAuth_Destroy
ClientAuth_Initialize
CreateClientAuth
CreateGameClient
CreateGameLauncher
CreateHttpsClient
CreateThirdPartyLauncher
GameClientP2P_BeginSession
GameClientP2P_Cerberus
GameClientP2P_EndSession
GameClientP2P_InitLocalization
GameClientP2P_PollForMessageToPeer
GameClientP2P_PollStatus
GameClientP2P_ReceiveMessageFromPeer
GameClientP2P_RegisterPeer
GameClientP2P_ResetState
GameClientP2P_SetLogCallback
GameClientP2P_SetMaxAllowedMessageLength
GameClientP2P_UnregisterPeer
GameClientP2P_UpdatePlatformUserAuthTicket
GameClient_ConnectionReset
GameClient_Destroy
GameClient_Initialize
GameClient_NetProtect
GameClient_PollStatus
GameClient_PopNetworkMessage
GameClient_PushNetworkMessage
GameClient_SetMaxAllowedMessageLength
GameClient_ValidateServerHost
GameLauncher_Destroy
GameLauncher_GetGameProcessId
GameLauncher_OpenGameProcess
GameLauncher_StartGameA
GameLauncher_StartGameW
NetProtectClient_GetProtectMessageOutputLength
NetProtectClient_ProtectMessage
NetProtectClient_UnprotectMessage
ThirdPartyLauncher_Destroy
ThirdPartyLauncher_Initialize
ThirdPartyLauncher_SetServer
```

### libconfig C++ functions (~227)
Embedded configuration library. Probably not called by the game directly
but needed in the export table for linkage to succeed. Could be added to
the stub as no-ops if a future build starts depending on them.

---

## EasyAntiCheat_x86.dll

- PE32 (x86), 0 named exports
- Probably ordinal-only, or unused by the x64 game
- Stub: empty DLL whose `DllMain` returns `TRUE`

---

## BattlEye exports

### BEClient_x64.dll
- 2 exports: `Init`, `GetVer`

### BEServer_x64.dll
- 2 exports: `Init`, `GetVer`

Both stubbed to return success / a version string. The game never calls
anything else on them.

---

## steam_appid.txt

- Original Steam value: `766370`
- We use: `480` (Spacewar). Goldberg works against any valid AppID;
  using a public-test one avoids interfering with anything Steam-side
  if Steam happens to be running.
