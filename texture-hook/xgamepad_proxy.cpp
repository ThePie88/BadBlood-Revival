/*
 * XGamepad.dll Proxy — loads real XGamepad + texture_hook.dll
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

static HMODULE g_real = nullptr;

typedef void* (*GenericFunc)();
static GenericFunc pGetGamepads = nullptr;
static GenericFunc pInitialize = nullptr;
static GenericFunc pPutInVector = nullptr;
static GenericFunc pRelease = nullptr;
static GenericFunc pTerminate = nullptr;

static void LoadReal() {
    if (g_real) return;
    g_real = LoadLibraryA("XGamepad_original.dll");
    if (g_real) {
        pGetGamepads = (GenericFunc)GetProcAddress(g_real, "GetGamepads");
        pInitialize  = (GenericFunc)GetProcAddress(g_real, "InitializeXGamepad");
        pPutInVector = (GenericFunc)GetProcAddress(g_real, "PutGamepadsInVector");
        pRelease     = (GenericFunc)GetProcAddress(g_real, "ReleaseGamepads");
        pTerminate   = (GenericFunc)GetProcAddress(g_real, "TerminateXGamepad");
    }
}

extern "C" {
    __declspec(dllexport) void* GetGamepads() {
        LoadReal(); return pGetGamepads ? pGetGamepads() : nullptr;
    }
    __declspec(dllexport) void* InitializeXGamepad() {
        LoadReal(); return pInitialize ? pInitialize() : nullptr;
    }
    __declspec(dllexport) void* PutGamepadsInVector() {
        LoadReal(); return pPutInVector ? pPutInVector() : nullptr;
    }
    __declspec(dllexport) void* ReleaseGamepads() {
        LoadReal(); return pRelease ? pRelease() : nullptr;
    }
    __declspec(dllexport) void* TerminateXGamepad() {
        LoadReal(); return pTerminate ? pTerminate() : nullptr;
    }
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
