/*
 * BEClient_x64.dll stub
 * Exports: Init, GetVer
 * Created by MrPie — DLBB Revival Project
 */
#include <windows.h>

static const char CREDIT[] = "DLBB Revival — Created by MrPie";

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)CREDIT;
    return TRUE;
}

__declspec(dllexport) int Init(void *param1, void *param2, void *param3, void *param4) {
    return 1; /* success */
}

__declspec(dllexport) const char *GetVer(void) {
    return "1.0.0.0";
}
