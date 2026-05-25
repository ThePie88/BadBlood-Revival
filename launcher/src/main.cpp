/*
 * PIE-LAUNCHER — Dying Light: Bad Blood Community Edition
 * ImGui + D3D11 launcher with backend API integration
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#include <d3d11.h>
#include <shlobj.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include <cmath>

#include "../lib/imgui/imgui.h"
#include "../lib/imgui/backends/imgui_impl_win32.h"
#include "../lib/imgui/backends/imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================
// CONFIGURATION — EDIT THESE BEFORE BUILDING (if needed)
// ============================================================
//
// Defaults are tuned for LOCAL PLAY (server on the same PC). If you're
// hosting publicly on a VPS with your own domain, change SERVER_HOST and
// VPS_IP — see the comments next to each.
//
// These values are compiled into the .exe. If you change them, rebuild
// the launcher (`build.bat`). A future version will load them at runtime
// from launcher.cfg.
//
// IMPORTANT — keep GAME_HOST exactly 12 chars. It's the hostname baked
// into the engine binary; the patcher only replaces it if you used
// `--server-host`. For local play it stays as `pls.dlbb.com` and the
// hosts file routes it to 127.0.0.1 (stunnel listens there).
// ------------------------------------------------------------

// --- LOCAL DEFAULTS ---------------------------------------------------------
// The launcher talks to a server on the same machine over HTTP. Stunnel on
// 127.0.0.1:443 (set up by setup-local.bat) terminates TLS for the GAME
// (not the launcher). The launcher itself uses plain HTTP on :80.

#define SERVER_HOST     "127.0.0.1"        // launcher backend host. Change to
                                            //   your domain (12 chars or your
                                            //   own length, doesn't matter
                                            //   here — only GAME_HOST has the
                                            //   12-char constraint) when
                                            //   hosting publicly.
#define SERVER_PORT     80                 // launcher API port
#define VPS_IP          "127.0.0.1"        // IP written into
                                            //   steam_settings/custom_broadcasts.txt
                                            //   for Goldberg matchmaking relay.
                                            //   Use 127.0.0.1 for solo / single-PC,
                                            //   a LAN IP (e.g. 192.168.1.50)
                                            //   for LAN play, or your VPS public
                                            //   IP for internet play.

#define GAME_HOST       "pls.dlbb.com"     // DON'T change — original Techland
                                            //   hostname compiled into the engine.
                                            //   Always routed via hosts file.

#define HOSTS_FILE      "C:\\Windows\\System32\\drivers\\etc\\hosts"
#define HOSTS_ENTRY     "\n127.0.0.1 pls.dlbb.com  # BadBlood-Revival\n"

// Launcher metadata — usually no need to change ------------------------------

#define CACHE_FILE      "pie_launcher.dat"
#define LAUNCHER_VER    "1.1.0"
#define VERSION_FILE    "pie_version.txt"
#define PATCH_DIR       "PATCH-PIE"        // local folder where downloaded
                                            //   client patch is extracted

// ============================================================
// Logo texture loading (PNG via stb_image)
// ============================================================

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../lib/stb_image.h"

static ID3D11ShaderResourceView* g_logoSRV = nullptr;
static int g_logoW = 0, g_logoH = 0;
static float g_startTime = 0;

static bool LoadLogoTexture(ID3D11Device* device, const char* path) {
    int w, h, ch;
    unsigned char* data = stbi_load(path, &w, &h, &ch, 4);
    if (!data) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w; desc.Height = h;
    desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = data;
    sub.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    device->CreateTexture2D(&desc, &sub, &tex);
    stbi_image_free(data);
    if (!tex) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(tex, &srvDesc, &g_logoSRV);
    tex->Release();
    g_logoW = w; g_logoH = h;
    return g_logoSRV != nullptr;
}

static float GetTime() {
    return (float)(GetTickCount() - (DWORD)(g_startTime * 1000.0f)) / 1000.0f;
}

// ============================================================
// Hosts file management (add on start, remove on exit)
// ============================================================

static bool g_hostsModified = false;

static void AddHostsEntry() {
    // Check if entry already exists
    FILE* f = fopen(HOSTS_FILE, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, GAME_HOST) && strstr(line, VPS_IP) && line[0] != '#') {
                fclose(f);
                g_hostsModified = true;
                return; // Already there
            }
        }
        fclose(f);
    }

    f = fopen(HOSTS_FILE, "a");
    if (f) {
        fprintf(f, "%s", HOSTS_ENTRY);
        fclose(f);
        g_hostsModified = true;
    }
}

static void RemoveHostsEntry() {
    if (!g_hostsModified) return;

    FILE* f = fopen(HOSTS_FILE, "r");
    if (!f) return;

    std::string content;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Skip our entry
        if (strstr(line, "# PIE-LAUNCHER"))
            continue;
        content += line;
    }
    fclose(f);

    f = fopen(HOSTS_FILE, "w");
    if (f) {
        fwrite(content.c_str(), 1, content.size(), f);
        fclose(f);
    }
    g_hostsModified = false;
}

// ============================================================
// Stunnel management
// ============================================================

static PROCESS_INFORMATION g_stunnelPI = {};

static void StartStunnel(const char* gamePath) {
    system("taskkill /f /im tstunnel.exe >nul 2>&1");
    Sleep(300);

    std::string stunnel = std::string(gamePath) + "\\tstunnel.exe";
    std::string conf = std::string(gamePath) + "\\stunnel_client.conf";

    // Check if stunnel exists
    if (GetFileAttributesA(stunnel.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (GetFileAttributesA(conf.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    std::string cmd = "\"" + stunnel + "\" \"" + conf + "\"";
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, gamePath, &si, &g_stunnelPI);
    Sleep(500);
}

static void StopStunnel() {
    if (g_stunnelPI.hProcess) {
        TerminateProcess(g_stunnelPI.hProcess, 0);
        CloseHandle(g_stunnelPI.hProcess);
        CloseHandle(g_stunnelPI.hThread);
        g_stunnelPI = {};
    }
    system("taskkill /f /im tstunnel.exe >nul 2>&1");
}

// ============================================================
// Persistent cache
// ============================================================

struct LauncherCache {
    char magic[4];          // "PIE\0"
    bool eulaAccepted;
    char gamePath[512];
    char lastUsername[64];
    char serverHost[128];
};

static LauncherCache g_cache = {};

static std::string GetLauncherDir() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string s(path);
    return s.substr(0, s.find_last_of('\\') + 1);
}

static void LoadCache() {
    std::string path = GetLauncherDir() + CACHE_FILE;
    FILE* f = fopen(path.c_str(), "rb");
    if (f) {
        fread(&g_cache, sizeof(g_cache), 1, f);
        fclose(f);
        if (memcmp(g_cache.magic, "PIE", 3) != 0) {
            memset(&g_cache, 0, sizeof(g_cache));
        }
        if (!g_cache.serverHost[0]) {
            strncpy(g_cache.serverHost, SERVER_HOST, sizeof(g_cache.serverHost)-1);
        }
    }
}

static void SaveCache() {
    memcpy(g_cache.magic, "PIE", 4);
    std::string path = GetLauncherDir() + CACHE_FILE;
    FILE* f = fopen(path.c_str(), "wb");
    if (f) {
        fwrite(&g_cache, sizeof(g_cache), 1, f);
        fclose(f);
    }
}

// ============================================================
// HTTP API (WinInet, supports HTTPS with self-signed certs)
// ============================================================

static std::string HttpPost(const char* endpoint, const char* jsonBody) {
    std::string result;
    HINTERNET hInternet = InternetOpenA("PIE-Launcher/" LAUNCHER_VER, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return "{\"error\":\"No internet\"}";

    // Parse server URL
    HINTERNET hConnect = InternetConnectA(hInternet, SERVER_HOST, SERVER_PORT,
        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) { InternetCloseHandle(hInternet); return "{\"error\":\"Cannot connect\"}"; }

    DWORD flags = INTERNET_FLAG_NO_CACHE_WRITE;

    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", endpoint, NULL, NULL, NULL, flags, 0);
    if (!hRequest) { InternetCloseHandle(hConnect); InternetCloseHandle(hInternet); return "{\"error\":\"Request failed\"}"; }

    const char* headers = "Content-Type: application/json\r\n";
    BOOL sent = HttpSendRequestA(hRequest, headers, -1, (LPVOID)jsonBody, strlen(jsonBody));

    if (sent) {
        char buf[4096];
        DWORD bytesRead;
        while (InternetReadFile(hRequest, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
            buf[bytesRead] = 0;
            result += buf;
        }
    } else {
        result = "{\"error\":\"Send failed\"}";
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return result;
}

// HTTP request with optional method, body, and Bearer token
// method: "GET" or "POST"
static std::string HttpRequest(const char* method, const char* endpoint,
                                const char* jsonBody, const char* token) {
    std::string result;
    HINTERNET hInternet = InternetOpenA("PIE-Launcher/" LAUNCHER_VER, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return "{\"error\":\"No internet\"}";

    HINTERNET hConnect = InternetConnectA(hInternet, SERVER_HOST, SERVER_PORT,
        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) { InternetCloseHandle(hInternet); return "{\"error\":\"Cannot connect\"}"; }

    DWORD flags = INTERNET_FLAG_NO_CACHE_WRITE;
    HINTERNET hRequest = HttpOpenRequestA(hConnect, method, endpoint, NULL, NULL, NULL, flags, 0);
    if (!hRequest) { InternetCloseHandle(hConnect); InternetCloseHandle(hInternet); return "{\"error\":\"Request failed\"}"; }

    char hdr[256];
    if (token && token[0]) {
        snprintf(hdr, sizeof(hdr), "Content-Type: application/json\r\nPLS-Authorization: Token %s\r\n", token);
    } else {
        snprintf(hdr, sizeof(hdr), "Content-Type: application/json\r\n");
    }

    BOOL sent;
    if (jsonBody && jsonBody[0]) {
        sent = HttpSendRequestA(hRequest, hdr, -1, (LPVOID)jsonBody, strlen(jsonBody));
    } else {
        sent = HttpSendRequestA(hRequest, hdr, -1, NULL, 0);
    }

    if (sent) {
        char buf[4096];
        DWORD bytesRead;
        while (InternetReadFile(hRequest, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
            buf[bytesRead] = 0;
            result += buf;
        }
    } else {
        result = "{\"error\":\"Send failed\"}";
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return result;
}

// Simple JSON value extractor (no library needed)
static std::string JsonGet(const std::string& json, const char* key) {
    std::string search = std::string("\"") + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) pos++;
    size_t end = pos;
    bool inQuote = (pos > 0 && json[pos-1] == '"');
    if (inQuote) {
        end = json.find('"', pos);
    } else {
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ') end++;
    }
    return json.substr(pos, end - pos);
}

// Extract array body for given key, e.g. "friends":[...] -> contents inside [ ]
// Returns substring with the [ ... ] still wrapping if found, else empty.
static std::string JsonGetArray(const std::string& json, const char* key) {
    std::string search = std::string("\"") + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find('[', pos);
    if (pos == std::string::npos) return "";
    int depth = 0;
    size_t start = pos;
    for (; pos < json.size(); pos++) {
        if (json[pos] == '[') depth++;
        else if (json[pos] == ']') {
            depth--;
            if (depth == 0) return json.substr(start, pos - start + 1);
        }
    }
    return "";
}

// Split a JSON array body into individual object substrings.
// Input: "[{...},{...}]" or "[]"  -> vector of "{...}" strings
static std::vector<std::string> JsonSplitObjects(const std::string& arr) {
    std::vector<std::string> result;
    int depth = 0;
    size_t start = std::string::npos;
    bool inStr = false;
    char prev = 0;
    for (size_t i = 0; i < arr.size(); i++) {
        char c = arr[i];
        if (inStr) {
            if (c == '"' && prev != '\\') inStr = false;
        } else {
            if (c == '"') inStr = true;
            else if (c == '{') {
                if (depth == 0) start = i;
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0 && start != std::string::npos) {
                    result.push_back(arr.substr(start, i - start + 1));
                    start = std::string::npos;
                }
            }
        }
        prev = c;
    }
    return result;
}

// ============================================================
// Version & Patch system
// ============================================================

static bool g_clientPatched = false;
static char g_clientVersion[32] = "none";
static char g_serverVersion[32] = "0.4.0";  // TODO: fetch from server
static bool g_patchAvailable = false;
static bool g_patching = false;
static float g_patchProgress = 0.0f;

static void ReadClientVersion(const char* gamePath) {
    std::string path = std::string(gamePath) + "\\" + VERSION_FILE;
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
        fgets(g_clientVersion, sizeof(g_clientVersion), f);
        // Trim newline
        char* nl = strchr(g_clientVersion, '\n');
        if (nl) *nl = 0;
        fclose(f);
    } else {
        strncpy(g_clientVersion, "not patched", sizeof(g_clientVersion));
    }
}

static void WriteClientVersion(const char* gamePath, const char* version) {
    std::string path = std::string(gamePath) + "\\" + VERSION_FILE;
    FILE* f = fopen(path.c_str(), "w");
    if (f) {
        fprintf(f, "%s", version);
        fclose(f);
    }
}

// Forward declaration
extern bool g_serverOnline;

// ============================================================
// Sanity check (game directory + patch integrity)
// ============================================================
static std::string g_sanityReport;
static bool g_sanityOk = false;
static bool g_patchSane = false;

// Required PIE patch files (relative to gamePath)
static const char* kRequiredPatchFiles[] = {
    "BadBloodGame.exe",
    "LightFX.dll",
    "LightFX_original.dll",
    "engine_x64_rwdi.dll",
    "texture_hook.dll",
    "tstunnel.exe",
    "stunnel_client.conf",
    "server.ini",
    "ColdClientLoader.ini",
    "steam_api64.dll",
    "steamclient64.dll",
    "certs\\cert.pem",
    "certs\\key.pem",
    "steam_settings\\custom_broadcasts.txt",
    "DW\\Data0.pak",
    NULL
};

// DLL filenames that are KNOWN to conflict (other injectors / overlays / mods)
// If found in game root they will hijack our LightFX proxy or crash the game.
static const char* kConflictDlls[] = {
    "dxgi.dll", "d3d11.dll", "d3d9.dll", "winmm.dll", "dinput8.dll",
    "version.dll", "msacm32.dll", "wininet.dll", "opengl32.dll",
    "xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll",
    "ReShade.dll", "ReShade64.dll", "dxgi.fx",
    NULL
};

static bool FileExistsA(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Returns true if directory looks like a valid PIE-patched DLBB install.
// Fills g_sanityReport with human-readable lines.
static bool RunSanityCheck(const char* gamePath) {
    g_sanityReport.clear();
    g_sanityOk = false;
    g_patchSane = false;

    if (!gamePath || !*gamePath) {
        g_sanityReport = "[X] No game path set.\n";
        return false;
    }

    DWORD a = GetFileAttributesA(gamePath);
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
        g_sanityReport = "[X] Game path does not exist.\n";
        return false;
    }

    char buf[512];
    int missing = 0;
    g_sanityReport += "=== PATCH FILES ===\n";
    for (int i = 0; kRequiredPatchFiles[i]; i++) {
        std::string full = std::string(gamePath) + "\\" + kRequiredPatchFiles[i];
        if (FileExistsA(full)) {
            snprintf(buf, sizeof(buf), "[OK] %s\n", kRequiredPatchFiles[i]);
        } else {
            snprintf(buf, sizeof(buf), "[MISSING] %s\n", kRequiredPatchFiles[i]);
            missing++;
        }
        g_sanityReport += buf;
    }

    // Check pie_version.txt presence
    std::string verPath = std::string(gamePath) + "\\" VERSION_FILE;
    if (!FileExistsA(verPath)) {
        g_sanityReport += "[MISSING] pie_version.txt\n";
        missing++;
    }

    g_patchSane = (missing == 0);

    // Conflict DLL scan in game root
    g_sanityReport += "\n=== CONFLICT SCAN ===\n";
    int conflicts = 0;
    for (int i = 0; kConflictDlls[i]; i++) {
        std::string full = std::string(gamePath) + "\\" + kConflictDlls[i];
        if (FileExistsA(full)) {
            snprintf(buf, sizeof(buf), "[!] %s found - may conflict with PIE\n", kConflictDlls[i]);
            g_sanityReport += buf;
            conflicts++;
        }
    }
    if (conflicts == 0) g_sanityReport += "[OK] No known conflicting DLLs.\n";

    // Summary
    g_sanityReport += "\n=== SUMMARY ===\n";
    if (missing == 0 && conflicts == 0) {
        g_sanityReport += "[OK] Game directory looks clean and patched.\n";
        g_sanityOk = true;
    } else {
        snprintf(buf, sizeof(buf), "%d missing file(s), %d conflict(s).\n", missing, conflicts);
        g_sanityReport += buf;
        if (missing > 0) g_sanityReport += "Run PATCH GAME to install missing files.\n";
        if (conflicts > 0) g_sanityReport += "Remove conflicting DLLs before launching.\n";
    }
    return g_sanityOk;
}

static void CheckPatchAvailable() {
    std::string resp = HttpPost("/api/version", "{}");
    std::string ver = JsonGet(resp, "version");
    if (!ver.empty()) {
        strncpy(g_serverVersion, ver.c_str(), sizeof(g_serverVersion)-1);
        g_serverOnline = true;
    } else {
        g_serverOnline = false;
    }
    g_patchAvailable = (strcmp(g_clientVersion, g_serverVersion) != 0);
    // Run sanity check — patch is "applied" only if version matches AND all files present
    RunSanityCheck(g_cache.gamePath);
    g_clientPatched = !g_patchAvailable && g_patchSane;
}

// Download patch zip from server and extract to game folder
static bool DownloadAndApplyPatch(const char* gamePath) {
    HINTERNET hInternet = InternetOpenA("PIE-Launcher/" LAUNCHER_VER, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return false;

    HINTERNET hConnect = InternetConnectA(hInternet, SERVER_HOST, SERVER_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) { InternetCloseHandle(hInternet); return false; }

    DWORD flags = INTERNET_FLAG_NO_CACHE_WRITE;
    HINTERNET hRequest = HttpOpenRequestA(hConnect, "GET", "/api/patch", NULL, NULL, NULL, flags, 0);
    if (!hRequest) { InternetCloseHandle(hConnect); InternetCloseHandle(hInternet); return false; }

    if (!HttpSendRequestA(hRequest, NULL, 0, NULL, 0)) {
        InternetCloseHandle(hRequest); InternetCloseHandle(hConnect); InternetCloseHandle(hInternet);
        return false;
    }

    // Download to temp file
    std::string tmpZip = std::string(gamePath) + "\\pie_patch.zip";
    FILE* f = fopen(tmpZip.c_str(), "wb");
    if (!f) { InternetCloseHandle(hRequest); InternetCloseHandle(hConnect); InternetCloseHandle(hInternet); return false; }

    char buf[8192];
    DWORD bytesRead;
    DWORD totalRead = 0;
    while (InternetReadFile(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
        fwrite(buf, 1, bytesRead, f);
        totalRead += bytesRead;
    }
    fclose(f);

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    if (totalRead < 100) { remove(tmpZip.c_str()); return false; }

    // Extract zip directly into game folder (zip has correct folder structure)
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "powershell -NoProfile -Command \"Expand-Archive -Force -Path '%s' -DestinationPath '%s'\"",
        tmpZip.c_str(), gamePath);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    WaitForSingleObject(pi.hProcess, 60000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Cleanup zip
    remove(tmpZip.c_str());

    // Write version file
    WriteClientVersion(gamePath, g_serverVersion);

    // Create steam_settings/custom_broadcasts.txt with VPS IP
    // (Goldberg matchmaking relay — bypasses CGNAT, relay runs on VPS:47584)
    // Done AFTER patch extraction so the steam_settings folder exists.
    {
        std::string ssDir = std::string(gamePath) + "\\steam_settings";
        CreateDirectoryA(ssDir.c_str(), NULL); // no-op if exists
        std::string bcPath = ssDir + "\\custom_broadcasts.txt";
        FILE* f = fopen(bcPath.c_str(), "w");
        if (f) {
            fprintf(f, "%s\n", VPS_IP);
            fclose(f);
        }
    }

    return true;
}

// ============================================================
// Game launcher
// ============================================================

static void WriteGoldbergConfig(const char* gamePath, const char* username, const char* sessionId) {
    std::string base = std::string(gamePath) + "\\steam_settings\\";
    CreateDirectoryA(base.c_str(), NULL);

    std::string namePath = base + "force_account_name.txt";
    FILE* f = fopen(namePath.c_str(), "w");
    if (f) { fprintf(f, "%s", username); fclose(f); }

    std::string idPath = base + "force_steamid.txt";
    f = fopen(idPath.c_str(), "w");
    if (f) { fprintf(f, "%s", sessionId); fclose(f); }
}

// Game process tracking (used by friend list heartbeat too)
static bool g_inGame = false;
static HANDLE g_gameProcess = NULL;

static DWORD WINAPI GameWatcherThread(LPVOID h) {
    HANDLE proc = (HANDLE)h;
    WaitForSingleObject(proc, INFINITE);
    CloseHandle(proc);
    g_gameProcess = NULL;
    g_inGame = false;
    return 0;
}

static void LaunchGame(const char* gamePath, uint64_t joinLobbyId = 0) {
    StartStunnel(gamePath);

    std::string exe = std::string(gamePath) + "\\BadBloodGame.exe";

    // Build command line. CreateProcessA wants a writable buffer where argv[0]
    // is the exe name (so the game's GetCommandLine sees it).
    char cmdLine[512];
    if (joinLobbyId) {
        snprintf(cmdLine, sizeof(cmdLine), "\"%s\" --pie-join %llu",
                 exe.c_str(), (unsigned long long)joinLobbyId);
    } else {
        snprintf(cmdLine, sizeof(cmdLine), "\"%s\"", exe.c_str());
    }

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    CreateProcessA(exe.c_str(), cmdLine, NULL, NULL, FALSE, 0, NULL, gamePath, &si, &pi);
    if (pi.hProcess) {
        g_inGame = true;
        g_gameProcess = pi.hProcess;
        CloseHandle(pi.hThread);
        // Spawn watcher thread to detect when game exits
        HANDLE t = CreateThread(NULL, 0, GameWatcherThread, pi.hProcess, 0, NULL);
        if (t) CloseHandle(t);
    }
}

// ============================================================
// D3D11
// ============================================================

static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = {60, 1};
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc = {1, 0};
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel;
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        fl, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    ID3D11Texture2D* pBB;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBB));
    g_pd3dDevice->CreateRenderTargetView(pBB, nullptr, &g_mainRenderTargetView);
    pBB->Release();
    return true;
}

static void CleanupDeviceD3D() {
    if (g_mainRenderTargetView) g_mainRenderTargetView->Release();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    if (msg == WM_SIZE && g_pd3dDevice && wParam != SIZE_MINIMIZED) {
        if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
        g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
        ID3D11Texture2D* pBB;
        g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBB));
        g_pd3dDevice->CreateRenderTargetView(pBB, nullptr, &g_mainRenderTargetView);
        pBB->Release();
        return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
// State
// ============================================================

enum Screen { SCREEN_EULA, SCREEN_LOGIN, SCREEN_SETUP, SCREEN_MAIN, SCREEN_SETTINGS };
static Screen g_screen = SCREEN_EULA;
static bool g_loggedIn = false;
static char g_username[64] = "";
static char g_password[64] = "";
static char g_regUser[64] = "";
static char g_regPass[64] = "";
static char g_regPass2[64] = "";
static char g_statusMsg[256] = "";
static bool g_showRegister = false;
bool g_serverOnline = false;
static int  g_playerCount = 0;
static char g_sessionId[64] = "";
static char g_token[64] = "";
static uint64_t g_plsId = 0;
static float g_downloadProgress = 0.0f;
static bool g_downloading = false;

// ============================================================
// Friends List state
// ============================================================

enum FriendStatus { FS_OFFLINE = 0, FS_ONLINE = 1, FS_INGAME = 2 };

struct Friend {
    uint64_t pls_id;
    char nick[64];
    FriendStatus status;
};

struct FriendRequest {
    uint64_t pls_id;
    char nick[64];
    bool incoming; // true = they sent to me, false = I sent to them
};

struct Toast {
    char title[64];
    char body[128];
    float startTime;   // GetTime() when shown
    float duration;    // seconds visible
    bool actionable;   // shows Accept/Decline buttons
    uint64_t friendId; // sender pls_id for actionable toasts
    uint64_t lobbyId;  // lobby_id to join (for lobby invites)
    bool dismissed;    // true after user clicked Accept/Decline
};

static std::vector<Friend> g_friends;
static std::vector<FriendRequest> g_requests;
static std::vector<Toast> g_toasts;
static bool g_friendsPanelOpen = true;        // sidebar visible by default
static int  g_friendsTab = 0;                  // 0 = friends, 1 = requests
static char g_friendSearchBuf[64] = "";
static char g_friendAddBuf[64] = "";
static bool g_showAddFriendPopup = false;

// Critical section guarding g_friends/g_requests/g_toasts (declared early so
// helpers like FriendsAddToast can reference it before the body lives below).
static CRITICAL_SECTION g_friendsLock;
static volatile bool g_friendsLockInit = false;

// Forward decls for symbols defined further down
static void FriendsStartPolling();
static void FriendsStopPolling();

// ============================================================
// UI helpers
// ============================================================

static void CenterText(const char* text, ImVec4 color = ImVec4(1,1,1,1)) {
    float w = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - w) / 2);
    ImGui::TextColored(color, "%s", text);
}

static void SetStatus(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_statusMsg, sizeof(g_statusMsg), fmt, args);
    va_end(args);
}

static void RenderLogo(float size = 80.0f) {
    if (g_logoSRV) {
        float aspect = (float)g_logoW / g_logoH;
        float w = size * aspect, h = size;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - w) / 2);

        // Pulsating glow via tint alpha
        float t = GetTime();
        float glow = 0.85f + 0.15f * sinf(t * 1.5f);
        ImGui::Image((ImTextureID)g_logoSRV, ImVec2(w, h));
    }
}

static void RenderTitle() {
    RenderLogo(100.0f);
    ImGui::Dummy(ImVec2(0, 8));

    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.Size > 1) ImGui::PushFont(io.Fonts->Fonts[1]);
    CenterText("DYING LIGHT: BAD BLOOD", ImVec4(1.0f, 0.55f, 0.0f, 1.0f));
    if (io.Fonts->Fonts.Size > 1) ImGui::PopFont();
    CenterText("PIE EDITION", ImVec4(0.67f, 0.67f, 0.67f, 1.0f));
}

static void RenderServerDot() {
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetCursorPos(ImVec2(ds.x - 200, 12));
    ImGui::Text("Server:");
    ImGui::SameLine();
    if (g_serverOnline)
        ImGui::TextColored(ImVec4(0.2f,1,0.2f,1), "ONLINE (%d)", g_playerCount);
    else
        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "OFFLINE");
}

static void RenderStatus() {
    if (g_statusMsg[0]) {
        ImGui::Dummy(ImVec2(0, 8));
        CenterText(g_statusMsg, ImVec4(1.0f, 1.0f, 0.5f, 1.0f));
    }
}

#define BEGIN_FULLSCREEN(name) \
    ImGui::SetNextWindowPos(ImVec2(0,0)); \
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize); \
    ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);

// ============================================================
// Screens
// ============================================================

static void ScreenEULA() {
    BEGIN_FULLSCREEN("EULA");
    ImGui::Dummy(ImVec2(0, 20));
    RenderTitle();
    ImGui::Dummy(ImVec2(0, 15));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));

    float cw = 700, wx = (ImGui::GetWindowWidth() - cw) / 2;
    ImGui::SetCursorPosX(wx);
    ImGui::BeginGroup();

    CenterText("END USER LICENSE AGREEMENT", ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::BeginChild("EulaText", ImVec2(cw, 350), true);
    ImGui::PushTextWrapPos(cw - 20);
    ImGui::TextColored(ImVec4(1,1,1,1), "IMPORTANT - PLEASE READ CAREFULLY");
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextWrapped("This software (\"PIE Edition\") is an unofficial, community-made modification for Dying Light: Bad Blood, a game developed by Techland S.A. This project is NOT affiliated with, endorsed by, or associated with Techland S.A. in any way.");
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextWrapped("1. NON-COMMERCIAL USE: This software is provided entirely free of charge and is intended solely for non-commercial, community preservation purposes.");
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::TextWrapped("2. NO WARRANTY: This software is provided \"AS IS\" without warranty of any kind.");
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::TextWrapped("3. INTELLECTUAL PROPERTY: All original game assets remain the property of Techland S.A.");
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::TextWrapped("4. PRESERVATION PURPOSE: The sole purpose is to preserve access to the game following server shutdown and delisting.");
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::TextWrapped("5. ASSUMPTION OF RISK: You use this software at your own risk.");
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::TextWrapped("6. COMPLIANCE: If Techland requests removal, we will comply immediately.");
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::TextWrapped("7. USER CONDUCT: No cheating, harassment, or abuse. Violations result in permanent bans.");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 10));
    static bool checked = false;
    ImGui::Checkbox("I have read and agree to the terms above", &checked);
    ImGui::Dummy(ImVec2(0, 8));

    bool active = checked;
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f,0.5f,0,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f,0.6f,0.1f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f,0.4f,0,1));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f,0.3f,0.3f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f,0.3f,0.3f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f,0.3f,0.3f,1));
    }
    if (ImGui::Button("Accept & Continue", ImVec2(cw, 40)) && active) {
        g_cache.eulaAccepted = true;
        SaveCache();
        g_screen = SCREEN_LOGIN;
    }
    ImGui::PopStyleColor(3);

    ImGui::EndGroup();
    ImGui::End();
}

static void ScreenLogin() {
    BEGIN_FULLSCREEN("Login");
    RenderServerDot();
    ImGui::Dummy(ImVec2(0, 20));
    RenderTitle();
    ImGui::Dummy(ImVec2(0, 15));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 30));

    float fw = 400, ww = ImGui::GetWindowWidth(), sx = (ww - fw) / 2;

    if (!g_showRegister) {
        CenterText("Login to your account");
        ImGui::Dummy(ImVec2(0, 15));

        ImGui::SetCursorPosX(sx); ImGui::PushItemWidth(fw);
        ImGui::InputText("##user", g_username, sizeof(g_username));
        ImGui::SetCursorPosX(sx);
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "Username");

        ImGui::Dummy(ImVec2(0, 5));
        ImGui::SetCursorPosX(sx);
        ImGui::InputText("##pass", g_password, sizeof(g_password), ImGuiInputTextFlags_Password);
        ImGui::SetCursorPosX(sx);
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "Password");
        ImGui::PopItemWidth();

        ImGui::Dummy(ImVec2(0, 15));
        ImGui::SetCursorPosX(sx);
        if (ImGui::Button("Login", ImVec2(fw/2-5, 35))) {
            char body[256];
            snprintf(body, sizeof(body), "{\"username\":\"%s\",\"password\":\"%s\"}", g_username, g_password);
            std::string resp = HttpPost("/api/login", body);
            std::string err = JsonGet(resp, "error");
            if (!err.empty()) {
                SetStatus("Login failed: %s", err.c_str());
            } else {
                std::string user = JsonGet(resp, "username");
                std::string sid = JsonGet(resp, "session_id");
                std::string tok = JsonGet(resp, "token");
                std::string pid = JsonGet(resp, "pls_id");
                strncpy(g_username, user.c_str(), sizeof(g_username)-1);
                strncpy(g_sessionId, sid.c_str(), sizeof(g_sessionId)-1);
                strncpy(g_token, tok.c_str(), sizeof(g_token)-1);
                g_plsId = pid.empty() ? 0 : strtoull(pid.c_str(), nullptr, 10);
                FriendsStartPolling();
                strncpy(g_cache.lastUsername, g_username, sizeof(g_cache.lastUsername)-1);
                SaveCache();
                g_loggedIn = true;
                g_statusMsg[0] = 0;
                if (g_cache.gamePath[0]) {
                    ReadClientVersion(g_cache.gamePath);
                    CheckPatchAvailable();
                    g_screen = SCREEN_MAIN;
                } else {
                    g_screen = SCREEN_SETUP;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Register", ImVec2(fw/2-5, 35))) {
            g_showRegister = true;
            g_statusMsg[0] = 0;
        }
    } else {
        CenterText("Create a new account");
        ImGui::Dummy(ImVec2(0, 15));

        ImGui::SetCursorPosX(sx); ImGui::PushItemWidth(fw);
        ImGui::InputText("##ruser", g_regUser, sizeof(g_regUser));
        ImGui::SetCursorPosX(sx);
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "Username");

        ImGui::Dummy(ImVec2(0, 5));
        ImGui::SetCursorPosX(sx);
        ImGui::InputText("##rpass", g_regPass, sizeof(g_regPass), ImGuiInputTextFlags_Password);
        ImGui::SetCursorPosX(sx);
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "Password");

        ImGui::Dummy(ImVec2(0, 5));
        ImGui::SetCursorPosX(sx);
        ImGui::InputText("##rpass2", g_regPass2, sizeof(g_regPass2), ImGuiInputTextFlags_Password);
        ImGui::SetCursorPosX(sx);
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "Confirm Password");
        ImGui::PopItemWidth();

        ImGui::Dummy(ImVec2(0, 15));
        ImGui::SetCursorPosX(sx);
        if (ImGui::Button("Create Account", ImVec2(fw/2-5, 35))) {
            if (strcmp(g_regPass, g_regPass2) != 0) {
                SetStatus("Passwords don't match!");
            } else {
                char body[256];
                snprintf(body, sizeof(body), "{\"username\":\"%s\",\"password\":\"%s\"}", g_regUser, g_regPass);
                std::string resp = HttpPost("/api/register", body);
                std::string err = JsonGet(resp, "error");
                if (!err.empty()) {
                    SetStatus("Registration failed: %s", err.c_str());
                } else {
                    g_showRegister = false;
                    SetStatus("Account created! Please login.");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Back to Login", ImVec2(fw/2-5, 35))) {
            g_showRegister = false;
            g_statusMsg[0] = 0;
        }
    }

    RenderStatus();
    ImGui::End();
}

static void ScreenSetup() {
    BEGIN_FULLSCREEN("Setup");
    RenderServerDot();
    ImGui::Dummy(ImVec2(0, 20));
    RenderTitle();
    ImGui::Dummy(ImVec2(0, 30));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 20));

    float cw = 500, ww = ImGui::GetWindowWidth(), sx = (ww - cw) / 2;

    CenterText("Choose how to set up your client:");
    ImGui::Dummy(ImVec2(0, 15));

    ImGui::SetCursorPosX(sx);
    if (ImGui::Button("Browse for existing installation", ImVec2(cw, 40))) {
        BROWSEINFOA bi = {};
        bi.lpszTitle = "Select Dying Light Bad Blood folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl) {
            SHGetPathFromIDListA(pidl, g_cache.gamePath);
            CoTaskMemFree(pidl);
            SaveCache();
        }
    }

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::SetCursorPosX(sx);
    ImGui::PushItemWidth(cw);
    ImGui::InputText("##gpath", g_cache.gamePath, sizeof(g_cache.gamePath));
    ImGui::PopItemWidth();
    ImGui::SetCursorPosX(sx);
    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "Game path");

    ImGui::Dummy(ImVec2(0, 15));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 15));

    ImGui::SetCursorPosX(sx);
    if (ImGui::Button("Download full client (coming soon)", ImVec2(cw, 40))) {
        SetStatus("Download not available yet");
    }

    ImGui::Dummy(ImVec2(0, 20));
    if (g_cache.gamePath[0]) {
        ImGui::SetCursorPosX(sx);
        if (ImGui::Button("Continue", ImVec2(cw, 40))) {
            SaveCache();
            ReadClientVersion(g_cache.gamePath);
            CheckPatchAvailable();
            g_screen = SCREEN_MAIN;
            g_statusMsg[0] = 0;
        }
    }

    RenderStatus();
    ImGui::End();
}

static void ScreenSettings() {
    BEGIN_FULLSCREEN("Settings");
    RenderServerDot();
    ImGui::Dummy(ImVec2(0, 20));
    RenderTitle();
    ImGui::Dummy(ImVec2(0, 15));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 20));

    float cw = 500, ww = ImGui::GetWindowWidth(), sx = (ww - cw) / 2;

    CenterText("Settings", ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
    ImGui::Dummy(ImVec2(0, 15));

    // Game path
    ImGui::SetCursorPosX(sx);
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1), "Game Installation Path:");
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::SetCursorPosX(sx);
    ImGui::PushItemWidth(cw - 90);
    ImGui::InputText("##settpath", g_cache.gamePath, sizeof(g_cache.gamePath));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Browse", ImVec2(80, 0))) {
        BROWSEINFOA bi = {};
        bi.lpszTitle = "Select Dying Light Bad Blood folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl) {
            SHGetPathFromIDListA(pidl, g_cache.gamePath);
            CoTaskMemFree(pidl);
        }
    }

    ImGui::Dummy(ImVec2(0, 15));

    // Verify game files
    ImGui::SetCursorPosX(sx);
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1), "Integrity:");
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::SetCursorPosX(sx);
    if (ImGui::Button("Verify Game Files", ImVec2(cw, 30))) {
        RunSanityCheck(g_cache.gamePath);
        SetStatus(g_sanityOk ? "Game files OK" : "Game files have issues — see report");
    }
    if (!g_sanityReport.empty()) {
        ImGui::SetCursorPosX(sx);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.047f,0.047f,0.071f,1));
        ImGui::BeginChild("SanityReport", ImVec2(cw, 160), true);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f,0.8f,0.8f,1));
        ImGui::TextUnformatted(g_sanityReport.c_str());
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 20));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 15));

    // Reset cache
    ImGui::SetCursorPosX(sx);
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1), "Reset:");
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::SetCursorPosX(sx);
    if (ImGui::Button("Reset all settings", ImVec2(cw, 30))) {
        memset(&g_cache, 0, sizeof(g_cache));
        SaveCache();
        g_loggedIn = false;
        g_screen = SCREEN_EULA;
        SetStatus("Settings reset!");
        ImGui::End();
        return;
    }
    ImGui::SetCursorPosX(sx);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Clears EULA acceptance, game path, and saved username.");

    ImGui::Dummy(ImVec2(0, 20));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 15));

    // About
    ImGui::SetCursorPosX(sx);
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1), "About:");
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::SetCursorPosX(sx);
    ImGui::Text("PIE Launcher v%s", LAUNCHER_VER);
    ImGui::SetCursorPosX(sx);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Dying Light: Bad Blood — Community Preservation Project");
    ImGui::SetCursorPosX(sx);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Created by MrPie");

    ImGui::Dummy(ImVec2(0, 25));

    // Back button
    ImGui::SetCursorPosX(sx);
    if (ImGui::Button("Save & Back", ImVec2(cw, 40))) {
        SaveCache();
        ReadClientVersion(g_cache.gamePath);
        CheckPatchAvailable();
        g_screen = SCREEN_MAIN;
        SetStatus("Settings saved!");
    }

    ImGui::End();
}

// ============================================================
// Friends List
// ============================================================

static void FriendsAddToast(const char* title, const char* body, bool actionable = false, uint64_t friendId = 0) {
    Toast t = {};
    strncpy(t.title, title, sizeof(t.title)-1);
    strncpy(t.body, body, sizeof(t.body)-1);
    t.startTime = GetTime();
    t.duration = actionable ? 10.0f : 4.0f;
    t.actionable = actionable;
    t.friendId = friendId;
    if (g_friendsLockInit) {
        EnterCriticalSection(&g_friendsLock);
        g_toasts.push_back(t);
        LeaveCriticalSection(&g_friendsLock);
    } else {
        g_toasts.push_back(t);
    }
    MessageBeep(MB_ICONASTERISK);
}

// ── Concurrent state (UI thread reads, poll thread writes) ──
// g_friendsLock is forward-declared above near g_friends.
static HANDLE g_friendsThread = NULL;
static volatile bool g_friendsThreadRun = false;
// g_inGame is defined earlier (near LaunchGame)

static void FriendsLockInit() {
    if (!g_friendsLockInit) {
        InitializeCriticalSection(&g_friendsLock);
        g_friendsLockInit = true;
    }
}

// ── HTTP wrappers ──
static bool FriendsHeartbeat() {
    if (!g_token[0]) return false;
    char body[64];
    snprintf(body, sizeof(body), "{\"state\":\"%s\"}", g_inGame ? "ingame" : "online");
    std::string r = HttpRequest("POST", "/api/friends/heartbeat", body, g_token);
    return r.find("\"ok\":true") != std::string::npos;
}

static bool FriendsFetchList() {
    if (!g_token[0]) return false;
    std::string r = HttpRequest("GET", "/api/friends/list", "", g_token);
    std::string arr = JsonGetArray(r, "friends");
    if (arr.empty()) return false;
    auto objs = JsonSplitObjects(arr);

    std::vector<Friend> tmp;
    tmp.reserve(objs.size());
    for (auto& o : objs) {
        Friend f = {};
        std::string pid = JsonGet(o, "pls_id");
        std::string nick = JsonGet(o, "nick");
        std::string st = JsonGet(o, "status");
        f.pls_id = pid.empty() ? 0 : strtoull(pid.c_str(), nullptr, 10);
        strncpy(f.nick, nick.c_str(), sizeof(f.nick)-1);
        if (st == "ingame") f.status = FS_INGAME;
        else if (st == "online") f.status = FS_ONLINE;
        else f.status = FS_OFFLINE;
        tmp.push_back(f);
    }

    EnterCriticalSection(&g_friendsLock);
    g_friends = std::move(tmp);
    LeaveCriticalSection(&g_friendsLock);
    return true;
}

static bool FriendsFetchRequests() {
    if (!g_token[0]) return false;
    std::string r = HttpRequest("GET", "/api/friends/requests", "", g_token);
    std::string incArr = JsonGetArray(r, "incoming");
    std::string outArr = JsonGetArray(r, "outgoing");
    auto incObjs = JsonSplitObjects(incArr);
    auto outObjs = JsonSplitObjects(outArr);

    std::vector<FriendRequest> tmp;
    for (auto& o : incObjs) {
        FriendRequest fr = {};
        std::string pid = JsonGet(o, "pls_id");
        std::string nick = JsonGet(o, "nick");
        fr.pls_id = pid.empty() ? 0 : strtoull(pid.c_str(), nullptr, 10);
        strncpy(fr.nick, nick.c_str(), sizeof(fr.nick)-1);
        fr.incoming = true;
        tmp.push_back(fr);
    }
    for (auto& o : outObjs) {
        FriendRequest fr = {};
        std::string pid = JsonGet(o, "pls_id");
        std::string nick = JsonGet(o, "nick");
        fr.pls_id = pid.empty() ? 0 : strtoull(pid.c_str(), nullptr, 10);
        strncpy(fr.nick, nick.c_str(), sizeof(fr.nick)-1);
        fr.incoming = false;
        tmp.push_back(fr);
    }

    EnterCriticalSection(&g_friendsLock);
    // Detect new incoming requests for toast notification
    for (auto& nr : tmp) {
        if (!nr.incoming) continue;
        bool existed = false;
        for (auto& old : g_requests) {
            if (old.pls_id == nr.pls_id && old.incoming) { existed = true; break; }
        }
        if (!existed) {
            // schedule toast (defer queueing because Toast vector itself might be touched on UI thread)
            // We push directly under lock since toasts are also protected here
            char body[128];
            snprintf(body, sizeof(body), "%s wants to add you", nr.nick);
            // Inline toast push
            Toast t = {};
            strncpy(t.title, "Friend Request", sizeof(t.title)-1);
            strncpy(t.body, body, sizeof(t.body)-1);
            t.startTime = GetTime();
            t.duration = 6.0f;
            g_toasts.push_back(t);
            MessageBeep(MB_ICONASTERISK);
        }
    }
    g_requests = std::move(tmp);
    LeaveCriticalSection(&g_friendsLock);
    return true;
}

static bool FriendsSendAdd(const char* nick) {
    if (!g_token[0]) return false;
    char body[128];
    snprintf(body, sizeof(body), "{\"nick\":\"%s\"}", nick);
    std::string r = HttpRequest("POST", "/api/friends/add", body, g_token);
    return r.find("\"ok\":true") != std::string::npos;
}

static bool FriendsSendAccept(uint64_t from_id) {
    if (!g_token[0]) return false;
    char body[64];
    snprintf(body, sizeof(body), "{\"from_id\":%llu}", (unsigned long long)from_id);
    std::string r = HttpRequest("POST", "/api/friends/accept", body, g_token);
    return r.find("\"ok\":true") != std::string::npos;
}

static bool FriendsSendDecline(uint64_t from_id) {
    if (!g_token[0]) return false;
    char body[64];
    snprintf(body, sizeof(body), "{\"from_id\":%llu}", (unsigned long long)from_id);
    std::string r = HttpRequest("POST", "/api/friends/decline", body, g_token);
    return r.find("\"ok\":true") != std::string::npos;
}

static bool FriendsSendCancel(uint64_t to_id) {
    if (!g_token[0]) return false;
    char body[64];
    snprintf(body, sizeof(body), "{\"to_id\":%llu}", (unsigned long long)to_id);
    std::string r = HttpRequest("POST", "/api/friends/cancel", body, g_token);
    return r.find("\"ok\":true") != std::string::npos;
}

static bool FriendsSendRemove(uint64_t pls_id) {
    if (!g_token[0]) return false;
    char body[64];
    snprintf(body, sizeof(body), "{\"pls_id\":%llu}", (unsigned long long)pls_id);
    std::string r = HttpRequest("POST", "/api/friends/remove", body, g_token);
    return r.find("\"ok\":true") != std::string::npos;
}

static bool FriendsSendInvite(uint64_t to_id, const char* lobby_id) {
    if (!g_token[0]) return false;
    char body[160];
    snprintf(body, sizeof(body), "{\"to_id\":%llu,\"lobby_id\":\"%s\"}",
             (unsigned long long)to_id, lobby_id);
    std::string r = HttpRequest("POST", "/api/lobby/invite", body, g_token);
    return r.find("\"ok\":true") != std::string::npos;
}

static bool FriendsPollLobbyInvites() {
    if (!g_token[0]) return false;
    std::string r = HttpRequest("GET", "/api/lobby/invites/poll", "", g_token);
    std::string arr = JsonGetArray(r, "invites");
    auto objs = JsonSplitObjects(arr);
    if (objs.empty()) return true;
    EnterCriticalSection(&g_friendsLock);
    for (auto& o : objs) {
        std::string nick = JsonGet(o, "from_nick");
        std::string lid = JsonGet(o, "lobby_id");
        std::string fid = JsonGet(o, "from_id");
        char body[128];
        snprintf(body, sizeof(body), "%s invited you to a lobby", nick.c_str());
        Toast t = {};
        strncpy(t.title, "Lobby Invite", sizeof(t.title)-1);
        strncpy(t.body, body, sizeof(t.body)-1);
        t.startTime = GetTime();
        t.duration = 30.0f; // longer for actionable
        t.actionable = true;
        t.friendId = fid.empty() ? 0 : strtoull(fid.c_str(), nullptr, 10);
        t.lobbyId = lid.empty() ? 0 : strtoull(lid.c_str(), nullptr, 10);
        t.dismissed = false;
        g_toasts.push_back(t);
        MessageBeep(MB_ICONASTERISK);
    }
    LeaveCriticalSection(&g_friendsLock);
    return true;
}

// ── Background polling thread ──
// Runs while user is logged in. Sends heartbeat every 10s, fetches list/requests
// every 5s, polls lobby invites every 3s.
static DWORD WINAPI FriendsPollThread(LPVOID) {
    DWORD lastHeartbeat = 0;
    DWORD lastList = 0;
    DWORD lastRequests = 0;
    DWORD lastInvites = 0;

    while (g_friendsThreadRun) {
        DWORD now = GetTickCount();

        if (now - lastHeartbeat >= 10000) {
            FriendsHeartbeat();
            lastHeartbeat = now;
        }
        if (now - lastList >= 5000) {
            FriendsFetchList();
            lastList = now;
        }
        if (now - lastRequests >= 5000) {
            FriendsFetchRequests();
            lastRequests = now;
        }
        if (now - lastInvites >= 3000) {
            FriendsPollLobbyInvites();
            lastInvites = now;
        }

        Sleep(500);
    }
    return 0;
}

static void FriendsStartPolling() {
    FriendsLockInit();
    if (g_friendsThread) return;
    g_friendsThreadRun = true;
    g_friendsThread = CreateThread(NULL, 0, FriendsPollThread, NULL, 0, NULL);
}

static void FriendsStopPolling() {
    if (!g_friendsThread) return;
    g_friendsThreadRun = false;
    WaitForSingleObject(g_friendsThread, 2000);
    CloseHandle(g_friendsThread);
    g_friendsThread = NULL;
    EnterCriticalSection(&g_friendsLock);
    g_friends.clear();
    g_requests.clear();
    LeaveCriticalSection(&g_friendsLock);
}

// Removed mock data init — now populated by FriendsFetchList from server.
static void FriendsInitMockData() {
}

static int FriendsCountIncoming() {
    int n = 0;
    for (auto& r : g_requests) if (r.incoming) n++;
    return n;
}

static int FriendsCountOnline() {
    int n = 0;
    for (auto& f : g_friends) if (f.status != FS_OFFLINE) n++;
    return n;
}

static const char* FriendStatusText(FriendStatus s) {
    switch (s) {
        case FS_ONLINE: return "Online";
        case FS_INGAME: return "In Game";
        default:        return "Offline";
    }
}

static ImVec4 FriendStatusColor(FriendStatus s) {
    switch (s) {
        case FS_ONLINE: return ImVec4(0.0f, 1.0f, 0.53f, 1);  // green
        case FS_INGAME: return ImVec4(1.0f, 0.78f, 0.0f, 1);  // amber
        default:        return ImVec4(0.4f, 0.4f, 0.42f, 1);  // grey
    }
}

// Draw a colored circle (avatar dot) at current cursor position
static void DrawAvatarDot(float radius, ImVec4 color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    p.y += radius + 2;
    p.x += radius + 2;
    ImU32 col = ImGui::ColorConvertFloat4ToU32(color);
    dl->AddCircleFilled(p, radius, col, 16);
    // Outline
    dl->AddCircle(p, radius, ImGui::ColorConvertFloat4ToU32(ImVec4(0,0,0,0.5f)), 16, 1.0f);
    ImGui::Dummy(ImVec2(radius * 2 + 6, radius * 2 + 6));
}

static void RenderFriendsSidebar(float panelW, float panelH) {
    ImGui::BeginChild("FriendsPanel", ImVec2(panelW, panelH), true, ImGuiWindowFlags_NoScrollbar);

    // Header
    if (ImGui::GetIO().Fonts->Fonts.Size > 1) ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.0f, 1), "FRIENDS");
    if (ImGui::GetIO().Fonts->Fonts.Size > 1) ImGui::PopFont();

    ImGui::SameLine();
    float btnW = 60;
    ImGui::SetCursorPosX(panelW - btnW - 8);
    if (ImGui::SmallButton("+ Add")) {
        g_showAddFriendPopup = true;
        g_friendAddBuf[0] = 0;
    }

    ImGui::Separator();

    // Self status
    DrawAvatarDot(6, ImVec4(0,1,0.53f,1));
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.91f,0.91f,0.91f,1), "%s", g_username[0] ? g_username : "You");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f,0.4f,0.42f,1), "Online");

    ImGui::Dummy(ImVec2(0, 4));

    // Sub-tabs (Friends / Requests with badge)
    int incoming = FriendsCountIncoming();
    char tab0Label[32], tab1Label[32];
    snprintf(tab0Label, sizeof(tab0Label), "Friends (%d)", FriendsCountOnline());
    if (incoming > 0)
        snprintf(tab1Label, sizeof(tab1Label), "Requests [%d]", incoming);
    else
        snprintf(tab1Label, sizeof(tab1Label), "Requests");

    // Snapshot under lock so the UI thread iterates a stable copy
    std::vector<Friend> friendsCopy;
    std::vector<FriendRequest> requestsCopy;
    EnterCriticalSection(&g_friendsLock);
    friendsCopy = g_friends;
    requestsCopy = g_requests;
    LeaveCriticalSection(&g_friendsLock);

    if (ImGui::BeginTabBar("FriendsTabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        if (ImGui::BeginTabItem(tab0Label)) {
            g_friendsTab = 0;

            ImGui::Dummy(ImVec2(0, 4));
            ImGui::SetNextItemWidth(panelW - 24);
            ImGui::InputTextWithHint("##search", "Search...", g_friendSearchBuf, sizeof(g_friendSearchBuf));

            ImGui::Dummy(ImVec2(0, 4));

            ImGui::BeginChild("FriendsList", ImVec2(0, 0), false);

            // Sort: online > ingame > offline, alphabetical inside same status
            std::sort(friendsCopy.begin(), friendsCopy.end(), [](const Friend& a, const Friend& b) {
                if (a.status != b.status) {
                    int sa = (a.status == FS_OFFLINE) ? 2 : (a.status == FS_INGAME ? 1 : 0);
                    int sb = (b.status == FS_OFFLINE) ? 2 : (b.status == FS_INGAME ? 1 : 0);
                    return sa < sb;
                }
                return strcmp(a.nick, b.nick) < 0;
            });

            for (Friend& f : friendsCopy) {
                // Search filter
                if (g_friendSearchBuf[0]) {
                    char nick_lc[64], q_lc[64];
                    int i;
                    for (i = 0; f.nick[i] && i < 63; i++) nick_lc[i] = (char)tolower((unsigned char)f.nick[i]);
                    nick_lc[i] = 0;
                    for (i = 0; g_friendSearchBuf[i] && i < 63; i++) q_lc[i] = (char)tolower((unsigned char)g_friendSearchBuf[i]);
                    q_lc[i] = 0;
                    if (!strstr(nick_lc, q_lc)) continue;
                }

                ImGui::PushID((int)f.pls_id);
                ImGui::BeginGroup();
                DrawAvatarDot(6, FriendStatusColor(f.status));
                ImGui::SameLine();

                ImVec4 nameCol = (f.status == FS_OFFLINE) ? ImVec4(0.5f,0.5f,0.5f,1) : ImVec4(0.91f,0.91f,0.91f,1);
                ImGui::TextColored(nameCol, "%s", f.nick);
                ImGui::SameLine();
                ImGui::TextColored(FriendStatusColor(f.status), "  %s", FriendStatusText(f.status));

                if (f.status == FS_INGAME) {
                    ImGui::SameLine();
                    float bw = 50;
                    ImGui::SetCursorPosX(panelW - bw - 16);
                    if (ImGui::SmallButton("Join")) {
                        // The friend is in-game. We send ourselves an invite from them
                        // wouldn't make sense — instead, we ask the server for a join target.
                        // For MVP we use the friend's session_id as lobby_id (= their CSteamID,
                        // which is the host CSteamID in Goldberg P2P convention).
                        // TODO: replace with proper "request join" API
                        FriendsAddToast("Join", "Use Invite from the host instead");
                    }
                } else if (f.status == FS_ONLINE) {
                    ImGui::SameLine();
                    float bw = 56;
                    ImGui::SetCursorPosX(panelW - bw - 16);
                    if (ImGui::SmallButton("Invite")) {
                        // Lobby ID = our session_id (= our CSteamID in Goldberg).
                        // The receiving side will call SteamMatchmaking::JoinLobby(this id).
                        // This works because Goldberg uses the host's CSteamID as the lobby key.
                        if (g_sessionId[0] && FriendsSendInvite(f.pls_id, g_sessionId)) {
                            char msg[128]; snprintf(msg, sizeof(msg), "Invite sent to %s", f.nick);
                            FriendsAddToast("Invite", msg);
                        } else {
                            FriendsAddToast("Error", "Invite failed");
                        }
                    }
                }

                ImGui::EndGroup();

                if (ImGui::BeginPopupContextItem("friend_ctx")) {
                    if (ImGui::MenuItem("Remove friend")) {
                        if (FriendsSendRemove(f.pls_id)) {
                            FriendsFetchList();
                        }
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
                ImGui::Dummy(ImVec2(0, 2));
            }

            if (friendsCopy.empty()) {
                ImGui::TextColored(ImVec4(0.4f,0.4f,0.42f,1), "  No friends yet");
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(tab1Label)) {
            g_friendsTab = 1;

            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextColored(ImVec4(0.53f,0.53f,0.53f,1), "INCOMING");
            ImGui::Separator();
            bool any = false;
            for (auto& r : requestsCopy) {
                if (!r.incoming) continue;
                any = true;
                ImGui::PushID((int)r.pls_id);
                DrawAvatarDot(5, ImVec4(0.6f,0.6f,0.6f,1));
                ImGui::SameLine();
                ImGui::Text("%s", r.nick);
                ImGui::SameLine();
                float bw = 50;
                ImGui::SetCursorPosX(panelW - bw*2 - 24);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f,0.5f,0.2f,1));
                if (ImGui::SmallButton("Accept")) {
                    if (FriendsSendAccept(r.pls_id)) {
                        FriendsFetchList();
                        FriendsFetchRequests();
                        char msg[128]; snprintf(msg, sizeof(msg), "%s added to friends", r.nick);
                        FriendsAddToast("Friend", msg);
                    }
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::SmallButton("Decline")) {
                    if (FriendsSendDecline(r.pls_id)) FriendsFetchRequests();
                }
                ImGui::PopID();
                ImGui::Dummy(ImVec2(0, 2));
            }
            if (!any) ImGui::TextColored(ImVec4(0.4f,0.4f,0.42f,1), "  No incoming requests");

            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextColored(ImVec4(0.53f,0.53f,0.53f,1), "OUTGOING");
            ImGui::Separator();
            any = false;
            for (auto& r : requestsCopy) {
                if (r.incoming) continue;
                any = true;
                ImGui::PushID((int)r.pls_id + 9000);
                DrawAvatarDot(5, ImVec4(0.6f,0.6f,0.6f,1));
                ImGui::SameLine();
                ImGui::Text("%s", r.nick);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), " Pending");
                ImGui::SameLine();
                float bw = 56;
                ImGui::SetCursorPosX(panelW - bw - 16);
                if (ImGui::SmallButton("Cancel")) {
                    if (FriendsSendCancel(r.pls_id)) FriendsFetchRequests();
                }
                ImGui::PopID();
                ImGui::Dummy(ImVec2(0, 2));
            }
            if (!any) ImGui::TextColored(ImVec4(0.4f,0.4f,0.42f,1), "  No outgoing requests");

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndChild();

    // Add friend popup
    if (g_showAddFriendPopup) {
        ImGui::OpenPopup("AddFriendPopup");
        g_showAddFriendPopup = false;
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(320, 0));
    if (ImGui::BeginPopupModal("AddFriendPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextColored(ImVec4(1.0f,0.55f,0.0f,1), "ADD FRIEND");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::Text("Player name:");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##addnick", g_friendAddBuf, sizeof(g_friendAddBuf));
        ImGui::Dummy(ImVec2(0, 6));
        if (ImGui::Button("Send Request", ImVec2(140, 28))) {
            if (g_friendAddBuf[0]) {
                if (FriendsSendAdd(g_friendAddBuf)) {
                    char msg[128]; snprintf(msg, sizeof(msg), "Friend request sent to %s", g_friendAddBuf);
                    FriendsAddToast("Request", msg);
                    FriendsFetchRequests();
                    FriendsFetchList();
                    g_friendAddBuf[0] = 0;
                    ImGui::CloseCurrentPopup();
                } else {
                    FriendsAddToast("Error", "Player not found or already friends");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 28))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void RenderToasts() {
    // Drop expired and dismissed toasts; snapshot rest
    std::vector<Toast> toastsCopy;
    if (g_friendsLockInit) {
        EnterCriticalSection(&g_friendsLock);
        for (auto it = g_toasts.begin(); it != g_toasts.end(); ) {
            float age = GetTime() - it->startTime;
            if (age > it->duration || it->dismissed) it = g_toasts.erase(it);
            else ++it;
        }
        toastsCopy = g_toasts;
        LeaveCriticalSection(&g_friendsLock);
    } else {
        for (auto it = g_toasts.begin(); it != g_toasts.end(); ) {
            float age = GetTime() - it->startTime;
            if (age > it->duration || it->dismissed) it = g_toasts.erase(it);
            else ++it;
        }
        toastsCopy = g_toasts;
    }

    if (toastsCopy.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float screenW = io.DisplaySize.x;
    float screenH = io.DisplaySize.y;
    float toastW = 280;
    float margin = 12;

    int idx = 0;
    for (auto& t : toastsCopy) {
        float age = GetTime() - t.startTime;
        float alpha = 1.0f;
        if (age < 0.3f) alpha = age / 0.3f;
        else if (age > t.duration - 0.5f) alpha = (t.duration - age) / 0.5f;

        float toastH = t.actionable ? 100.0f : 70.0f;
        float yOff = screenH - margin - (idx + 1) * (toastH + 8);
        float xOff = screenW - margin - toastW;

        ImGui::SetNextWindowPos(ImVec2(xOff, yOff));
        ImGui::SetNextWindowSize(ImVec2(toastW, toastH));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.10f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.55f, 0.0f, 0.7f));

        char id[32]; snprintf(id, sizeof(id), "##toast%d", idx);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
        if (!t.actionable) flags |= ImGuiWindowFlags_NoInputs;

        ImGui::Begin(id, nullptr, flags);

        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.0f, 1), "%s", t.title);
        ImGui::PushTextWrapPos(toastW - 12);
        ImGui::Text("%s", t.body);
        ImGui::PopTextWrapPos();

        if (t.actionable) {
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.55f, 0.2f, 1));
            if (ImGui::Button("Accept", ImVec2(80, 22))) {
                // Launch game with --pie-join lobbyId
                if (g_inGame) {
                    // Game already running — can't pass arg, would need IPC.
                    // For MVP we just notify and ignore.
                    FriendsAddToast("Error", "Close the game first, then accept");
                } else if (g_cache.gamePath[0] && t.lobbyId) {
                    WriteGoldbergConfig(g_cache.gamePath, g_username, g_sessionId);
                    LaunchGame(g_cache.gamePath, t.lobbyId);
                    SetStatus("Joining lobby...");
                }
                // Mark dismissed
                if (g_friendsLockInit) {
                    EnterCriticalSection(&g_friendsLock);
                    for (auto& tt : g_toasts) {
                        if (tt.startTime == t.startTime && tt.lobbyId == t.lobbyId) tt.dismissed = true;
                    }
                    LeaveCriticalSection(&g_friendsLock);
                }
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.1f, 0.1f, 1));
            if (ImGui::Button("Decline", ImVec2(80, 22))) {
                if (g_friendsLockInit) {
                    EnterCriticalSection(&g_friendsLock);
                    for (auto& tt : g_toasts) {
                        if (tt.startTime == t.startTime && tt.lobbyId == t.lobbyId) tt.dismissed = true;
                    }
                    LeaveCriticalSection(&g_friendsLock);
                }
            }
            ImGui::PopStyleColor();
        }

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        idx++;
    }
}

static void ScreenMain() {
    BEGIN_FULLSCREEN("Main");
    RenderServerDot();

    // Init mock data on first frame
    FriendsInitMockData();

    float ww = ImGui::GetWindowWidth();
    float wh = ImGui::GetWindowHeight();

    // Friends sidebar reserves space on the right when open
    float friendsW = g_friendsPanelOpen ? 280.0f : 0.0f;
    float availW = ww - friendsW;

    // Responsive width for main content
    float pw = availW * 0.88f;
    if (pw < 520) pw = 520;
    if (pw > 1100) pw = 1100;
    float sx = (availW - pw) / 2;
    float leftW = pw * 0.64f, rightW = pw * 0.34f;

    // ── Header: Logo + Title + User ──
    ImGui::Dummy(ImVec2(0, 8));
    RenderLogo(64.0f);
    ImGui::Dummy(ImVec2(0, 4));

    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.Size > 1) ImGui::PushFont(io.Fonts->Fonts[1]);
    CenterText("DYING LIGHT: BAD BLOOD", ImVec4(1.0f, 0.55f, 0.0f, 1.0f));
    if (io.Fonts->Fonts.Size > 1) ImGui::PopFont();
    CenterText("PIE EDITION", ImVec4(0.53f, 0.53f, 0.53f, 1.0f));

    ImGui::Dummy(ImVec2(0, 4));

    // User bar
    ImGui::SetCursorPosX(sx);
    ImGui::TextColored(ImVec4(0.0f,1.0f,0.53f,1), "  %s", g_username);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.33f,0.33f,0.33f,1), "|");
    ImGui::SameLine();
    if (ImGui::SmallButton("Disconnect")) {
        FriendsStopPolling();
        g_loggedIn = false;
        g_sessionId[0] = 0;
        g_token[0] = 0;
        g_plsId = 0;
        g_username[0] = 0;
        g_password[0] = 0;
        g_statusMsg[0] = 0;
        g_friends.clear();
        g_requests.clear();
        g_screen = SCREEN_LOGIN;
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.33f,0.33f,0.33f,1), "|");
    ImGui::SameLine();
    ImGui::Text("Client:");
    ImGui::SameLine();
    if (g_clientPatched)
        ImGui::TextColored(ImVec4(0.0f,1.0f,0.53f,1), "v%s", g_clientVersion);
    else
        ImGui::TextColored(ImVec4(1.0f,0.6f,0.2f,1), "needs update");

    // Friends toggle (right-aligned)
    {
        int incoming = FriendsCountIncoming();
        char btnLabel[32];
        if (incoming > 0)
            snprintf(btnLabel, sizeof(btnLabel), "Friends [%d]", incoming);
        else
            snprintf(btnLabel, sizeof(btnLabel), "%s Friends", g_friendsPanelOpen ? ">" : "<");
        ImVec2 sz = ImGui::CalcTextSize(btnLabel);
        ImGui::SameLine();
        ImGui::SetCursorPosX(sx + pw - sz.x - 24);
        if (incoming > 0) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.15f, 0.15f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.25f, 0.25f, 1));
        }
        if (ImGui::SmallButton(btnLabel)) {
            g_friendsPanelOpen = !g_friendsPanelOpen;
        }
        if (incoming > 0) ImGui::PopStyleColor(2);
    }

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8));

    // ── PLAY Button (big, pulsing if ready) ──
    bool canPlay = g_loggedIn && g_serverOnline && g_clientPatched;

    if (canPlay) {
        float pulse = 0.15f + 0.55f * (0.5f + 0.5f * sinf(GetTime() * 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, pulse, 0.0f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f,0.75f,0.1f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f,0.4f,0.0f,1));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f,0.15f,0.18f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f,0.18f,0.22f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f,0.12f,0.15f,1));
    }
    ImGui::SetCursorPosX(sx);
    if (io.Fonts->Fonts.Size > 1) ImGui::PushFont(io.Fonts->Fonts[1]);
    if (ImGui::Button(canPlay ? "P L A Y" : "P L A Y", ImVec2(pw, 52)) && canPlay) {
        WriteGoldbergConfig(g_cache.gamePath, g_username, g_sessionId);
        LaunchGame(g_cache.gamePath);
        SetStatus("Game launched!");
    }
    if (io.Fonts->Fonts.Size > 1) ImGui::PopFont();
    ImGui::PopStyleColor(3);

    // ── Patch + Settings row ──
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetCursorPosX(sx);
    bool needsPatch = g_patchAvailable || !g_patchSane;
    if (needsPatch) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f,0.55f,0.0f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f,0.67f,0.2f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f,0.44f,0.0f,1));
    }
    if (ImGui::Button(needsPatch ? "PATCH GAME" : "Update Client", ImVec2(pw/2-5, 28))) {
        if (needsPatch) {
            SetStatus("Downloading patch from server...");
            if (DownloadAndApplyPatch(g_cache.gamePath)) {
                ReadClientVersion(g_cache.gamePath);
                CheckPatchAvailable();
                SetStatus("Patch v%s applied!", g_clientVersion);
            } else {
                SetStatus("Patch failed! Is the server running?");
            }
        } else {
            SetStatus("Client is up to date!");
        }
    }
    if (needsPatch) ImGui::PopStyleColor(3);
    ImGui::SameLine();
    if (ImGui::Button("Settings", ImVec2(pw/2-5, 28))) {
        g_statusMsg[0] = 0;
        g_screen = SCREEN_SETTINGS;
    }

    if (needsPatch) {
        ImGui::SetCursorPosX(sx);
        ImGui::TextColored(ImVec4(1,0.8f,0.3f,1), "New patch: v%s (you have: %s)", g_serverVersion, g_clientVersion);
    }

    RenderStatus();

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    // ── Two column layout: News/Changelog left, Info right ──
    ImGui::SetCursorPosX(sx);
    ImGui::BeginGroup();

    // Compute remaining height for the two columns (leave room for footer)
    float colH = wh - ImGui::GetCursorPosY() - 40;
    if (colH < 220) colH = 220;

    // LEFT COLUMN
    ImGui::BeginChild("LeftCol", ImVec2(leftW, colH), false);

    // News
    ImGui::TextColored(ImVec4(1.0f,0.55f,0.0f,1), "NEWS");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushTextWrapPos(leftW - 10);
    ImGui::TextColored(ImVec4(0.91f,0.91f,0.91f,1), "OPEN BETA");
    ImGui::TextColored(ImVec4(0.53f,0.53f,0.53f,1), "Colored gloves for all 20 Agent skins. Custom texture mod support via mods/textures/ folder. Dedicated remote server — just launch and play.");
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextColored(ImVec4(0.91f,0.91f,0.91f,1), "Known Issues");
    ImGui::TextColored(ImVec4(0.53f,0.53f,0.53f,1), "Loot boxes not functional. Leaderboard shows placeholder data. Menu gloves still default.");
    ImGui::PopTextWrapPos();

    ImGui::Dummy(ImVec2(0, 8));

    // Changelog
    ImGui::TextColored(ImVec4(1.0f,0.55f,0.0f,1), "CHANGELOG v0.4.0");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushTextWrapPos(leftW - 10);
    ImGui::TextColored(ImVec4(0.53f,0.53f,0.53f,1),
        "+ Runtime texture replacement (DLL hook)\n"
        "+ 20 custom FPP glove textures\n"
        "+ Remote dedicated server\n"
        "+ Auto-update via launcher\n"
        "+ Custom ABDM material system");
    ImGui::PopTextWrapPos();

    ImGui::EndChild();

    ImGui::SameLine();

    // RIGHT COLUMN
    ImGui::BeginChild("RightCol", ImVec2(rightW, colH), false);

    // Server info card
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.047f,0.047f,0.071f,1));
    ImGui::BeginChild("ServerCard", ImVec2(rightW - 5, 80), true);
    CenterText("SERVER", ImVec4(0.53f,0.53f,0.53f,1));
    if (g_serverOnline) {
        CenterText("ONLINE", ImVec4(0.0f,1.0f,0.53f,1));
        char pc[32]; snprintf(pc, sizeof(pc), "%d player%s", g_playerCount, g_playerCount != 1 ? "s" : "");
        CenterText(pc, ImVec4(0.53f,0.53f,0.53f,1));
    } else {
        CenterText("OFFLINE", ImVec4(1.0f,0.2f,0.33f,1));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 6));

    // Coming Soon card
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.047f,0.047f,0.071f,1));
    ImGui::BeginChild("ComingSoon", ImVec2(rightW - 5, 0), true);
    ImGui::TextColored(ImVec4(1.0f,0.55f,0.0f,1), "COMING SOON");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextColored(ImVec4(0.53f,0.53f,0.53f,1), "Friends List");
    ImGui::TextColored(ImVec4(0.33f,0.33f,0.33f,1), "  See who's online");
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextColored(ImVec4(0.53f,0.53f,0.53f,1), "Match History");
    ImGui::TextColored(ImVec4(0.33f,0.33f,0.33f,1), "  Recent games log");
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextColored(ImVec4(0.53f,0.53f,0.53f,1), "In-Game Overlay");
    ImGui::TextColored(ImVec4(0.33f,0.33f,0.33f,1), "  Party invites");
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextColored(ImVec4(0.53f,0.53f,0.53f,1), "Mod Workshop");
    ImGui::TextColored(ImVec4(0.33f,0.33f,0.33f,1), "  Share texture packs");
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::EndGroup();

    // ── Friends sidebar (right side, fixed) ──
    if (g_friendsPanelOpen) {
        float panelW = 280.0f;
        float panelH = wh - 24;  // leave footer
        ImGui::SetCursorPos(ImVec2(ww - panelW - 8, 8));
        RenderFriendsSidebar(panelW, panelH);
    }

    // ── Footer ──
    ImGui::SetCursorPosY(wh - 25);
    CenterText("PIE Edition v" LAUNCHER_VER " — Created by MrPie — Not affiliated with Techland", ImVec4(0.25f,0.25f,0.25f,1));

    ImGui::End();

    // Toast notifications (rendered after main window so they're on top)
    RenderToasts();
}

// ============================================================
// Main
// ============================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Check Visual C++ 2010 x64 Redistributable (required by LightFX_original.dll)
    // Without msvcr100.dll the game crashes on launch with 0xc06d007e MODULE_NOT_FOUND
    {
        HMODULE hVc = LoadLibraryA("msvcr100.dll");
        if (!hVc) {
            int r = MessageBoxA(NULL,
                "Visual C++ 2010 Redistributable (x64) is missing.\n"
                "The game will crash on launch without it (msvcr100.dll not found).\n\n"
                "Click OK to open the Microsoft download page, then install vcredist_x64.exe and restart the launcher.",
                "PIE Launcher \xE2\x80\x94 Missing Dependency",
                MB_OKCANCEL | MB_ICONWARNING);
            if (r == IDOK) {
                ShellExecuteA(NULL, "open",
                    "https://www.microsoft.com/en-us/download/details.aspx?id=26999",
                    NULL, NULL, SW_SHOWNORMAL);
            }
            return 1;
        }
        FreeLibrary(hVc);
    }

    // Load cached settings
    LoadCache();
    if (g_cache.eulaAccepted) g_screen = SCREEN_LOGIN;
    if (g_cache.lastUsername[0]) strncpy(g_username, g_cache.lastUsername, sizeof(g_username)-1);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0,
        hInstance, nullptr, nullptr, nullptr, nullptr, L"PIE_Launcher", nullptr };
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"PIE Launcher \u2014 Dying Light: Bad Blood",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 680, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); return 1; }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    // Match website CSS colors exactly
    s.WindowRounding = 0; s.FrameRounding = 6; s.GrabRounding = 4;
    s.WindowBorderSize = 0; s.FramePadding = ImVec2(10,6); s.ItemSpacing = ImVec2(8,8);
    s.ScrollbarRounding = 4; s.ScrollbarSize = 8;
    auto* c = s.Colors;
    // --bg: #050508, --bg2: #0c0c12, --bg3: #14141c
    c[ImGuiCol_WindowBg]       = ImVec4(0.020f,0.020f,0.031f,1); // #050508
    c[ImGuiCol_ChildBg]        = ImVec4(0.047f,0.047f,0.071f,1); // #0c0c12
    c[ImGuiCol_FrameBg]        = ImVec4(0.078f,0.078f,0.110f,1); // #14141c
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.110f,0.110f,0.150f,1);
    c[ImGuiCol_FrameBgActive]  = ImVec4(0.140f,0.140f,0.190f,1);
    // --orange: #ff8c00, --orange-light: #ffaa33, --orange-dark: #cc7000
    c[ImGuiCol_Button]         = ImVec4(1.00f,0.55f,0.00f,1); // #ff8c00
    c[ImGuiCol_ButtonHovered]  = ImVec4(1.00f,0.67f,0.20f,1); // #ffaa33
    c[ImGuiCol_ButtonActive]   = ImVec4(0.80f,0.44f,0.00f,1); // #cc7000
    c[ImGuiCol_Separator]      = ImVec4(0.20f,0.20f,0.25f,0.5f);
    c[ImGuiCol_Text]           = ImVec4(0.91f,0.91f,0.91f,1); // #e8e8e8
    c[ImGuiCol_TextDisabled]   = ImVec4(0.53f,0.53f,0.53f,1); // #888
    c[ImGuiCol_CheckMark]      = ImVec4(1.0f,0.55f,0.0f,1);
    c[ImGuiCol_SliderGrab]     = ImVec4(1.0f,0.55f,0.0f,1);
    c[ImGuiCol_Header]         = ImVec4(1.0f,0.55f,0.0f,0.2f);
    c[ImGuiCol_HeaderHovered]  = ImVec4(1.0f,0.55f,0.0f,0.3f);
    c[ImGuiCol_ScrollbarBg]    = ImVec4(0.020f,0.020f,0.031f,1);
    c[ImGuiCol_ScrollbarGrab]  = ImVec4(0.80f,0.44f,0.00f,1);

    // Load logo texture
    {
        char logoPath[MAX_PATH];
        GetModuleFileNameA(NULL, logoPath, MAX_PATH);
        std::string lp(logoPath);
        lp = lp.substr(0, lp.find_last_of('\\') + 1) + "assets\\logo.png";
        if (LoadLogoTexture(g_pd3dDevice, lp.c_str())) {
            // Logo loaded
        }
    }

    g_startTime = (float)GetTickCount() / 1000.0f;

    // Load custom font (Inter)
    ImGuiIO& io2 = ImGui::GetIO();
    char fontPath[MAX_PATH];
    GetModuleFileNameA(NULL, fontPath, MAX_PATH);
    std::string fp(fontPath);
    fp = fp.substr(0, fp.find_last_of('\\') + 1) + "assets\\Inter-Regular.ttf";
    if (GetFileAttributesA(fp.c_str()) != INVALID_FILE_ATTRIBUTES) {
        io2.Fonts->AddFontFromFileTTF(fp.c_str(), 16.0f);
        // Also load a bigger version for titles
        io2.Fonts->AddFontFromFileTTF(fp.c_str(), 28.0f);
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Add hosts entry (requires admin)
    AddHostsEntry();

    // Initial server check
    g_serverOnline = false;
    g_playerCount = 0;

    // Server status check timer
    DWORD lastServerCheck = 0;
    auto CheckServer = [&]() {
        DWORD now = GetTickCount();
        if (now - lastServerCheck > 20000 || lastServerCheck == 0) {
            lastServerCheck = now;
            std::string resp = HttpPost("/api/version", "{}");
            std::string ver = JsonGet(resp, "version");
            if (!ver.empty()) {
                g_serverOnline = true;
                strncpy(g_serverVersion, ver.c_str(), sizeof(g_serverVersion)-1);
            } else {
                g_serverOnline = false;
            }
        }
    };

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg); continue;
        }
        CheckServer();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        switch (g_screen) {
            case SCREEN_EULA:  ScreenEULA();  break;
            case SCREEN_LOGIN: ScreenLogin(); break;
            case SCREEN_SETUP: ScreenSetup(); break;
            case SCREEN_MAIN:     ScreenMain();     break;
            case SCREEN_SETTINGS: ScreenSettings(); break;
        }

        ImGui::Render();
        float cc[4] = {0.020f, 0.020f, 0.031f, 1}; // #050508
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    // Cleanup: remove hosts entry and stop stunnel
    RemoveHostsEntry();
    StopStunnel();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
