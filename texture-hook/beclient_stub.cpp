/*
 * BEClient_x64.dll stub — replaces BattlEye + loads texture_hook.dll
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" {
    __declspec(dllexport) int GetVer() { return 1; }
    __declspec(dllexport) int Init(void* a, void* b, void* c, void* d) { return 1; }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadLibraryA("texture_hook.dll");
    }
    return TRUE;
}
