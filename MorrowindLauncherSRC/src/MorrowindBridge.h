#pragma once
#include <cstdint>
#include <cstring>

// ============================================================================
// MorrowindBridge.h — Shared memory layout between F4SE plugin and OpenMW
//
// Both the F4SE plugin and the patched OpenMW build include this header.
// The struct lives in a Win32 Named Shared Memory block called
// "MorrowindPipboyBridge". OpenMW creates it; F4SE opens it.
// ============================================================================

constexpr uint32_t PIPBOY_WIDTH  = 876;
constexpr uint32_t PIPBOY_HEIGHT = 700;
constexpr uint32_t PIPBOY_BPP    = 4; // RGBA
constexpr uint32_t FRAME_BUFFER_SIZE = PIPBOY_WIDTH * PIPBOY_HEIGHT * PIPBOY_BPP;

// Pip-Boy render target size — must match uPipboyTargetWidth/Height in Prefs.ini.
// Prefs.ini has 876x700 which exactly matches the SDL2 capture size, so injection
// is 1:1 with no scaling needed.
constexpr uint32_t PIPBOY_RT_WIDTH  = 876;
constexpr uint32_t PIPBOY_RT_HEIGHT = 700;

// No padding — RT and bridge frame are the same dimensions.
constexpr uint32_t PIPBOY_PAD_LEFT   = 0;
constexpr uint32_t PIPBOY_PAD_RIGHT  = 0;
constexpr uint32_t PIPBOY_PAD_TOP    = 0;
constexpr uint32_t PIPBOY_PAD_BOTTOM = 0;

// Named shared memory (Windows-only, used when both processes are in Wine)
constexpr const char* BRIDGE_NAME = "MorrowindPipboyBridge";

// File-backed shared memory path (fallback; primary IPC is named mapping above)
constexpr const char* BRIDGE_FILE_WIN = "C:\\tmp\\morrowind_pipboy_bridge";

// Input action bitmask flags — F4SE writes these, OpenMW reads them
namespace MWInput {
    constexpr uint32_t KEY_W         = (1 << 0);
    constexpr uint32_t KEY_A         = (1 << 1);
    constexpr uint32_t KEY_S         = (1 << 2);
    constexpr uint32_t KEY_D         = (1 << 3);
    constexpr uint32_t KEY_SPACE     = (1 << 4);  // jump
    constexpr uint32_t KEY_E         = (1 << 5);  // use/activate
    constexpr uint32_t KEY_LSHIFT    = (1 << 6);  // run
    constexpr uint32_t KEY_TAB       = (1 << 7);  // inventory
    constexpr uint32_t KEY_ESCAPE    = (1 << 8);  // menu
    constexpr uint32_t KEY_1         = (1 << 9);
    constexpr uint32_t KEY_2         = (1 << 10);
    constexpr uint32_t KEY_3        = (1 << 11);
    constexpr uint32_t KEY_4         = (1 << 12);
    constexpr uint32_t KEY_5         = (1 << 13);
    constexpr uint32_t KEY_6         = (1 << 14);
    constexpr uint32_t KEY_7         = (1 << 15);
    constexpr uint32_t KEY_8         = (1 << 16);
    constexpr uint32_t KEY_R         = (1 << 17); // ready weapon
    constexpr uint32_t KEY_F         = (1 << 18); // ready magic
    constexpr uint32_t KEY_J         = (1 << 19); // journal
    constexpr uint32_t KEY_LCTRL     = (1 << 20); // sneak
    constexpr uint32_t KEY_RETURN    = (1 << 21); // enter/confirm
    constexpr uint32_t KEY_BACKSPACE = (1 << 22); // backspace
    constexpr uint32_t KEY_UP        = (1 << 23);
    constexpr uint32_t KEY_DOWN      = (1 << 24);
    constexpr uint32_t KEY_LEFT      = (1 << 25);
    constexpr uint32_t KEY_RIGHT     = (1 << 26);
    constexpr uint32_t KEY_P         = (1 << 27); // settings menu
    constexpr uint32_t MOUSE_LEFT    = (1 << 28); // attack
    constexpr uint32_t MOUSE_RIGHT   = (1 << 29); // block
    constexpr uint32_t MOUSE_MIDDLE  = (1 << 30); // third person toggle
}

struct BridgeInput {
    float moveX;              // -1.0 (left) to 1.0 (right)  — computed from WASD
    float moveY;              // -1.0 (back) to 1.0 (forward) — computed from WASD
    float lookX;              // mouse delta X (yaw)
    float lookY;              // mouse delta Y (pitch)
    uint8_t keyStates[256];   // per-key state, indexed by Windows VK code (1=pressed, 0=released)
                              // VK_LBUTTON(1) and VK_RBUTTON(2) carry mouse button state
    bool attack;              // convenience: VK_LBUTTON
    bool jump;                // convenience: VK_SPACE
    bool use;                 // convenience: 'E'
    uint8_t _pad[1];
};

struct MorrowindBridge {
    // --- Frame data (written by OpenMW, read by F4SE) ---
    uint8_t  frameBuffer[FRAME_BUFFER_SIZE]; // raw RGBA pixels, 876x512
    uint64_t frameID;                        // incremented by OpenMW each frame

    // --- State flags ---
    bool morrowindReady;   // set true by OpenMW once rendering loop starts
    bool shutdownRequest;  // F4SE can set this to ask OpenMW to quit gracefully
    uint8_t _pad[6];

    // --- Input data (written by F4SE, read by OpenMW) ---
    BridgeInput input;
};

// Sanity check — the frame buffer dominates the struct size
static_assert(sizeof(MorrowindBridge) > FRAME_BUFFER_SIZE,
              "Bridge struct must contain the full frame buffer");
