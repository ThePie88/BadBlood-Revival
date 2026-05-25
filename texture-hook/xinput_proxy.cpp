/*
 * XINPUT1_3.dll Proxy — loads real XInput + texture_hook.dll
 * Build: g++ -shared -o XINPUT1_3.dll xinput_proxy.cpp -static -std=c++17
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

// Forward declarations for XInput functions
typedef DWORD (WINAPI *XInputGetState_t)(DWORD, void*);
typedef DWORD (WINAPI *XInputSetState_t)(DWORD, void*);
typedef DWORD (WINAPI *XInputGetCapabilities_t)(DWORD, DWORD, void*);
typedef void  (WINAPI *XInputEnable_t)(BOOL);
typedef DWORD (WINAPI *XInputGetDSoundAudioDeviceGuids_t)(DWORD, void*, void*);
typedef DWORD (WINAPI *XInputGetBatteryInformation_t)(DWORD, BYTE, void*);
typedef DWORD (WINAPI *XInputGetKeystroke_t)(DWORD, DWORD, void*);

static HMODULE g_real = nullptr;
static XInputGetState_t  pGetState = nullptr;
static XInputSetState_t  pSetState = nullptr;
static XInputGetCapabilities_t pGetCaps = nullptr;
static XInputEnable_t    pEnable = nullptr;
static XInputGetDSoundAudioDeviceGuids_t pGetDSound = nullptr;
static XInputGetBatteryInformation_t pGetBattery = nullptr;
static XInputGetKeystroke_t pGetKeystroke = nullptr;

static void LoadReal() {
    if (g_real) return;
    // Load real XInput from system directory
    char sysdir[MAX_PATH];
    GetSystemDirectoryA(sysdir, MAX_PATH);
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "%s\\XINPUT1_3.dll", sysdir);
    g_real = LoadLibraryA(path);
    if (!g_real) {
        // Try xinput1_4 or xinput9_1_0
        snprintf(path, MAX_PATH, "%s\\XINPUT1_4.dll", sysdir);
        g_real = LoadLibraryA(path);
    }
    if (g_real) {
        pGetState = (XInputGetState_t)GetProcAddress(g_real, "XInputGetState");
        pSetState = (XInputSetState_t)GetProcAddress(g_real, "XInputSetState");
        pGetCaps = (XInputGetCapabilities_t)GetProcAddress(g_real, "XInputGetCapabilities");
        pEnable = (XInputEnable_t)GetProcAddress(g_real, "XInputEnable");
        pGetDSound = (XInputGetDSoundAudioDeviceGuids_t)GetProcAddress(g_real, "XInputGetDSoundAudioDeviceGuids");
        pGetBattery = (XInputGetBatteryInformation_t)GetProcAddress(g_real, "XInputGetBatteryInformation");
        pGetKeystroke = (XInputGetKeystroke_t)GetProcAddress(g_real, "XInputGetKeystroke");
    }
}

extern "C" {
    __declspec(dllexport) DWORD WINAPI XInputGetState(DWORD idx, void* state) {
        LoadReal();
        return pGetState ? pGetState(idx, state) : ERROR_DEVICE_NOT_CONNECTED;
    }
    __declspec(dllexport) DWORD WINAPI XInputSetState(DWORD idx, void* vib) {
        LoadReal();
        return pSetState ? pSetState(idx, vib) : ERROR_DEVICE_NOT_CONNECTED;
    }
    __declspec(dllexport) DWORD WINAPI XInputGetCapabilities(DWORD idx, DWORD flags, void* caps) {
        LoadReal();
        return pGetCaps ? pGetCaps(idx, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
    }
    __declspec(dllexport) void WINAPI XInputEnable(BOOL enable) {
        LoadReal();
        if (pEnable) pEnable(enable);
    }
    __declspec(dllexport) DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD idx, void* a, void* b) {
        LoadReal();
        return pGetDSound ? pGetDSound(idx, a, b) : ERROR_DEVICE_NOT_CONNECTED;
    }
    __declspec(dllexport) DWORD WINAPI XInputGetBatteryInformation(DWORD idx, BYTE type, void* info) {
        LoadReal();
        return pGetBattery ? pGetBattery(idx, type, info) : ERROR_DEVICE_NOT_CONNECTED;
    }
    __declspec(dllexport) DWORD WINAPI XInputGetKeystroke(DWORD idx, DWORD rsv, void* ks) {
        LoadReal();
        return pGetKeystroke ? pGetKeystroke(idx, rsv, ks) : ERROR_DEVICE_NOT_CONNECTED;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // Load our texture hook DLL
        LoadLibraryA("texture_hook.dll");
    }
    else if (reason == DLL_PROCESS_DETACH) {
        if (g_real) FreeLibrary(g_real);
    }
    return TRUE;
}
