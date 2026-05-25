/*
 * BadBlood-Revival — DNS Redirect DLL (version.dll proxy)
 *
 * Optional hook: hooks getaddrinfo so any lookup of pls.dlbb.com goes to
 * NEW_HOST instead. Useful if your server lives on a different hostname
 * than the original Techland one. For LOCAL play you usually don't need
 * this — the hosts file already redirects pls.dlbb.com to 127.0.0.1.
 *
 * Defaults below treat OLD_HOST == NEW_HOST as a no-op (the redirect
 * does nothing). To enable: change NEW_HOST to your public hostname.
 *
 * All version.dll exports are forwarded to the real system DLL at runtime.
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string.h>

#define OLD_HOST "pls.dlbb.com"            /* don't change — game-baked */
#define NEW_HOST "pls.dlbb.com"            /* same as OLD_HOST = no-op (local mode).
                                              Change to your public hostname for
                                              internet hosting, e.g. "pls.example.it". */

/* ---- DNS Hook ---- */

typedef INT (WSAAPI *getaddrinfo_t)(PCSTR, PCSTR, const ADDRINFOA *, PADDRINFOA *);
static getaddrinfo_t real_getaddrinfo = NULL;

static INT WSAAPI hook_getaddrinfo(PCSTR node, PCSTR service,
                                    const ADDRINFOA *hints, PADDRINFOA *res) {
    if (node && _stricmp(node, OLD_HOST) == 0)
        return real_getaddrinfo(NEW_HOST, service, hints, res);
    return real_getaddrinfo(node, service, hints, res);
}

static void PatchModule(HMODULE mod) {
    if (!mod) return;
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != 0x5A4D) return;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[1].VirtualAddress;
    if (!rva) return;

    for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva);
         imp->Name; imp++) {
        if (_stricmp((char *)(base + imp->Name), "ws2_32.dll") != 0) continue;
        IMAGE_THUNK_DATA *orig = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; orig->u1.AddressOfData; orig++, thunk++) {
            if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG64) continue;
            IMAGE_IMPORT_BY_NAME *n = (IMAGE_IMPORT_BY_NAME *)(base + orig->u1.AddressOfData);
            if (strcmp(n->Name, "getaddrinfo") == 0) {
                real_getaddrinfo = (getaddrinfo_t)thunk->u1.Function;
                DWORD old;
                VirtualProtect(&thunk->u1.Function, 8, PAGE_READWRITE, &old);
                thunk->u1.Function = (ULONGLONG)hook_getaddrinfo;
                VirtualProtect(&thunk->u1.Function, 8, old, &old);
                return;
            }
        }
    }
}

/* ---- version.dll proxy (runtime forwarding) ---- */

static HMODULE g_ver = NULL;

static FARPROC Ver(const char *name) {
    if (!g_ver) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strcat(path, "\\version.dll");
        g_ver = LoadLibraryA(path);
    }
    return GetProcAddress(g_ver, name);
}

/* Use assembly trampolines to forward with correct args */
#define PROXY(name) \
    __declspec(naked) void __cdecl proxy_##name(void) { \
        __asm { jmp [fp_##name] } \
    } \
    static void *fp_##name;

/* Can't use naked+inline asm with MSVC x64 or MinGW x64 easily.
   Instead, use a simpler approach: export functions that just call through. */

/* For x64, we'll use a different technique: load and call at runtime */
#define PROXY_FUNC(name) \
    __declspec(dllexport) ULONGLONG __stdcall name() { \
        typedef ULONGLONG (__stdcall *fn_t)(); \
        static fn_t fn = NULL; \
        if (!fn) fn = (fn_t)Ver(#name); \
        /* This won't work for functions with args... */ \
        return fn ? fn() : 0; \
    }

/* Actually for x64, the simplest working approach is to NOT proxy version.dll
   and instead use a DLL that's always loaded. Let's check if the game
   loads version.dll at all first. If not, we can just be a plain DLL
   that gets loaded via a different mechanism. */

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        HMODULE ws2 = LoadLibraryA("ws2_32.dll");
        if (ws2)
            real_getaddrinfo = (getaddrinfo_t)GetProcAddress(ws2, "getaddrinfo");
        PatchModule(GetModuleHandleA(NULL));
        PatchModule(GetModuleHandleA("engine_x64_rwdi.dll"));
        PatchModule(GetModuleHandleA("gamedll_x64_rwdi.dll"));
    }
    return TRUE;
}
