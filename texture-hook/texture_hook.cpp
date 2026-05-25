/*
 * DLBB Texture Hook — hooks RCreateTexture2D in rd3d11
 * Replaces player_9 FPP textures with custom DDS from mods/textures/
 *
 * Key findings from IDA + Frida analysis:
 * - RCreateTexture2D at rd3d11+0x48840
 * - a2+0x04 = width, a2+0x08 = height
 * - a2+0x38 = texture name (char*)
 * - a2+0x40 = pixel data pointer
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <unordered_map>

// ============================================================
// CONFIGURATION — EDIT BEFORE BUILDING (if needed)
// ============================================================
//
// VPS_IP is where to redirect any leaked DNS / connect() that doesn't
// honour the hosts file. Defaults to 127.0.0.1 for local play. Change
// to your VPS public IP for internet hosting.
//
// REDIRECT_HOST is the original hostname the game tries to reach.
// Don't change it — it's what's compiled into the Techland engine.
//
// IMPORTANT: VPS_IP is also hardcoded as four ints below in Hooked_connect
// (search for `vpsIp = ...`). If you change VPS_IP, update that
// computation too. A future revision should derive both from one source.
// ------------------------------------------------------------

#define VPS_IP        "127.0.0.1"          // 127.0.0.1 = local play.
                                            //   Change to your VPS public IP
                                            //   if hosting publicly.
#define REDIRECT_HOST "pls.dlbb.com"       // DON'T change

static FILE* g_log = nullptr;

// DNS hook forward declarations
typedef int (WSAAPI *getaddrinfo_t)(const char*, const char*, const struct addrinfo*, struct addrinfo**);
static getaddrinfo_t g_origGetAddrInfo = nullptr;

// SSL bypass forward declarations
typedef BOOL (WINAPI *CertVerifyProc_t)(LPVOID, LPVOID, LPVOID, LPVOID);
static CertVerifyProc_t g_origCertVerify = nullptr;
static int g_callCount = 0;
static std::unordered_map<std::string, std::string> g_overrides;
static std::string g_modsPath;

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fflush(g_log);
}

// SSL bypass - always return TRUE (cert valid)
static BOOL WINAPI Hooked_CertVerify(LPVOID pszPolicyOID, LPVOID pChainContext, LPVOID pPolicyPara, LPVOID pPolicyStatus) {
    BOOL result = g_origCertVerify(pszPolicyOID, pChainContext, pPolicyPara, pPolicyStatus);
    if (pPolicyStatus) {
        *(DWORD*)pPolicyStatus = 0;  // dwError = 0 = cert OK
    }
    return TRUE;
}

// DNS hook implementation
static int WSAAPI Hooked_getaddrinfo(const char* node, const char* service,
    const struct addrinfo* hints, struct addrinfo** res)
{
    if (node && _stricmp(node, REDIRECT_HOST) == 0) {
        Log("[DNS] Redirecting %s -> %s\n", node, VPS_IP);
        return g_origGetAddrInfo(VPS_IP, service, hints, res);
    }
    return g_origGetAddrInfo(node, service, hints, res);
}

// connect() hook — redirect at socket level
typedef int (WSAAPI *connect_t)(SOCKET, const struct sockaddr*, int);
static connect_t g_origConnect = nullptr;

static int WSAAPI Hooked_connect(SOCKET s, const struct sockaddr* name, int namelen) {
    if (name && name->sa_family == AF_INET) {
        struct sockaddr_in* addr = (struct sockaddr_in*)name;
        unsigned long ip = ntohl(addr->sin_addr.s_addr);
        unsigned short port = ntohs(addr->sin_port);

        // Redirect Techland CloudFront IPs to our VPS
        // Log all port 443 connections for debugging
        if (port == 443) {
            Log("[CONNECT] %d.%d.%d.%d:%d\n",
                (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, ip&0xFF, port);

            // Replace any port-443 connection (except localhost) with our
            // server IP. NOTE: this duplicates VPS_IP as four ints — keep
            // in sync with the #define above. TODO: parse VPS_IP at DLL load.
            // 127.0.0.1 = (127 << 24) | (0 << 16) | (0 << 8) | 1
            unsigned long vpsIp = (127UL << 24) | (0UL << 16) | (0UL << 8) | 1UL;   // <<< MATCH VPS_IP ABOVE
            if (ip != vpsIp && ip != 0x7F000001) {  // not already target and not localhost
                Log("[CONNECT] REDIRECT -> %s:443\n", VPS_IP);
                addr->sin_addr.s_addr = htonl(vpsIp);
            }
        }
    }
    return g_origConnect(s, name, namelen);
}

// Wide version hook
typedef int (WSAAPI *GetAddrInfoW_t)(const wchar_t*, const wchar_t*, const ADDRINFOW*, ADDRINFOW**);
static GetAddrInfoW_t g_origGetAddrInfoW = nullptr;

static int WSAAPI Hooked_GetAddrInfoW(const wchar_t* node, const wchar_t* service,
    const ADDRINFOW* hints, ADDRINFOW** res)
{
    if (node && _wcsicmp(node, L"pls.dlbb.com") == 0) {
        Log("[DNS-W] Redirecting pls.dlbb.com -> %s\n", VPS_IP);
        // Wide-string version of VPS_IP. Edit to match the #define above.
        return g_origGetAddrInfoW(L"127.0.0.1", service, hints, res);
    }
    return g_origGetAddrInfoW(node, service, hints, res);
}

// gethostbyname hook
typedef struct hostent* (WSAAPI *gethostbyname_t)(const char*);
static gethostbyname_t g_origGethostbyname = nullptr;

static struct hostent* WSAAPI Hooked_gethostbyname(const char* name) {
    if (name && _stricmp(name, REDIRECT_HOST) == 0) {
        Log("[DNS-OLD] Redirecting %s -> %s\n", name, VPS_IP);
        return g_origGethostbyname(VPS_IP);
    }
    return g_origGethostbyname(name);
}

// ============================================================
// Hook
// ============================================================

static void* g_trampoline = nullptr;

static void* AllocNearby(void* target) {
    uintptr_t addr = (uintptr_t)target;
    for (uintptr_t t = addr - 0x10000; t > addr - 0x7FFF0000; t -= 0x10000) {
        void* p = VirtualAlloc((void*)t, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    for (uintptr_t t = addr + 0x10000; t < addr + 0x7FFF0000; t += 0x10000) {
        void* p = VirtualAlloc((void*)t, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    return nullptr;
}

// Original function: _QWORD* __fastcall RCreateTexture2D(__int64 a1, __int64 a2)
typedef void* (__fastcall *RCreateTexture2D_t)(void* a1, void* a2);
static RCreateTexture2D_t g_original = nullptr;

// DDS file loader - returns raw pixel data
static unsigned char* LoadDDSFile(const char* path, int* outSize) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char header[148];
    fread(header, 1, 4, f);
    if (memcmp(header, "DDS ", 4) != 0) { fclose(f); return nullptr; }

    // Read rest of header
    fread(header + 4, 1, 124, f);
    int hdr_size = 128;
    // Check DX10 extended header
    if (memcmp(header + 84, "DX10", 4) == 0) {
        fread(header + 128, 1, 20, f);
        hdr_size = 148;
    }

    long data_size = sz - hdr_size;
    unsigned char* data = (unsigned char*)malloc(data_size);
    fread(data, 1, data_size, f);
    fclose(f);
    *outSize = data_size;
    return data;
}

static void* __fastcall Hooked_RCreateTexture2D(void* a1, void* a2) {
    g_callCount++;

    char* texName = nullptr;
    try {
        // a2+0x38 = texture name
        texName = *(char**)((char*)a2 + 0x38);
    } catch (...) {}

    if (texName && !g_overrides.empty()) {
        // Check if this texture has an override
        // Strip .dds extension for matching
        std::string nameStr(texName);
        if (nameStr.size() > 4 && nameStr.substr(nameStr.size()-4) == ".dds")
            nameStr = nameStr.substr(0, nameStr.size()-4);
        auto it = g_overrides.find(nameStr);
        if (it != g_overrides.end()) {
            int w = *(int*)((char*)a2 + 4);
            int h = *(int*)((char*)a2 + 8);
            int fmt = *(int*)((char*)a2 + 20);

            // Log original data info
            void* origPixels = *(void**)((char*)a2 + 0x40);
            int origPitch = *(int*)((char*)a2 + 0x48);  // SysMemPitch might be nearby

            Log("[REPLACE] #%d \"%s\" %dx%d fmt=%d origData=%p\n",
                g_callCount, texName, w, h, fmt, origPixels);

            // Dump first 32 bytes of original pixel data
            if (origPixels) {
                unsigned char* ob = (unsigned char*)origPixels;
                Log("[ORIG_DATA] %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
                    ob[0],ob[1],ob[2],ob[3],ob[4],ob[5],ob[6],ob[7],
                    ob[8],ob[9],ob[10],ob[11],ob[12],ob[13],ob[14],ob[15]);
            }

            // Scan a2 for size/pitch info
            Log("[DESC] ");
            for (int off = 0; off <= 0x60; off += 4) {
                unsigned int val = *(unsigned int*)((char*)a2 + off);
                if (val > 0 && val < 0x10000000)
                    Log("+%02X=%u ", off, val);
            }
            Log("\n");

            // Read actual buffer size from descriptor
            int actualSize = *(int*)((char*)a2 + 0x5C);  // data_size field
            int pitch = *(int*)((char*)a2 + 0x58);        // pitch field
            Log("[REPLACE] fmt=%d actualSize=%d pitch=%d\n", fmt, actualSize, pitch);

            // Load custom DDS
            int dataSize = 0;
            unsigned char* customData = LoadDDSFile(it->second.c_str(), &dataSize);

            // Use the SMALLER of custom data size and actual buffer size
            int writeSize = (dataSize < actualSize) ? dataSize : actualSize;
            Log("[REPLACE] Custom: %d bytes, buffer: %d bytes, will write: %d\n", dataSize, actualSize, writeSize);

            if (writeSize <= 0 || !customData) {
                Log("[REPLACE] Skip - no data\n");
                if (customData) free(customData);
            } else {
                // Find the pixel buffer at a2+0x50
                void* bigBuffer = *(void**)((char*)a2 + 0x50);
                if (bigBuffer && (uintptr_t)bigBuffer > 0x10000) {
                    MEMORY_BASIC_INFORMATION mbi;
                    VirtualQuery(bigBuffer, &mbi, sizeof(mbi));
                    Log("[REPLACE] Buffer %p region=%zuKB prot=0x%X\n", bigBuffer, mbi.RegionSize/1024, mbi.Protect);

                    if (mbi.RegionSize >= (size_t)writeSize && mbi.State == MEM_COMMIT) {
                        memcpy(bigBuffer, customData, writeSize);
                        Log("[REPLACE] SUCCESS! Wrote %d bytes\n", writeSize);
                    } else {
                        Log("[REPLACE] Buffer too small (%zu < %d)\n", mbi.RegionSize, writeSize);
                    }
                } else {
                    Log("[REPLACE] Invalid buffer pointer: %p\n", bigBuffer);
                }
                free(customData);
            }
        }

        // Log player textures for debugging
        if (strstr(texName, "player_") || strstr(texName, "jacket") || strstr(texName, "glove")) {
            int w = *(int*)((char*)a2 + 4);
            int h = *(int*)((char*)a2 + 8);
            Log("[PLAYER] #%d \"%s\" %dx%d\n", g_callCount, texName, w, h);
        }
    }

    return g_original(a1, a2);
}

// ============================================================
// Init
// ============================================================

static void LoadOverrides() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string basePath(exePath);
    basePath = basePath.substr(0, basePath.find_last_of('\\'));
    g_modsPath = basePath + "\\mods\\textures\\";

    WIN32_FIND_DATAA fd;
    std::string searchPath = g_modsPath + "*.dds";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string filename(fd.cFileName);
            std::string texName = filename.substr(0, filename.length() - 4);
            std::string fullPath = g_modsPath + filename;
            g_overrides[texName] = fullPath;
            Log("[INIT] Override: %s\n", texName.c_str());
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    Log("[INIT] %zu overrides from %s\n", g_overrides.size(), g_modsPath.c_str());
}

// ============================================================
// Material DLC Hook — redirect ?(1) to our custom .mp
// ============================================================

// CFileHashMultiContainer::Open(this, path, flags, magic)
typedef bool (__fastcall *FHMCOpen_t)(void* thisPtr, const char* path, int flags, unsigned int magic);
static FHMCOpen_t g_origOpen = nullptr;
static void* g_openTrampoline = nullptr;
static std::string g_dlcMpPath; // path to our custom .mp

static bool __fastcall Hooked_FHMCOpen(void* thisPtr, const char* path, int flags, unsigned int magic) {
    // Check if this is a DLC path that would fail
    // ONLY redirect the main .mp, NOT the _refs.mp!
    if (path && strstr(path, "?(1)") && !strstr(path, "_refs")) {
        if (!g_dlcMpPath.empty()) {
            Log("[DLC_HOOK] Intercepted '%s' -> redirecting to '%s'\n", path, g_dlcMpPath.c_str());
            bool result = g_origOpen(thisPtr, g_dlcMpPath.c_str(), flags, magic);
            Log("[DLC_HOOK] Redirect result: %d\n", result);
            return result;
        }
    }
    // Log refs attempts but don't redirect
    if (path && strstr(path, "?(1)") && strstr(path, "_refs")) {
        Log("[DLC_HOOK] SKIP refs: '%s'\n", path);
    }
    return g_origOpen(thisPtr, path, flags, magic);
}

static void InstallOpenHook(HMODULE hFS) {
    // CFileHashMultiContainer::Open at RVA 0x6640
    void* func = (void*)((uintptr_t)hFS + 0x6640);
    unsigned char* p = (unsigned char*)func;
    Log("[DLC_HOOK] Open at %p: %02X %02X %02X %02X %02X %02X\n",
        func, p[0], p[1], p[2], p[3], p[4], p[5]);

    void* page = AllocNearby(func);
    if (!page) { Log("[DLC_HOOK] Cannot alloc near page\n"); return; }

    unsigned char* tramp = (unsigned char*)page;
    unsigned char* relay = tramp + 64;

    // Copy first bytes to trampoline (need at least 5 for JMP rel32)
    // Check if prologue is long enough
    // Common prologues: push rbx (1) + sub rsp,X (4) = 5 bytes minimum
    int prologueLen = 6; // safe default
    memcpy(tramp, func, prologueLen);
    tramp[prologueLen] = 0xFF; tramp[prologueLen + 1] = 0x25; *(int*)(tramp + prologueLen + 2) = 0;
    *(uintptr_t*)(tramp + prologueLen + 6) = (uintptr_t)func + prologueLen;

    g_openTrampoline = tramp;
    g_origOpen = (FHMCOpen_t)tramp;

    // Relay to our hook
    relay[0] = 0xFF; relay[1] = 0x25; *(int*)(relay + 2) = 0;
    *(uintptr_t*)(relay + 6) = (uintptr_t)Hooked_FHMCOpen;

    // Patch original
    DWORD old;
    VirtualProtect(func, prologueLen, PAGE_EXECUTE_READWRITE, &old);
    intptr_t rel = (intptr_t)relay - ((intptr_t)func + 5);
    p[0] = 0xE9;
    *(int*)(p + 1) = (int)rel;
    for (int i = 5; i < prologueLen; i++) p[i] = 0x90;
    VirtualProtect(func, prologueLen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), func, prologueLen);

    Log("[DLC_HOOK] Open hook installed!\n");
}

// ============================================================
// Material Name Redirect — redirect "pie" material names to "mural"
// ============================================================

// CMaterialMgr::LoadMaterial uses a lowercase name buffer (Source[260]) before hashing.
// We hook the CRC32 function (engine+0x7DCA20) which is called with the name.
// Simpler: hook at the point where LoadMaterial calls sub_1807DCA20 with the name.
//
// Actually simplest: hook CFileHashMultiContainer::Fetch in filesystem DLL.
// When Fetch is called on materials/strings with PIE hash, redirect to MURAL hash.

typedef bool (__fastcall *Fetch_t)(void* thisPtr, int containerIdx, unsigned int hash, void** outData, unsigned int* outSize);
static Fetch_t g_origFetch = nullptr;

// PIE -> MURAL hash mapping
struct HashRemap {
    unsigned int pie;
    unsigned int mural;
};

static HashRemap g_remaps[4];
static int g_numRemaps = 0;

static unsigned int GameCRC32(const char* name) {
    // Standard CRC32 with init 0x811C9DC5
    unsigned int crc = 0x811C9DC5;
    // Use zlib-compatible CRC32
    static unsigned int table[256] = {0};
    if (!table[1]) {
        for (int i = 0; i < 256; i++) {
            unsigned int c = i;
            for (int j = 0; j < 8; j++)
                c = (c & 1) ? (c >> 1) ^ 0xEDB88320 : c >> 1;
            table[i] = c;
        }
    }
    crc = crc ^ 0xFFFFFFFF;
    for (const char* p = name; *p; p++)
        crc = (crc >> 8) ^ table[(crc ^ (unsigned char)*p) & 0xFF];
    return crc ^ 0xFFFFFFFF;
}

static bool __fastcall Hooked_Fetch(void* thisPtr, int containerIdx, unsigned int hash, void** outData, unsigned int* outSize) {
    // Check if this hash is a PIE hash that should redirect to MURAL
    for (int i = 0; i < g_numRemaps; i++) {
        if (hash == g_remaps[i].pie) {
            Log("[REMAP] Fetch hash 0x%08X (pie) -> 0x%08X (mural) container=%d\n",
                hash, g_remaps[i].mural, containerIdx);
            return g_origFetch(thisPtr, containerIdx, g_remaps[i].mural, outData, outSize);
        }
    }
    return g_origFetch(thisPtr, containerIdx, hash, outData, outSize);
}

static void InstallFetchHook(HMODULE hFS) {
    // Fetch at RVA 0x5A30
    void* func = (void*)((uintptr_t)hFS + 0x5A30);
    unsigned char* p = (unsigned char*)func;
    Log("[REMAP] Fetch at %p: %02X %02X %02X %02X %02X %02X\n",
        func, p[0], p[1], p[2], p[3], p[4], p[5]);

    void* page = AllocNearby(func);
    if (!page) { Log("[REMAP] Cannot alloc near page\n"); return; }

    unsigned char* tramp = (unsigned char*)page;
    unsigned char* relay = tramp + 64;

    int prologueLen = 6;
    memcpy(tramp, func, prologueLen);
    tramp[prologueLen] = 0xFF; tramp[prologueLen + 1] = 0x25; *(int*)(tramp + prologueLen + 2) = 0;
    *(uintptr_t*)(tramp + prologueLen + 6) = (uintptr_t)func + prologueLen;

    g_origFetch = (Fetch_t)tramp;

    relay[0] = 0xFF; relay[1] = 0x25; *(int*)(relay + 2) = 0;
    *(uintptr_t*)(relay + 6) = (uintptr_t)Hooked_Fetch;

    DWORD old;
    VirtualProtect(func, prologueLen, PAGE_EXECUTE_READWRITE, &old);
    intptr_t rel = (intptr_t)relay - ((intptr_t)func + 5);
    p[0] = 0xE9;
    *(int*)(p + 1) = (int)rel;
    p[5] = 0x90;
    VirtualProtect(func, prologueLen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), func, prologueLen);

    Log("[REMAP] Fetch hook installed!\n");
}

static DWORD WINAPI HookThread(LPVOID) {
    // Wait for filesystem dll
    HMODULE hFS = nullptr;
    for (int i = 0; i < 300; i++) {
        hFS = GetModuleHandleA("filesystem_x64_rwdi.dll");
        if (hFS) break;
        Sleep(100);
    }
    if (hFS) {
        // Setup PIE -> MURAL remaps
        g_remaps[0] = {GameCRC32("player_9_jacket_pie_fpp.mat"), GameCRC32("player_9_jacket_mural_fpp.mat")};
        g_remaps[1] = {GameCRC32("player_9_jacket_pie.mat"), GameCRC32("player_9_jacket_mural.mat")};
        g_remaps[2] = {GameCRC32("player_9_pants_pie.mat"), GameCRC32("player_9_pants_mural.mat")};
        g_remaps[3] = {GameCRC32("player_9_shoes_pie.mat"), GameCRC32("player_9_shoes_mural.mat")};
        g_numRemaps = 4;

        Log("[REMAP] Hash remaps:\n");
        const char* names[] = {"jacket_fpp", "jacket_tpp", "pants", "shoes"};
        for (int i = 0; i < 4; i++)
            Log("[REMAP]   %s: 0x%08X -> 0x%08X\n", names[i], g_remaps[i].pie, g_remaps[i].mural);

        InstallFetchHook(hFS);
    }

    // Wait for rd3d11
    HMODULE hRd3d11 = nullptr;
    for (int i = 0; i < 600; i++) {
        hRd3d11 = GetModuleHandleA("rd3d11_x64_rwdi.dll");
        if (hRd3d11) break;
        Sleep(100);
    }
    if (!hRd3d11) { Log("[ERROR] rd3d11 not found\n"); return 1; }
    Log("[INIT] rd3d11 at %p\n", hRd3d11);

    // RCreateTexture2D at RVA 0x48840
    void* func = (void*)((uintptr_t)hRd3d11 + 0x48840);
    unsigned char* p = (unsigned char*)func;
    Log("[INIT] RCreateTexture2D at %p: %02X %02X %02X %02X %02X\n",
        func, p[0], p[1], p[2], p[3], p[4]);

    // Load overrides
    LoadOverrides();

    if (g_overrides.empty()) {
        Log("[INIT] No overrides, logging only\n");
    }

    // Install hook - prologue: 89 9E A0 00 00 00 (mov [rsi+0A0h], ebx) = 6 bytes
    // Actually check what's really there
    // Need at least 5 bytes for JMP rel32
    void* page = AllocNearby(func);
    if (!page) { Log("[ERROR] Cannot alloc near page\n"); return 1; }

    unsigned char* tramp = (unsigned char*)page;
    unsigned char* relay = tramp + 64;

    // Save first 6 bytes to trampoline
    memcpy(tramp, func, 6);
    // JMP back to func+6
    tramp[6] = 0xFF; tramp[7] = 0x25; *(int*)(tramp + 8) = 0;
    *(uintptr_t*)(tramp + 12) = (uintptr_t)func + 6;

    g_trampoline = tramp;
    g_original = (RCreateTexture2D_t)tramp;

    // Relay: JMP to our hook
    relay[0] = 0xFF; relay[1] = 0x25; *(int*)(relay + 2) = 0;
    *(uintptr_t*)(relay + 6) = (uintptr_t)Hooked_RCreateTexture2D;

    // Patch func: JMP rel32 to relay (5 bytes) + NOP (1 byte)
    DWORD old;
    VirtualProtect(func, 6, PAGE_EXECUTE_READWRITE, &old);
    intptr_t rel = (intptr_t)relay - ((intptr_t)func + 5);
    p[0] = 0xE9;
    *(int*)(p + 1) = (int)rel;
    p[5] = 0x90; // NOP to pad to 6 bytes
    VirtualProtect(func, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), func, 6);

    Log("[INIT] Texture hook installed! Waiting for textures...\n");

    // ============================================================
    // SSL Bypass
    // ============================================================

    HMODULE hCrypt32 = GetModuleHandleA("crypt32.dll");
    if (!hCrypt32) hCrypt32 = LoadLibraryA("crypt32.dll");
    if (hCrypt32) {
        void* pCertVerify = (void*)GetProcAddress(hCrypt32, "CertVerifyCertificateChainPolicy");
        if (pCertVerify) {
            void* sslPage = AllocNearby(pCertVerify);
            if (sslPage) {
                unsigned char* sslTramp = (unsigned char*)sslPage;
                unsigned char* sslRelay = sslTramp + 64;

                memcpy(sslTramp, pCertVerify, 7);
                sslTramp[7] = 0xFF; sslTramp[8] = 0x25; *(int*)(sslTramp + 9) = 0;
                *(uintptr_t*)(sslTramp + 13) = (uintptr_t)pCertVerify + 7;
                g_origCertVerify = (CertVerifyProc_t)sslTramp;

                sslRelay[0] = 0xFF; sslRelay[1] = 0x25; *(int*)(sslRelay + 2) = 0;
                *(uintptr_t*)(sslRelay + 6) = (uintptr_t)Hooked_CertVerify;

                DWORD sslOld;
                VirtualProtect(pCertVerify, 7, PAGE_EXECUTE_READWRITE, &sslOld);
                intptr_t sslRel = (intptr_t)sslRelay - ((intptr_t)pCertVerify + 5);
                ((unsigned char*)pCertVerify)[0] = 0xE9;
                *(int*)((unsigned char*)pCertVerify + 1) = (int)sslRel;
                ((unsigned char*)pCertVerify)[5] = 0x90;
                ((unsigned char*)pCertVerify)[6] = 0x90;
                VirtualProtect(pCertVerify, 7, sslOld, &sslOld);
                FlushInstructionCache(GetCurrentProcess(), pCertVerify, 7);
                Log("[SSL] CertVerify hooked - accepting all certs\n");
            }
        }
    }

    // ============================================================
    // DNS Hook — redirect pls.dlbb.com to VPS IP
    // ============================================================

    HMODULE hWs2 = GetModuleHandleA("ws2_32.dll");
    if (!hWs2) hWs2 = LoadLibraryA("ws2_32.dll");
    if (hWs2) {
        void* pGetAddrInfo = (void*)GetProcAddress(hWs2, "getaddrinfo");
        if (pGetAddrInfo) {
            void* dnsPage = AllocNearby(pGetAddrInfo);
            if (!dnsPage) { Log("[DNS] Cannot alloc page near ws2_32\n"); return 0; }
            unsigned char* dnsTramp = (unsigned char*)dnsPage;
            unsigned char* dnsRelay = dnsTramp + 64;

            // Prologue: 48 8B C4 (3) + 48 89 58 08 (4) = 7 bytes
            memcpy(dnsTramp, pGetAddrInfo, 7);
            dnsTramp[7] = 0xFF; dnsTramp[8] = 0x25; *(int*)(dnsTramp + 9) = 0;
            *(uintptr_t*)(dnsTramp + 13) = (uintptr_t)pGetAddrInfo + 7;

            g_origGetAddrInfo = (getaddrinfo_t)dnsTramp;

            dnsRelay[0] = 0xFF; dnsRelay[1] = 0x25; *(int*)(dnsRelay + 2) = 0;
            *(uintptr_t*)(dnsRelay + 6) = (uintptr_t)Hooked_getaddrinfo;

            DWORD dnsOld;
            VirtualProtect(pGetAddrInfo, 7, PAGE_EXECUTE_READWRITE, &dnsOld);
            intptr_t dnsRel = (intptr_t)dnsRelay - ((intptr_t)pGetAddrInfo + 5);
            ((unsigned char*)pGetAddrInfo)[0] = 0xE9;
            *(int*)((unsigned char*)pGetAddrInfo + 1) = (int)dnsRel;
            ((unsigned char*)pGetAddrInfo)[5] = 0x90; // NOP
            ((unsigned char*)pGetAddrInfo)[6] = 0x90; // NOP
            VirtualProtect(pGetAddrInfo, 7, dnsOld, &dnsOld);
            FlushInstructionCache(GetCurrentProcess(), pGetAddrInfo, 7);

            Log("[DNS] getaddrinfo hooked! pls.dlbb.com -> %s\n", VPS_IP);
        }

        // Hook GetAddrInfoW
        void* pGetAddrInfoW = (void*)GetProcAddress(hWs2, "GetAddrInfoW");
        if (pGetAddrInfoW) {
            void* dnsPageW = AllocNearby(pGetAddrInfoW);
            if (dnsPageW) {
                unsigned char* t = (unsigned char*)dnsPageW;
                unsigned char* r = t + 64;
                memcpy(t, pGetAddrInfoW, 7);
                t[7] = 0xFF; t[8] = 0x25; *(int*)(t+9) = 0;
                *(uintptr_t*)(t+13) = (uintptr_t)pGetAddrInfoW + 7;
                g_origGetAddrInfoW = (GetAddrInfoW_t)t;
                r[0] = 0xFF; r[1] = 0x25; *(int*)(r+2) = 0;
                *(uintptr_t*)(r+6) = (uintptr_t)Hooked_GetAddrInfoW;
                DWORD o; VirtualProtect(pGetAddrInfoW, 7, PAGE_EXECUTE_READWRITE, &o);
                ((unsigned char*)pGetAddrInfoW)[0] = 0xE9;
                *(int*)((unsigned char*)pGetAddrInfoW+1) = (int)((intptr_t)r - ((intptr_t)pGetAddrInfoW+5));
                ((unsigned char*)pGetAddrInfoW)[5] = 0x90;
                ((unsigned char*)pGetAddrInfoW)[6] = 0x90;
                VirtualProtect(pGetAddrInfoW, 7, o, &o);
                Log("[DNS] GetAddrInfoW hooked!\n");
            }
        }

        // Hook gethostbyname
        void* pGethostbyname = (void*)GetProcAddress(hWs2, "gethostbyname");
        if (pGethostbyname) {
            void* dnsPageH = AllocNearby(pGethostbyname);
            if (dnsPageH) {
                unsigned char* t = (unsigned char*)dnsPageH;
                unsigned char* r = t + 64;
                memcpy(t, pGethostbyname, 7);
                t[7] = 0xFF; t[8] = 0x25; *(int*)(t+9) = 0;
                *(uintptr_t*)(t+13) = (uintptr_t)pGethostbyname + 7;
                g_origGethostbyname = (gethostbyname_t)t;
                r[0] = 0xFF; r[1] = 0x25; *(int*)(r+2) = 0;
                *(uintptr_t*)(r+6) = (uintptr_t)Hooked_gethostbyname;
                DWORD o; VirtualProtect(pGethostbyname, 7, PAGE_EXECUTE_READWRITE, &o);
                ((unsigned char*)pGethostbyname)[0] = 0xE9;
                *(int*)((unsigned char*)pGethostbyname+1) = (int)((intptr_t)r - ((intptr_t)pGethostbyname+5));
                ((unsigned char*)pGethostbyname)[5] = 0x90;
                ((unsigned char*)pGethostbyname)[6] = 0x90;
                VirtualProtect(pGethostbyname, 7, o, &o);
                Log("[DNS] gethostbyname hooked!\n");
            }
        }

        // connect() hook removed — crashes tobii_gameintegration_x64.dll
        // Solution: local stunnel on client, started by launcher
    }

    return 0;
}

// ============================================================
// Lobby Join (PIE friend list integration)
//
// On startup we parse the command line for "--pie-join <lobby_id>".
// If found, we wait until steam_api64.dll is loaded and SteamAPI_Init has run,
// then call SteamAPI_ISteamMatchmaking_JoinLobby(lobby_id) directly.
//
// The lobby_id is the CSteamID of the lobby host (= the host's session_id from
// the launcher login). This works because Goldberg uses the host CSteamID as
// the lobby identifier in P2P mode.
// ============================================================

typedef uint64_t CSteamID_t;
typedef void* (__cdecl *SteamAPI_Init_t)();
typedef void* (__cdecl *SteamAPI_SteamMatchmaking_v009_t)();
typedef uint64_t (__cdecl *SteamAPI_ISteamMatchmaking_JoinLobby_t)(void* iface, CSteamID_t lobbyId);

static CSteamID_t g_pendingJoinLobby = 0;

static CSteamID_t ParsePieJoinArg() {
    LPSTR cmd = GetCommandLineA();
    if (!cmd) return 0;
    const char* needle = "--pie-join";
    const char* p = strstr(cmd, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == '=') p++;
    // Read uint64
    CSteamID_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    return v;
}

static DWORD WINAPI LobbyJoinThread(LPVOID) {
    Log("[LOBBY] Pending join: %llu — waiting for steam_api64.dll\n",
        (unsigned long long)g_pendingJoinLobby);

    // Wait for steam_api64.dll to be loaded
    HMODULE hSteam = NULL;
    for (int i = 0; i < 600; i++) {  // up to 60s
        hSteam = GetModuleHandleA("steam_api64.dll");
        if (hSteam) break;
        Sleep(100);
    }
    if (!hSteam) {
        Log("[LOBBY] steam_api64.dll never loaded — abort\n");
        return 1;
    }
    Log("[LOBBY] steam_api64.dll at %p\n", hSteam);

    // Resolve required Goldberg exports
    auto pSteamMatchmaking_v009 = (void*(__cdecl*)())GetProcAddress(hSteam, "SteamAPI_SteamMatchmaking_v009");
    auto pJoinLobby = (uint64_t(__cdecl*)(void*, CSteamID_t))GetProcAddress(hSteam, "SteamAPI_ISteamMatchmaking_JoinLobby");

    if (!pSteamMatchmaking_v009 || !pJoinLobby) {
        Log("[LOBBY] Missing exports: v009=%p Join=%p\n", pSteamMatchmaking_v009, pJoinLobby);
        return 1;
    }

    // Wait until SteamAPI_Init has been called by the game.
    // We can detect this by polling SteamMatchmaking() — it returns NULL until init.
    void* matchmaking = NULL;
    for (int i = 0; i < 600; i++) {
        matchmaking = pSteamMatchmaking_v009();
        if (matchmaking) break;
        Sleep(200);
    }
    if (!matchmaking) {
        Log("[LOBBY] SteamMatchmaking() never returned — abort\n");
        return 1;
    }
    Log("[LOBBY] ISteamMatchmaking at %p\n", matchmaking);

    // Give the game a few extra seconds to fully boot up the Steam state
    Sleep(3000);

    Log("[LOBBY] Calling JoinLobby(%llu)...\n", (unsigned long long)g_pendingJoinLobby);
    uint64_t result = pJoinLobby(matchmaking, g_pendingJoinLobby);
    Log("[LOBBY] JoinLobby returned: %llu\n", (unsigned long long)result);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        char* sl = strrchr(path, '\\');
        if (sl) *(sl + 1) = 0;
        strcat(path, "texture_hook.log");
        g_log = fopen(path, "w");
        Log("[INIT] Texture Hook DLL v2 - RCreateTexture2D hook\n");

        // Check for --pie-join arg from launcher (lobby invite accept)
        g_pendingJoinLobby = ParsePieJoinArg();
        if (g_pendingJoinLobby) {
            Log("[INIT] --pie-join detected: %llu\n", (unsigned long long)g_pendingJoinLobby);
            CreateThread(NULL, 0, LobbyJoinThread, NULL, 0, NULL);
        }

        CreateThread(NULL, 0, HookThread, NULL, 0, NULL);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        Log("[END] Total calls: %d\n", g_callCount);
        if (g_log) fclose(g_log);
    }
    return TRUE;
}
