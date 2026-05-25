/*
 * discord-rpc.dll Proxy — loads real discord-rpc + texture_hook.dll
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HMODULE g_real = nullptr;

static void LoadReal() {
    if (g_real) return;
    g_real = LoadLibraryA("discord-rpc_original.dll");
}

#define PROXY(name) \
    static decltype(&name) p_##name = nullptr; \
    extern "C" __declspec(dllexport) void name() { \
        LoadReal(); \
        if (!p_##name) p_##name = (decltype(&name))GetProcAddress(g_real, #name); \
        if (p_##name) p_##name(); \
    }

// Can't use the macro easily since functions have args. Just forward everything:
typedef void (*VoidFunc)();
static VoidFunc GetFunc(const char* name) {
    LoadReal();
    return (VoidFunc)GetProcAddress(g_real, name);
}

extern "C" {
    __declspec(dllexport) void Discord_ClearPresence() { auto f = GetFunc("Discord_ClearPresence"); if(f) f(); }
    __declspec(dllexport) void Discord_Initialize() { auto f = GetFunc("Discord_Initialize"); if(f) f(); }
    __declspec(dllexport) void Discord_Register() { auto f = GetFunc("Discord_Register"); if(f) f(); }
    __declspec(dllexport) void Discord_RegisterSteamGame() { auto f = GetFunc("Discord_RegisterSteamGame"); if(f) f(); }
    __declspec(dllexport) void Discord_Respond() { auto f = GetFunc("Discord_Respond"); if(f) f(); }
    __declspec(dllexport) void Discord_RunCallbacks() { auto f = GetFunc("Discord_RunCallbacks"); if(f) f(); }
    __declspec(dllexport) void Discord_Shutdown() { auto f = GetFunc("Discord_Shutdown"); if(f) f(); }
    __declspec(dllexport) void Discord_UpdateHandlers() { auto f = GetFunc("Discord_UpdateHandlers"); if(f) f(); }
    __declspec(dllexport) void Discord_UpdatePresence() { auto f = GetFunc("Discord_UpdatePresence"); if(f) f(); }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadLibraryA("texture_hook.dll");
    }
    else if (reason == DLL_PROCESS_DETACH) {
        if (g_real) FreeLibrary(g_real);
    }
    return TRUE;
}
