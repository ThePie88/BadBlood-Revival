/*
 * LightFX.dll Proxy — forwards all calls + loads texture_hook.dll
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HMODULE g_real = nullptr;

static void LoadReal() {
    if (g_real) return;
    g_real = LoadLibraryA("LightFX_original.dll");
}

static void* GetOriginal(const char* name) {
    LoadReal();
    return g_real ? (void*)GetProcAddress(g_real, name) : nullptr;
}

// All LightFX functions return unsigned int, take various args
// Use variadic forwarding via naked thunks isn't possible in x64
// Just stub them all to return 0 (game doesn't need real LightFX)

extern "C" {
    __declspec(dllexport) unsigned int LFX_Initialize() { return 1; } // return "not available"
    __declspec(dllexport) unsigned int LFX_Release() { return 0; }
    __declspec(dllexport) unsigned int LFX_Reset() { return 0; }
    __declspec(dllexport) unsigned int LFX_Update() { return 0; }
    __declspec(dllexport) unsigned int LFX_UpdateDefault() { return 0; }
    __declspec(dllexport) unsigned int LFX_GetNumDevices(void* a) { return 0; }
    __declspec(dllexport) unsigned int LFX_GetDeviceDescription(unsigned int a, void* b, unsigned int c, unsigned char* d) { return 0; }
    __declspec(dllexport) unsigned int LFX_GetNumLights(unsigned int a, void* b) { return 0; }
    __declspec(dllexport) unsigned int LFX_GetLightColor(unsigned int a, unsigned int b, void* c) { return 0; }
    __declspec(dllexport) unsigned int LFX_GetLightDescription(unsigned int a, unsigned int b, void* c, unsigned int d) { return 0; }
    __declspec(dllexport) unsigned int LFX_GetLightLocation(unsigned int a, unsigned int b, void* c) { return 0; }
    __declspec(dllexport) unsigned int LFX_SetLightColor(unsigned int a, unsigned int b, void* c) { return 0; }
    __declspec(dllexport) unsigned int LFX_SetLightActionColor(unsigned int a, unsigned int b, unsigned int c, void* d) { return 0; }
    __declspec(dllexport) unsigned int LFX_ActionColor(unsigned int a, unsigned int b, unsigned int c, void* d) { return 0; }
    __declspec(dllexport) unsigned int LFX_ActionColorEx(unsigned int a, unsigned int b, unsigned int c, void* d, void* e) { return 0; }
    __declspec(dllexport) unsigned int LFX_SetTiming(int a) { return 0; }
    __declspec(dllexport) unsigned int LFX_Light(unsigned int a, unsigned int b, unsigned int c) { return 0; }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadLibraryA("texture_hook.dll");
    }
    return TRUE;
}
