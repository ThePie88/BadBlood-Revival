/*
 * EasyAntiCheat_x64.dll stub
 *
 * Exports all 50 EAC functions as no-ops returning success.
 * Created by MrPie — DLBB Revival Project
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <string.h>

static const char CREDIT[] = "BadBlood-Revival EAC stub";

/* ---- DNS Redirect ----
 * Optional belt-and-braces hook for code paths inside the EAC stub that
 * might resolve PLS hostnames bypassing the OS resolver.
 *
 * Defaults treat OLD_HOST == NEW_HOST as a no-op (local mode — the hosts
 * file already redirects pls.dlbb.com to 127.0.0.1). Change NEW_HOST to
 * your public hostname for internet hosting.
 */
#define OLD_HOST "pls.dlbb.com"            /* don't change — game-baked */
#define NEW_HOST "pls.dlbb.com"            /* same as OLD_HOST = no-op (local mode).
                                              Change to your public hostname for
                                              internet hosting, e.g. "pls.example.it". */

typedef INT (WSAAPI *getaddrinfo_t)(PCSTR, PCSTR, const ADDRINFOA *, PADDRINFOA *);
static getaddrinfo_t real_gai = NULL;

static INT WSAAPI hook_gai(PCSTR node, PCSTR svc, const ADDRINFOA *hints, PADDRINFOA *res) {
    if (node && _stricmp(node, OLD_HOST) == 0)
        return real_gai(NEW_HOST, svc, hints, res);
    return real_gai(node, svc, hints, res);
}

static void PatchIAT(HMODULE mod) {
    if (!mod) return;
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != 0x5A4D) return;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[1].VirtualAddress;
    if (!rva) return;
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva);
    for (; imp->Name; imp++) {
        if (_stricmp((char *)(base + imp->Name), "ws2_32.dll") != 0) continue;
        IMAGE_THUNK_DATA *orig = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; orig->u1.AddressOfData; orig++, thunk++) {
            if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG64) continue;
            IMAGE_IMPORT_BY_NAME *n = (IMAGE_IMPORT_BY_NAME *)(base + orig->u1.AddressOfData);
            if (strcmp(n->Name, "getaddrinfo") == 0) {
                if (!real_gai) real_gai = (getaddrinfo_t)thunk->u1.Function;
                DWORD old;
                VirtualProtect(&thunk->u1.Function, 8, PAGE_READWRITE, &old);
                thunk->u1.Function = (ULONGLONG)hook_gai;
                VirtualProtect(&thunk->u1.Function, 8, old, &old);
                return;
            }
        }
    }
}

static void InstallDnsHook(void) {
    HMODULE ws2 = LoadLibraryA("ws2_32.dll");
    if (ws2) real_gai = (getaddrinfo_t)GetProcAddress(ws2, "getaddrinfo");
    PatchIAT(GetModuleHandleA(NULL));
    PatchIAT(GetModuleHandleA("engine_x64_rwdi.dll"));
    PatchIAT(GetModuleHandleA("gamedll_x64_rwdi.dll"));
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)CREDIT;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        InstallDnsHook();
    }
    return TRUE;
}

/* ---- Cerberus (anti-cheat game hooks) ---- */
__declspec(dllexport) void Cerberus_BeginFrame(void) {}
__declspec(dllexport) void Cerberus_EndFrame(void) {}
__declspec(dllexport) void Cerberus_GameRoundEnd(void) {}
__declspec(dllexport) void Cerberus_GameRoundStart(void) {}
__declspec(dllexport) void Cerberus_PlayerDespawn(void) {}
__declspec(dllexport) void Cerberus_PlayerRevive(void) {}
__declspec(dllexport) void Cerberus_PlayerSpawn(void) {}
__declspec(dllexport) void Cerberus_PlayerTakeDamage(void) {}
__declspec(dllexport) void Cerberus_PlayerTick(void) {}
__declspec(dllexport) void Cerberus_PlayerUseWeapon(void) {}

/* ---- ClientAuth ---- */
__declspec(dllexport) int ClientAuth_ClientWriteChallengeResponse(void *a, void *b, void *c, void *d) { return 0; }
__declspec(dllexport) void ClientAuth_Destroy(void *handle) {}
__declspec(dllexport) int ClientAuth_Initialize(void *a, void *b) { return 1; }
__declspec(dllexport) void *CreateClientAuth(void) { return NULL; }

/* ---- CreateXxx factories ---- */
__declspec(dllexport) void *CreateGameClient(void) { return NULL; }
__declspec(dllexport) void *CreateGameLauncher(void) { return NULL; }
__declspec(dllexport) void *CreateHttpsClient(void) { return NULL; }
__declspec(dllexport) void *CreateThirdPartyLauncher(void) { return NULL; }

/* ---- GameClientP2P ---- */
__declspec(dllexport) int GameClientP2P_BeginSession(void *a, void *b) { return 1; }
__declspec(dllexport) void GameClientP2P_Cerberus(void *a, void *b) {}
__declspec(dllexport) void GameClientP2P_EndSession(void *handle) {}
__declspec(dllexport) int GameClientP2P_InitLocalization(void *a, void *b) { return 1; }
__declspec(dllexport) int GameClientP2P_PollForMessageToPeer(void *a, void *b, void *c, void *d) { return 0; }
__declspec(dllexport) int GameClientP2P_PollStatus(void *handle) { return 0; }
__declspec(dllexport) int GameClientP2P_ReceiveMessageFromPeer(void *a, void *b, void *c) { return 0; }
__declspec(dllexport) int GameClientP2P_RegisterPeer(void *a, void *b) { return 1; }
__declspec(dllexport) void GameClientP2P_ResetState(void *handle) {}
__declspec(dllexport) void GameClientP2P_SetLogCallback(void *callback) {}
__declspec(dllexport) void GameClientP2P_SetMaxAllowedMessageLength(void *a, uint32_t len) {}
__declspec(dllexport) void GameClientP2P_UnregisterPeer(void *a, void *b) {}
__declspec(dllexport) int GameClientP2P_UpdatePlatformUserAuthTicket(void *a, void *b, void *c) { return 1; }

/* ---- GameClient ---- */
__declspec(dllexport) void GameClient_ConnectionReset(void *handle) {}
__declspec(dllexport) void GameClient_Destroy(void *handle) {}
__declspec(dllexport) int GameClient_Initialize(void *a, void *b) { return 1; }
__declspec(dllexport) int GameClient_NetProtect(void *a, void *b) { return 1; }
__declspec(dllexport) int GameClient_PollStatus(void *handle) { return 0; }
__declspec(dllexport) int GameClient_PopNetworkMessage(void *a, void *b, void *c) { return 0; }
__declspec(dllexport) int GameClient_PushNetworkMessage(void *a, void *b, uint32_t len) { return 1; }
__declspec(dllexport) void GameClient_SetMaxAllowedMessageLength(void *a, uint32_t len) {}
__declspec(dllexport) int GameClient_ValidateServerHost(void *a, void *b) { return 1; }

/* ---- GameLauncher ---- */
__declspec(dllexport) void GameLauncher_Destroy(void *handle) {}
__declspec(dllexport) uint32_t GameLauncher_GetGameProcessId(void *handle) { return 0; }
__declspec(dllexport) int GameLauncher_OpenGameProcess(void *a, void *b) { return 1; }
__declspec(dllexport) int GameLauncher_StartGameA(void *a, const char *path) { return 1; }
__declspec(dllexport) int GameLauncher_StartGameW(void *a, const wchar_t *path) { return 1; }

/* ---- NetProtectClient ---- */
__declspec(dllexport) uint32_t NetProtectClient_GetProtectMessageOutputLength(uint32_t inputLen) { return inputLen; }
__declspec(dllexport) int NetProtectClient_ProtectMessage(void *a, void *b, uint32_t len, void *c, uint32_t *outLen) {
    if (outLen) *outLen = len;
    return 1;
}
__declspec(dllexport) int NetProtectClient_UnprotectMessage(void *a, void *b, uint32_t len, void *c, uint32_t *outLen) {
    if (outLen) *outLen = len;
    return 1;
}

/* ---- ThirdPartyLauncher ---- */
__declspec(dllexport) void ThirdPartyLauncher_Destroy(void *handle) {}
__declspec(dllexport) int ThirdPartyLauncher_Initialize(void *a, void *b) { return 1; }
__declspec(dllexport) void ThirdPartyLauncher_SetServer(void *a, void *b) {}
