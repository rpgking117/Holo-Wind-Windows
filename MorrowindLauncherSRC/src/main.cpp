// ============================================================================
// MorrowindLauncher — F4SE Plugin
//
// Launches OpenMW and writes frames directly to the Pip-Boy's D3D11 render
// target texture via DXGI/D3D11 vtable hooks. No Scaleform involvement.
//
// Build: Windows x64 DLL (MSVC x64, native Windows)
// ============================================================================

// Papyrus native registration offsets are version-specific; disable until
// correct offsets for FO4 1.11.191 are found. F9 key handler still works.
#define DISABLE_PAPYRUS_REGISTER

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <string>
#include <thread>
#include <atomic>
#include <fstream>
#include <cstring>

#include "f4se_stubs.h"
#include "MorrowindBridge.h"
#include "papyrus_native.h"

// ---------------------------------------------------------------------------
// F4SE Plugin Version Data
// ---------------------------------------------------------------------------

extern "C" {
__declspec(dllexport) F4SEPluginVersionData F4SEPlugin_Version = {
    F4SEPluginVersionData::kVersion,
    2,                          // plugin version (bumped for D3D11 rewrite)
    "MorrowindLauncher",
    "ProjectTES",
    0,
    F4SEPluginVersionData::kStructureIndependence_1_11_137Layout,
    { RUNTIME_VERSION_1_11_191, 0 },
    0, 0, 0, {}
};
};

// ---------------------------------------------------------------------------
// Global State
// ---------------------------------------------------------------------------

PluginLog g_log;

static PluginHandle            g_pluginHandle = kPluginHandle_Invalid;
static F4SEMessagingInterface* g_messaging    = nullptr;
static F4SEPapyrusInterface*   g_papyrus     = nullptr;
static F4SEScaleformInterface* g_scaleform   = nullptr;

static std::atomic<bool>      g_morrowindRunning{false};
static uint64_t               g_pipboyActiveFrames = 0;
static uint64_t               g_lastShutdownTick = 0;

// Watcher process
static HANDLE                 g_hWatcher = nullptr;

// True only while Scaleform is actively finishing frames to the Pip-Boy RT
// (i.e. the Pip-Boy is visually open). Reset after 5 frames of inactivity.
static bool                   g_scaleformPipboyActive = false;
static uint64_t               g_scaleformPipboyLastFrame = 0;


// Shared memory bridge
static HANDLE           g_hMapFile    = nullptr;
static MorrowindBridge* g_bridge      = nullptr;

// OpenMW process
static HANDLE           g_hOpenMW     = nullptr;

// Config
static std::string g_openMWPath;
static std::string g_morrowindDataPath;
static std::string g_shutdownPath;

// Window subclass
static WNDPROC g_origWndProc = nullptr;
static HWND    g_fo4Window   = nullptr;

// ---------------------------------------------------------------------------
// D3D11 Hook State
// ---------------------------------------------------------------------------

static ID3D11Device*        g_d3dDevice       = nullptr;
static ID3D11DeviceContext*  g_d3dContext      = nullptr;
static ID3D11Texture2D*     g_pipboyTexture   = nullptr;
static ID3D11Texture2D*     g_stagingTexture  = nullptr;
static DXGI_FORMAT          g_pipboyFormat    = DXGI_FORMAT_UNKNOWN;
static bool                 g_pipboyFound     = false;
static bool                 g_pipboyBound     = false;
static ID3D11RenderTargetView* g_pipboyRTV    = nullptr;
static uint64_t             g_lastCopiedFrame = 0;
static uint32_t             g_pipboyRTWidth   = 0;
static uint32_t             g_pipboyRTHeight  = 0;
static uint64_t             g_presentCount    = 0;
static uint64_t             g_pipboyLastBoundFrame = 0;

// Diagnostics: track unique RT dimensions we've seen
struct RTDimEntry { uint32_t w; uint32_t h; uint32_t fmt; uint32_t bind; };
static RTDimEntry g_seenDims[64];
static int        g_seenDimCount = 0;
static int        g_diagLogCount = 0;

// Original vtable function pointers
typedef HRESULT (STDMETHODCALLTYPE *PFN_Present)(IDXGISwapChain*, UINT, UINT);
typedef void    (STDMETHODCALLTYPE *PFN_OMSetRenderTargets)(
    ID3D11DeviceContext*, UINT, ID3D11RenderTargetView*const*, ID3D11DepthStencilView*);

static PFN_Present            g_origPresent   = nullptr;
static PFN_OMSetRenderTargets g_origOMSetRT   = nullptr;

// ---------------------------------------------------------------------------
// INI Config
// ---------------------------------------------------------------------------

static std::string GetModuleDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    auto pos = s.find_last_of("\\/");
    return (pos != std::string::npos) ? s.substr(0, pos) : s;
}

static void LoadConfig() {
    std::string iniPath = GetModuleDir() + "\\Data\\F4SE\\Plugins\\MorrowindLauncher.ini";
    std::ifstream file(iniPath);
    if (!file.is_open()) {
        _MESSAGE("Could not open config: %s", iniPath.c_str());
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[')
            continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        auto trim = [](std::string& s) {
            auto a = s.find_first_not_of(" \t\r\n");
            auto b = s.find_last_not_of(" \t\r\n");
            s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };
        trim(key);
        trim(val);

        if (key == "OpenMWPath")             g_openMWPath = val;
        else if (key == "MorrowindDataPath") g_morrowindDataPath = val;
        else if (key == "ShutdownFile")      g_shutdownPath = val;
    }

    _MESSAGE("OpenMWPath    = %s", g_openMWPath.c_str());
    _MESSAGE("MorrowindData = %s", g_morrowindDataPath.c_str());
    _MESSAGE("ShutdownFile  = %s", g_shutdownPath.c_str());
}

// ---------------------------------------------------------------------------
// Shared Memory
// ---------------------------------------------------------------------------

static bool OpenSharedMemory() {
    g_hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, BRIDGE_NAME);
    if (!g_hMapFile) return false;

    g_bridge = (MorrowindBridge*)MapViewOfFile(
        g_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(MorrowindBridge));
    if (!g_bridge) {
        CloseHandle(g_hMapFile);
        g_hMapFile = nullptr;
        return false;
    }
    return true;
}

static void CloseSharedMemory() {
    if (g_bridge)   { UnmapViewOfFile(g_bridge); g_bridge = nullptr; }
    if (g_hMapFile) { CloseHandle(g_hMapFile);   g_hMapFile = nullptr; }
}

// ---------------------------------------------------------------------------
// Input Forwarding
// ---------------------------------------------------------------------------

static void UpdateMovementAxes() {
    if (!g_bridge) return;
    g_bridge->input.moveX = 0.0f;
    g_bridge->input.moveY = 0.0f;
    if (g_bridge->input.keyStates['W']) g_bridge->input.moveY += 1.0f;
    if (g_bridge->input.keyStates['S']) g_bridge->input.moveY -= 1.0f;
    if (g_bridge->input.keyStates['D']) g_bridge->input.moveX += 1.0f;
    if (g_bridge->input.keyStates['A']) g_bridge->input.moveX -= 1.0f;
}

static bool Papyrus_Launch();
static bool Papyrus_Shutdown();

static bool IsPipboyActive() {
    return g_pipboyFound && (g_presentCount - g_pipboyLastBoundFrame < 10);
}

static LRESULT CALLBACK MorrowindWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // F9: shutdown anytime Morrowind is running; launch ONLY when Pip-Boy is active
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam == VK_F9) {
        if (g_morrowindRunning.load()) {
            _MESSAGE("F9 pressed - shutting down Morrowind");
            Papyrus_Shutdown();
            g_morrowindRunning.store(false);
            return 0;
        } else if (IsPipboyActive()) {
            _MESSAGE("F9 pressed - launching Morrowind (Pip-Boy active)");
            Papyrus_Launch();
            return 0;
        }
        // Pip-Boy not open — pass F9 through to FO4 normally
        return CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam);
    }

    // Tab always passes through to FO4 for native holotape eject menu.
    // If Morrowind is running, shut it down first so the eject is clean.
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam == VK_TAB) {
        if (g_morrowindRunning.load()) {
            _MESSAGE("Tab pressed - shutting down Morrowind for holotape eject");
            Papyrus_Shutdown();
            g_morrowindRunning.store(false);
        }
        return CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam);
    }

    // Handle raw input — bypasses FO4's Pip-Boy UI consuming keys like E
    if (msg == WM_INPUT && g_morrowindRunning.load() && g_bridge) {
        UINT size = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        if (size <= 64) {
            uint8_t buf[64];
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == size) {
                RAWINPUT* raw = (RAWINPUT*)buf;
                if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                    USHORT vk = raw->data.keyboard.VKey;
                    bool down = !(raw->data.keyboard.Flags & RI_KEY_BREAK);

                    // Tab: always pass to FO4. Also trigger shutdown here in case
                    // WM_KEYDOWN for Tab never arrives (FO4 may eat it via DirectInput).
                    if (vk == VK_TAB) {
                        if (down && g_morrowindRunning.load()) {
                            _MESSAGE("Tab raw input - shutting down Morrowind");
                            Papyrus_Shutdown();
                            g_morrowindRunning.store(false);
                        }
                        return CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam);
                    }

                    if (vk > 0 && vk < 256) {
                        g_bridge->input.keyStates[vk] = down ? 1 : 0;
                        g_bridge->input.jump   = g_bridge->input.keyStates[VK_SPACE] != 0;
                        g_bridge->input.use    = g_bridge->input.keyStates['E'] != 0;
                        UpdateMovementAxes();
                    }
                } else if (raw->header.dwType == RIM_TYPEMOUSE) {
                    float dx = (float)raw->data.mouse.lLastX;
                    float dy = (float)raw->data.mouse.lLastY;
                    if (dx != 0.0f || dy != 0.0f) {
                        g_bridge->input.lookX += dx;
                        g_bridge->input.lookY += dy;
                    }
                    USHORT btn = raw->data.mouse.usButtonFlags;
                    if (btn & RI_MOUSE_LEFT_BUTTON_DOWN)  { g_bridge->input.keyStates[VK_LBUTTON] = 1; g_bridge->input.attack = true;  }
                    if (btn & RI_MOUSE_LEFT_BUTTON_UP)    { g_bridge->input.keyStates[VK_LBUTTON] = 0; g_bridge->input.attack = false; }
                    if (btn & RI_MOUSE_RIGHT_BUTTON_DOWN) { g_bridge->input.keyStates[VK_RBUTTON] = 1; }
                    if (btn & RI_MOUSE_RIGHT_BUTTON_UP)   { g_bridge->input.keyStates[VK_RBUTTON] = 0; }
                }
            }
        }
        return 0;
    }

    if (!g_morrowindRunning.load() || !g_bridge)
        return CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam);

    // Consume keyboard and mouse so FO4 doesn't process them while Morrowind runs
    // (Tab is handled above and never consumed)
    if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) return 0;
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) return 0;

    return CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam);
}

static void InstallWindowHook() {
    g_fo4Window = FindWindowA("Fallout4", nullptr);
    if (!g_fo4Window) {
        _MESSAGE("Cannot find FO4 window for input hook");
        return;
    }
    g_origWndProc = (WNDPROC)SetWindowLongPtrA(g_fo4Window, GWLP_WNDPROC, (LONG_PTR)MorrowindWndProc);
    _MESSAGE("Window subclassed for input routing (orig=%p)", g_origWndProc);
    // Raw input registered only when Morrowind starts, not here
}

static void InstallRawInput() {
    if (!g_fo4Window) return;
    RAWINPUTDEVICE rid[2] = {};
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage     = 0x06; // keyboard
    rid[0].dwFlags     = RIDEV_INPUTSINK;
    rid[0].hwndTarget  = g_fo4Window;
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage     = 0x02; // mouse
    rid[1].dwFlags     = RIDEV_INPUTSINK;
    rid[1].hwndTarget  = g_fo4Window;
    if (RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)))
        _MESSAGE("Raw input registered for keyboard + mouse");
    else
        _MESSAGE("Failed to register raw input (error %u)", GetLastError());
}

static void RemoveRawInput() {
    if (!g_fo4Window) {
        _MESSAGE("RemoveRawInput: no FO4 window");
        return;
    }
    // Replace INPUTSINK with a normal window-targeted registration.
    // RIDEV_REMOVE would destroy the registration entirely, killing FO4's own
    // raw input (mouse-look). Re-registering with flags=0 keeps the devices
    // registered for FO4's window without the background-capture flag.
    RAWINPUTDEVICE rid[2] = {};
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage     = 0x06;
    rid[0].dwFlags     = 0;
    rid[0].hwndTarget  = g_fo4Window;
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage     = 0x02;
    rid[1].dwFlags     = 0;
    rid[1].hwndTarget  = g_fo4Window;
    if (RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)))
        _MESSAGE("Raw input restored to normal window registration");
    else
        _MESSAGE("RemoveRawInput restore failed (error %u)", GetLastError());
}

static void RemoveWindowHook() {
    if (g_fo4Window && g_origWndProc) {
        SetWindowLongPtrA(g_fo4Window, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
        g_origWndProc = nullptr;
    }
}

// ---------------------------------------------------------------------------
// D3D11 Hooks — Pip-Boy Texture Injection
//
// Strategy:
//   1. Hook IDXGISwapChain::Present to capture the D3D11 device (one-time)
//   2. Hook ID3D11DeviceContext::OMSetRenderTargets to:
//      a. Find the 876×512 Pip-Boy render target by dimensions
//      b. Detect when Scaleform finishes rendering to it (RT unbind)
//      c. Immediately overwrite with bridge frame data before the 3D pass
// ---------------------------------------------------------------------------

static bool g_stagingHasData = false;

static void CopyBridgeFrame(ID3D11DeviceContext* ctx) {
    if (!g_stagingTexture || !g_pipboyTexture)
        return;

    // Update staging texture only when OpenMW produces a new frame
    if (g_bridge && g_bridge->morrowindReady) {
        uint64_t curFrame = g_bridge->frameID;
        if (curFrame > 0 && curFrame != g_lastCopiedFrame) {
            D3D11_MAPPED_SUBRESOURCE mapped;
            HRESULT hr = ctx->Map(g_stagingTexture, 0, D3D11_MAP_WRITE, 0, &mapped);
            if (SUCCEEDED(hr)) {
                const uint8_t* src = g_bridge->frameBuffer;
                bool swizzle = (g_pipboyFormat == DXGI_FORMAT_B8G8R8A8_UNORM ||
                                g_pipboyFormat == DXGI_FORMAT_B8G8R8X8_UNORM);

                auto boost = [](uint8_t v) -> uint8_t {
                    uint32_t b = (uint32_t)v * 268 / 255;
                    return b > 255 ? 255 : (uint8_t)b;
                };

                // Render into the visible screen area, stretching to fill
                const uint32_t imgLeft   = PIPBOY_PAD_LEFT;
                const uint32_t imgTop    = PIPBOY_PAD_TOP;
                const uint32_t imgRight  = g_pipboyRTWidth - PIPBOY_PAD_RIGHT;
                const uint32_t imgBottom = g_pipboyRTHeight - PIPBOY_PAD_BOTTOM;
                const uint32_t destW = imgRight - imgLeft;
                const uint32_t destH = imgBottom - imgTop;

                for (uint32_t dy = 0; dy < g_pipboyRTHeight; dy++) {
                    uint8_t* dst = (uint8_t*)mapped.pData + dy * mapped.RowPitch;

                    if (dy < imgTop || dy >= imgBottom) {
                        memset(dst, 0, g_pipboyRTWidth * 4);
                        continue;
                    }

                    uint32_t sy = (dy - imgTop) * PIPBOY_HEIGHT / destH;
                    if (sy >= PIPBOY_HEIGHT) sy = PIPBOY_HEIGHT - 1;
                    const uint8_t* srcRow = src + sy * PIPBOY_WIDTH * 4;

                    for (uint32_t dx = 0; dx < g_pipboyRTWidth; dx++) {
                        if (dx < imgLeft || dx >= imgRight) {
                            dst[dx*4+0] = 0;
                            dst[dx*4+1] = 0;
                            dst[dx*4+2] = 0;
                            dst[dx*4+3] = 0;
                            continue;
                        }

                        uint32_t sx = (dx - imgLeft) * PIPBOY_WIDTH / destW;
                        if (sx >= PIPBOY_WIDTH) sx = PIPBOY_WIDTH - 1;
                        const uint8_t* sp = srcRow + sx * 4;

                        if (swizzle) {
                            dst[dx*4+0] = boost(sp[2]);
                            dst[dx*4+1] = boost(sp[1]);
                            dst[dx*4+2] = boost(sp[0]);
                        } else {
                            dst[dx*4+0] = boost(sp[0]);
                            dst[dx*4+1] = boost(sp[1]);
                            dst[dx*4+2] = boost(sp[2]);
                        }
                        dst[dx*4+3] = 0xFF;
                    }
                }
                ctx->Unmap(g_stagingTexture, 0);
                g_lastCopiedFrame = curFrame;
                g_stagingHasData = true;

                static int logCount = 0;
                if (logCount < 10) {
                    _MESSAGE("Frame %llu: pad=(%u,%u,%u,%u) dest=%ux%u fmt=%u swiz=%d",
                             (unsigned long long)curFrame,
                             PIPBOY_PAD_LEFT, PIPBOY_PAD_RIGHT, PIPBOY_PAD_TOP, PIPBOY_PAD_BOTTOM,
                             destW, destH,
                             g_pipboyFormat, swizzle ? 1 : 0);
                    logCount++;
                }
            }
        }
    }

    // Always blit staging → Pip-Boy RT to overwrite whatever Scaleform drew
    if (g_stagingHasData) {
        ctx->CopyResource(g_pipboyTexture, g_stagingTexture);
    }
}

static void STDMETHODCALLTYPE HookedOMSetRT(
    ID3D11DeviceContext* self, UINT numViews,
    ID3D11RenderTargetView* const* ppRTV, ID3D11DepthStencilView* pDSV)
{
    bool wasBound = g_pipboyBound;
    g_pipboyBound = false;

    if (ppRTV && numViews > 0) {
        for (UINT i = 0; i < numViews; i++) {
            if (!ppRTV[i]) continue;

            if (!g_pipboyFound) {
                ID3D11Resource* res = nullptr;
                ppRTV[i]->GetResource(&res);
                if (res) {
                    ID3D11Texture2D* tex = nullptr;
                    if (SUCCEEDED(res->QueryInterface(
                            __uuidof(ID3D11Texture2D), (void**)&tex))) {
                        D3D11_TEXTURE2D_DESC desc;
                        tex->GetDesc(&desc);

                        // Log unique RT dimensions for diagnostics
                        if (g_diagLogCount < 200) {
                            bool seen = false;
                            for (int d = 0; d < g_seenDimCount; d++) {
                                if (g_seenDims[d].w == desc.Width &&
                                    g_seenDims[d].h == desc.Height &&
                                    g_seenDims[d].fmt == desc.Format) {
                                    seen = true;
                                    break;
                                }
                            }
                            if (!seen && g_seenDimCount < 64) {
                                g_seenDims[g_seenDimCount] = {desc.Width, desc.Height, (uint32_t)desc.Format, desc.BindFlags};
                                g_seenDimCount++;
                                _MESSAGE("RT #%d: %ux%u fmt=%u bind=0x%X mip=%u tex=%p",
                                         g_seenDimCount, desc.Width, desc.Height,
                                         desc.Format, desc.BindFlags, desc.MipLevels, tex);
                                g_diagLogCount++;
                            }
                        }

                        if (desc.Width == PIPBOY_RT_WIDTH &&
                            desc.MipLevels == 1 &&
                            (desc.BindFlags & D3D11_BIND_RENDER_TARGET)) {
                            g_pipboyTexture = tex;
                            g_pipboyFormat = desc.Format;
                            g_pipboyRTV = ppRTV[i];
                            g_pipboyRTWidth = desc.Width;
                            g_pipboyRTHeight = desc.Height;
                            g_pipboyFound = true;
                            _MESSAGE("Pip-Boy texture: %p (%ux%u fmt=%u bind=0x%X)",
                                     tex, desc.Width, desc.Height, desc.Format, desc.BindFlags);

                            D3D11_TEXTURE2D_DESC sd = {};
                            sd.Width = desc.Width;
                            sd.Height = desc.Height;
                            sd.MipLevels = 1;
                            sd.ArraySize = 1;
                            sd.Format = desc.Format;
                            sd.SampleDesc.Count = 1;
                            sd.Usage = D3D11_USAGE_STAGING;
                            sd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                            HRESULT hr = g_d3dDevice->CreateTexture2D(&sd, nullptr, &g_stagingTexture);
                            _MESSAGE("Staging texture: %p (%ux%u hr=0x%08X)",
                                     g_stagingTexture, desc.Width, desc.Height, hr);
                        } else {
                            tex->Release();
                        }
                    }
                    res->Release();
                }
            } else {
                if (ppRTV[i] == g_pipboyRTV) {
                    g_pipboyBound = true;
                }
            }
        }
    }

    if (g_pipboyBound)
        g_pipboyLastBoundFrame = g_presentCount;

    // Pip-Boy RT just unbound → Scaleform finished one frame to it
    if (wasBound && !g_pipboyBound) {
        g_scaleformPipboyLastFrame = g_presentCount;
        if (g_morrowindRunning.load())
            CopyBridgeFrame(self);
    }

    g_origOMSetRT(self, numViews, ppRTV, pDSV);
}

static HRESULT STDMETHODCALLTYPE HookedPresent(
    IDXGISwapChain* self, UINT syncInterval, UINT flags)
{
    g_presentCount++;

    // Maintain Scaleform-to-Pip-Boy active flag: true while Pip-Boy is visually open
    g_scaleformPipboyActive = (g_scaleformPipboyLastFrame > 0) &&
                              (g_presentCount - g_scaleformPipboyLastFrame < 5);

    // Last-chance copy: if OMSetRT didn't catch the transition, force it here
    if (g_d3dContext && g_pipboyFound && g_morrowindRunning.load()) {
        CopyBridgeFrame(g_d3dContext);
    }

    // One-time: capture device + context, then hook OMSetRenderTargets
    if (!g_d3dDevice) {
        if (SUCCEEDED(self->GetDevice(__uuidof(ID3D11Device), (void**)&g_d3dDevice))) {
            g_d3dDevice->GetImmediateContext(&g_d3dContext);
            _MESSAGE("D3D11 device: %p, context: %p", g_d3dDevice, g_d3dContext);

            // Hook OMSetRenderTargets (vtable index 33)
            void** ctxVtable = *(void***)g_d3dContext;
            g_origOMSetRT = (PFN_OMSetRenderTargets)ctxVtable[33];

            DWORD oldProtect;
            if (VirtualProtect(&ctxVtable[33], sizeof(void*),
                               PAGE_EXECUTE_READWRITE, &oldProtect)) {
                ctxVtable[33] = (void*)HookedOMSetRT;
                VirtualProtect(&ctxVtable[33], sizeof(void*), oldProtect, &oldProtect);
                _MESSAGE("Hooked OMSetRenderTargets at vtable[33]");
            } else {
                _MESSAGE("FAILED to hook OMSetRenderTargets (VirtualProtect error %u)",
                         GetLastError());
            }
        }
    }

    return g_origPresent(self, syncInterval, flags);
}

static void InstallD3D11Hooks() {
    _MESSAGE("Installing D3D11 Present hook...");

    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "MorrowindD3DHook";
    RegisterClassA(&wc);

    HWND dummyWnd = CreateWindowA("MorrowindD3DHook", "", WS_OVERLAPPED,
        0, 0, 2, 2, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 2;
    scd.BufferDesc.Height = 2;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = dummyWnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    IDXGISwapChain* dummySC = nullptr;
    ID3D11Device* dummyDev = nullptr;
    ID3D11DeviceContext* dummyCtx = nullptr;
    D3D_FEATURE_LEVEL fl;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &scd, &dummySC, &dummyDev, &fl, &dummyCtx);

    if (FAILED(hr)) {
        _MESSAGE("D3D11CreateDeviceAndSwapChain failed: 0x%08X", hr);
        DestroyWindow(dummyWnd);
        UnregisterClassA("MorrowindD3DHook", wc.hInstance);
        return;
    }

    _MESSAGE("Dummy swap chain created (feature level 0x%X)", fl);

    // Hook Present via vtable (index 8)
    // Under DXVK, all swap chains share the same vtable, so hooking the dummy
    // also hooks the game's real swap chain.
    void** scVtable = *(void***)dummySC;
    g_origPresent = (PFN_Present)scVtable[8];

    DWORD oldProtect;
    if (VirtualProtect(&scVtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        scVtable[8] = (void*)HookedPresent;
        VirtualProtect(&scVtable[8], sizeof(void*), oldProtect, &oldProtect);
        _MESSAGE("Hooked Present at vtable[8] (orig=%p)", g_origPresent);
    } else {
        _MESSAGE("FAILED to VirtualProtect Present vtable entry (error %u)", GetLastError());
    }

    dummyCtx->Release();
    dummyDev->Release();
    dummySC->Release();
    DestroyWindow(dummyWnd);
    UnregisterClassA("MorrowindD3DHook", wc.hInstance);
}

// ---------------------------------------------------------------------------
// OpenMW Watcher Thread
// ---------------------------------------------------------------------------

static void OpenMWWatcherThread() {
    _MESSAGE("Watcher thread started");

    // launch_openmw.exe: starts morrowind_watcher.ps1 (detached), writes
    // trigger file, then exits. We do NOT treat its exit as an error.
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::string cmdLine = "\"" + g_openMWPath + "\"";
    char cmdBuf[2048];
    strncpy(cmdBuf, cmdLine.c_str(), sizeof(cmdBuf) - 1);
    cmdBuf[sizeof(cmdBuf) - 1] = '\0';

    std::string workDir = g_openMWPath;
    auto lastSlash = workDir.find_last_of("\\/");
    if (lastSlash != std::string::npos)
        workDir = workDir.substr(0, lastSlash);

    if (!CreateProcessA(nullptr, cmdBuf, nullptr, nullptr, FALSE, 0,
                        nullptr, workDir.c_str(), &si, &pi)) {
        _MESSAGE("Failed to launch OpenMW (error %u)", GetLastError());
        g_morrowindRunning.store(false);
        return;
    }

    // Store handle but only to close it — launch_openmw.exe exits quickly
    // by design after starting the watcher and writing the trigger file.
    g_hOpenMW = pi.hProcess;
    if (pi.hThread) CloseHandle(pi.hThread);
    _MESSAGE("Launcher started (PID %u), waiting for shared memory...", pi.dwProcessId);

    // Wait up to 30s for SDL2 proxy to create shared memory.
    // Do NOT check launcher exit — it exits normally after ~300ms.
    for (int i = 0; i < 300; i++) {
        if (OpenSharedMemory()) break;
        Sleep(100);
    }

    if (!g_bridge) {
        _MESSAGE("Timed out waiting for shared memory (30s)");
        goto cleanup;
    }

    _MESSAGE("Shared memory connected, waiting for OpenMW ready signal...");

    for (int i = 0; i < 100; i++) {
        if (g_bridge->morrowindReady) break;
        Sleep(100);
    }

    if (!g_bridge->morrowindReady) {
        _MESSAGE("OpenMW never signaled ready");
        goto cleanup;
    }

    _MESSAGE("OpenMW ready — bridge active (pipboy texture %s)",
             g_pipboyFound ? "FOUND" : "not yet found");

    InstallRawInput();

    // Poll until shutdown — watcher PS1 manages OpenMW's actual process.
    // Shutdown is signaled by writing the shutdown file (via Papyrus_Shutdown).
    while (g_morrowindRunning.load()) {
        if (g_bridge && g_bridge->shutdownRequest) break;
        Sleep(500);
    }
    _MESSAGE("OpenMW session ended");

cleanup:
    g_morrowindRunning.store(false);
    g_lastCopiedFrame = 0;
    RemoveRawInput();

    if (g_bridge) memset(&g_bridge->input, 0, sizeof(g_bridge->input));
    CloseSharedMemory();
    if (g_hOpenMW) { CloseHandle(g_hOpenMW); g_hOpenMW = nullptr; }

    _MESSAGE("Cleanup complete");
}

// ---------------------------------------------------------------------------
// Launch / Shutdown
// ---------------------------------------------------------------------------

static bool InternalLaunch() {
    if (g_morrowindRunning.load()) return false;
    if (g_openMWPath.empty()) {
        _MESSAGE("InternalLaunch: OpenMWPath not configured");
        return false;
    }
    g_morrowindRunning.store(true);
    std::thread watcher(OpenMWWatcherThread);
    watcher.detach();
    return true;
}

static bool Papyrus_Launch() {
    if (g_morrowindRunning.load()) {
        _MESSAGE("Papyrus Launch() called but Morrowind is already running");
        return false;
    }
    if (g_openMWPath.empty()) {
        _MESSAGE("Papyrus Launch() called but OpenMWPath is not configured");
        return false;
    }
    _MESSAGE("Papyrus Launch() called");
    return InternalLaunch();
}

static bool Papyrus_Shutdown() {
    if (!g_morrowindRunning.load()) return false;

    g_lastShutdownTick = GetTickCount64();

    // Release raw input immediately so FO4 gets its controls back right away.
    // The watcher thread will also call RemoveRawInput() in cleanup, which is fine.
    RemoveRawInput();

    if (g_bridge) g_bridge->shutdownRequest = true;

    // Signal morrowind_watcher.ps1 to kill OpenMW by writing the shutdown file.
    // The watcher polls for this file every 500ms and calls proc.Kill() when found.
    if (!g_shutdownPath.empty()) {
        HANDLE h = CreateFileA(g_shutdownPath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            _MESSAGE("Shutdown file written: %s", g_shutdownPath.c_str());
        } else {
            _MESSAGE("Failed to write shutdown file %s (error %u)",
                     g_shutdownPath.c_str(), GetLastError());
        }
    } else {
        _MESSAGE("WARNING: ShutdownFile not configured — OpenMW will not be killed automatically");
    }

    return true;
}

static bool Papyrus_IsRunning() {
    return g_morrowindRunning.load();
}

// ---------------------------------------------------------------------------
// Papyrus VM Registration
// ---------------------------------------------------------------------------

static bool PapyrusRegisterFunctions(VirtualMachine* vm) {
    bool ok = true;
    ok &= RegisterNativeBoolFunction(vm, "Launch",    "MorrowindLauncher", Papyrus_Launch);
    ok &= RegisterNativeBoolFunction(vm, "IsRunning", "MorrowindLauncher", Papyrus_IsRunning);
    ok &= RegisterNativeBoolFunction(vm, "Shutdown",  "MorrowindLauncher", Papyrus_Shutdown);
    _MESSAGE("Papyrus native functions registered (%s)", ok ? "OK" : "FAILED");
    return true;
}

// ---------------------------------------------------------------------------
// Scaleform — GFx launch handler called by MorrowindDisplay.swf on init
// ---------------------------------------------------------------------------

static bool ScaleformCallback(GFxMovieView* view, GFxValue* root) {
    bool pipboyActive = IsPipboyActive();
    _MESSAGE("ScaleformCallback fired (running=%d, pipboyActive=%d)",
             g_morrowindRunning.load() ? 1 : 0, pipboyActive ? 1 : 0);

    if (!view || !view->movieRoot) {
        _MESSAGE("ScaleformCallback: null view or movieRoot");
        return true;
    }

    // Identify MorrowindDisplay.swf by reading _root.nSerpent — other SWFs won't have it
    GFxValue nSerpentVal = {};
    bool check = view->movieRoot->GetVariable(&nSerpentVal, "_root.nSerpent");
    _MESSAGE("ScaleformCallback: nSerpent check=%d type=%u", check ? 1 : 0, nSerpentVal.GetType());

    if (!check || nSerpentVal.GetType() == 0) {
        _MESSAGE("ScaleformCallback: not MorrowindDisplay.swf, ignoring");
        return true;
    }

    if (!pipboyActive) {
        return true;
    }

    _MESSAGE("ScaleformCallback: MorrowindDisplay + Pip-Boy active → launching OpenMW");
    InternalLaunch();
    return true;
}

// ---------------------------------------------------------------------------
// Watcher Process Management
// ---------------------------------------------------------------------------

static void StartWatcher() {
    if (g_openMWPath.empty()) return;

    // Derive watcher script path from the OpenMW/launcher directory
    std::string dir = g_openMWPath;
    auto slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir = dir.substr(0, slash);
    std::string script = dir + "\\morrowind_watcher.ps1";

    std::string pid = std::to_string(GetCurrentProcessId());
    std::string cmd = "powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden"
                      " -ExecutionPolicy Bypass -File \"" + script + "\""
                      " -FO4PID " + pid;
    char buf[2048];
    strncpy(buf, cmd.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    if (CreateProcessA(nullptr, buf, nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        g_hWatcher = pi.hProcess;
        CloseHandle(pi.hThread);
        _MESSAGE("Watcher started hidden (PID %u): %s", pi.dwProcessId, script.c_str());
    } else {
        _MESSAGE("Failed to start watcher (error %u): %s", GetLastError(), script.c_str());
    }
}

static void StopWatcher() {
    if (!g_hWatcher) return;
    TerminateProcess(g_hWatcher, 0);
    CloseHandle(g_hWatcher);
    g_hWatcher = nullptr;
    _MESSAGE("Watcher stopped");
}

// ---------------------------------------------------------------------------
// F4SE Message Handler
// ---------------------------------------------------------------------------

void OnF4SEMessage(F4SEMessagingInterface::Message* msg) {
    switch (msg->type) {
        case F4SEMessagingInterface::kMessage_GameDataReady:
            _MESSAGE("Game data ready");
            LoadConfig();
            StartWatcher();
            InstallWindowHook();
            InstallD3D11Hooks();
            break;

        case F4SEMessagingInterface::kMessage_GameLoaded:
            _MESSAGE("Game loaded");
            break;
    }
}

// ---------------------------------------------------------------------------
// Exported functions
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport) bool MorrowindLauncher_Launch() {
    return Papyrus_Launch();
}

extern "C" __declspec(dllexport) bool MorrowindLauncher_Shutdown() {
    return Papyrus_Shutdown();
}

// ---------------------------------------------------------------------------
// F4SE Plugin Entry Points
// ---------------------------------------------------------------------------

extern "C" {

__declspec(dllexport) bool F4SEPlugin_Query(const F4SEInterface* f4se, PluginInfo* info) {
    char logPath[MAX_PATH];
    GetEnvironmentVariableA("USERPROFILE", logPath, MAX_PATH);
    std::string logFile = std::string(logPath) +
        "\\Documents\\My Games\\Fallout4\\F4SE\\MorrowindLauncher.log";
    g_log.Open(logFile.c_str());

    _MESSAGE("=== MorrowindLauncher Query (D3D11 renderer) ===");
    _MESSAGE("Runtime version: 0x%08X", f4se->runtimeVersion);

    info->infoVersion = PluginInfo::kInfoVersion;
    info->name        = "MorrowindLauncher";
    info->version     = 2;

    g_pluginHandle = f4se->GetPluginHandle();
    return true;
}

__declspec(dllexport) bool F4SEPlugin_Load(const F4SEInterface* f4se) {
    if (!g_log.m_file) {
        char logPath[MAX_PATH];
        GetEnvironmentVariableA("USERPROFILE", logPath, MAX_PATH);
        std::string logFile = std::string(logPath) +
            "\\Documents\\My Games\\Fallout4\\F4SE\\MorrowindLauncher.log";
        g_log.Open(logFile.c_str());
    }

    _MESSAGE("=== MorrowindLauncher Load (D3D11 renderer) ===");
    _MESSAGE("F4SE version: 0x%08X, runtime: 0x%08X", f4se->f4seVersion, f4se->runtimeVersion);

    g_pluginHandle = f4se->GetPluginHandle();
    _MESSAGE("Plugin handle: %u", g_pluginHandle);

    g_messaging = (F4SEMessagingInterface*)f4se->QueryInterface(kInterface_Messaging);
    _MESSAGE("Messaging interface: %p", g_messaging);

    if (!g_messaging) {
        _MESSAGE("FATAL: Could not get messaging interface");
        return false;
    }

    g_messaging->RegisterListener(g_pluginHandle, "F4SE", OnF4SEMessage);

    g_papyrus = (F4SEPapyrusInterface*)f4se->QueryInterface(kInterface_Papyrus);
    _MESSAGE("Papyrus interface: %p", g_papyrus);
    if (g_papyrus) {
        g_papyrus->Register(PapyrusRegisterFunctions);
    } else {
        _MESSAGE("WARNING: Could not get Papyrus interface — native functions unavailable");
    }

    g_scaleform = (F4SEScaleformInterface*)f4se->QueryInterface(kInterface_Scaleform);
    _MESSAGE("Scaleform interface: %p", g_scaleform);
    if (g_scaleform) {
        g_scaleform->Register("MorrowindDisplay", ScaleformCallback);
        _MESSAGE("Scaleform callback registered");
    } else {
        _MESSAGE("WARNING: Could not get Scaleform interface — SWF auto-launch unavailable");
    }

    _MESSAGE("Plugin loaded successfully");
    return true;
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            RemoveWindowHook();
            CloseSharedMemory();
            StopWatcher();
            g_log.Close();
            break;
    }
    return TRUE;
}
