/*
 * pipboy_capture.c  --  Hollo-Wind Windows SDL2 proxy (Linux-compatible approach)
 *
 * Mirrors the Linux libpipboy_capture.so approach:
 *   - Intercepts SDL_GL_SwapWindow
 *   - Uses SDL_GL_GetDrawableSize for actual framebuffer dimensions
 *   - Reads RGBA pixels from OpenGL framebuffer
 *   - Nearest-neighbour scales to PIPBOY_WIDTH x PIPBOY_HEIGHT
 *   - Flips Y (GL bottom-up -> top-down)
 *   - Writes to "HolloWindBridge" named shared memory
 *
 * Bridge layout matches MorrowindBridge.h / pipboy_bridge.hpp exactly:
 *   [0]                frameBuffer[876*700*4]  RGBA top-to-bottom
 *   [FRAME_BUFFER_SIZE] frameID (uint64_t)
 *   [FRAME_BUFFER_SIZE+8] morrowindReady (uint8_t)
 *   [FRAME_BUFFER_SIZE+9] shutdownRequest (uint8_t)
 *   [FRAME_BUFFER_SIZE+10] _pad[6]
 *
 * Build: see build.bat
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIPBOY_WIDTH       876
#define PIPBOY_HEIGHT      700
#define PIPBOY_BPP         4
#define FRAME_BUFFER_SIZE  (PIPBOY_WIDTH * PIPBOY_HEIGHT * PIPBOY_BPP)  /* 2,452,800 */
#define BRIDGE_NAME        "MorrowindPipboyBridge"
#define BRIDGE_TOTAL_SIZE  (FRAME_BUFFER_SIZE + 292) /* 16 hdr + 272 BridgeInput + 4 pad */

/* Must match MorrowindBridge.h BridgeInput exactly */
typedef struct {
    float    moveX;
    float    moveY;
    float    lookX;
    float    lookY;
    uint8_t  keyStates[256]; /* indexed by Windows VK code */
    uint8_t  attack;
    uint8_t  jump;
    uint8_t  use;
    uint8_t  _ipad;
} BridgeInput; /* 272 bytes */

typedef struct {
    uint8_t           frameBuffer[FRAME_BUFFER_SIZE];
    volatile uint64_t frameID;
    volatile uint8_t  morrowindReady;
    uint8_t           shutdownRequest;
    uint8_t           _pad[6];
    BridgeInput       input;
} HolloWindBridge;

/* ---- SDL2 event injection ---------------------------------------- */
/* SDL event types */
#define SDL_WINDOWEVENT      0x00000200u
#define SDL_KEYDOWN          0x00000300u
#define SDL_KEYUP            0x00000301u
#define SDL_TEXTINPUT        0x00000303u
#define SDL_MOUSEMOTION      0x00000400u
#define SDL_MOUSEBUTTONDOWN  0x00000401u
#define SDL_MOUSEBUTTONUP    0x00000402u
#define SDL_PRESSED          1
#define SDL_RELEASED         0
#define SDL_BUTTON_LEFT      1
#define SDL_BUTTON_RIGHT     3
#define SDLK_SCANCODE_MASK   0x40000000u
#define SDL_WINDOWEVENT_SHOWN         1
#define SDL_WINDOWEVENT_FOCUS_GAINED 12

/* SDL_KeyboardEvent (32 bytes, fits in 56-byte SDL_Event union) */
typedef struct {
    uint32_t type;
    uint32_t timestamp;
    uint32_t windowID;
    uint8_t  state;
    uint8_t  repeat;
    uint8_t  pad2;
    uint8_t  pad3;
    uint32_t scancode;
    int32_t  sym;
    uint16_t mod;
    uint16_t _kpad;
    uint32_t unused;
} SdlKeyEvent;

/* SDL_MouseMotionEvent (36 bytes) */
typedef struct {
    uint32_t type;
    uint32_t timestamp;
    uint32_t windowID;
    uint32_t which;
    uint32_t state;
    int32_t  x, y;
    int32_t  xrel, yrel;
} SdlMouseMotionEvent;

/* SDL_MouseButtonEvent (28 bytes) */
typedef struct {
    uint32_t type;
    uint32_t timestamp;
    uint32_t windowID;
    uint32_t which;
    uint8_t  button;
    uint8_t  state;
    uint8_t  clicks;
    uint8_t  pad1;
    int32_t  x, y;
} SdlMouseButtonEvent;

/* SDL_TextInputEvent */
typedef struct {
    uint32_t type;
    uint32_t timestamp;
    uint32_t windowID;
    char     text[32];
} SdlTextInputEvent;

/* SDL_WindowEvent */
typedef struct {
    uint32_t type;
    uint32_t timestamp;
    uint32_t windowID;
    uint8_t  event;
    uint8_t  pad1, pad2, pad3;
    int32_t  data1, data2;
} SdlWindowEvent;

/* Windows VK code → SDL scancode + keysym.
   scan==0 means no mapping (skip). Mouse buttons (VK 1,2) handled separately. */
typedef struct { uint32_t scan; int32_t sym; } VkSdlEntry;
#define SC(s,k) {(s),(k)}
#define SM(s)   {(s),(int32_t)(SDLK_SCANCODE_MASK|(s))}
static const VkSdlEntry g_vk_sdl[256] = {
    /* 0x08 VK_BACK */    [  8]=SC(42,'\b'),
    /* 0x0D VK_RETURN */  [ 13]=SC(40,'\r'),
    /* 0x10 VK_SHIFT */   [ 16]=SM(225),
    /* 0x11 VK_CONTROL */ [ 17]=SM(224),
    /* 0x12 VK_MENU */    [ 18]=SM(226),
    /* 0x13 VK_PAUSE */   [ 19]=SM( 72),
    /* 0x14 VK_CAPITAL */ [ 20]=SM( 57),
    /* 0x1B VK_ESCAPE */  [ 27]=SC(41,0x1B),
    /* 0x20 VK_SPACE */   [ 32]=SC(44,' '),
    /* 0x21 VK_PRIOR */   [ 33]=SM( 75),
    /* 0x22 VK_NEXT */    [ 34]=SM( 78),
    /* 0x23 VK_END */     [ 35]=SM( 77),
    /* 0x24 VK_HOME */    [ 36]=SM( 74),
    /* 0x25 VK_LEFT */    [ 37]=SM( 80),
    /* 0x26 VK_UP */      [ 38]=SM( 82),
    /* 0x27 VK_RIGHT */   [ 39]=SM( 79),
    /* 0x28 VK_DOWN */    [ 40]=SM( 81),
    /* 0x2D VK_INSERT */  [ 45]=SM( 73),
    /* 0x2E VK_DELETE */  [ 46]=SM( 76),
    /* 0x30-0x39 0-9 */
    [48]=SC(39,'0'),[49]=SC(30,'1'),[50]=SC(31,'2'),[51]=SC(32,'3'),[52]=SC(33,'4'),
    [53]=SC(34,'5'),[54]=SC(35,'6'),[55]=SC(36,'7'),[56]=SC(37,'8'),[57]=SC(38,'9'),
    /* 0x41-0x5A A-Z (sym=lowercase) */
    [65]=SC( 4,'a'),[66]=SC( 5,'b'),[67]=SC( 6,'c'),[68]=SC( 7,'d'),[69]=SC( 8,'e'),
    [70]=SC( 9,'f'),[71]=SC(10,'g'),[72]=SC(11,'h'),[73]=SC(12,'i'),[74]=SC(13,'j'),
    [75]=SC(14,'k'),[76]=SC(15,'l'),[77]=SC(16,'m'),[78]=SC(17,'n'),[79]=SC(18,'o'),
    [80]=SC(19,'p'),[81]=SC(20,'q'),[82]=SC(21,'r'),[83]=SC(22,'s'),[84]=SC(23,'t'),
    [85]=SC(24,'u'),[86]=SC(25,'v'),[87]=SC(26,'w'),[88]=SC(27,'x'),[89]=SC(28,'y'),
    [90]=SC(29,'z'),
    /* numpad */
    [ 96]=SM( 98),[ 97]=SM( 89),[ 98]=SM( 90),[ 99]=SM( 91),[100]=SM( 92),
    [101]=SM( 93),[102]=SM( 94),[103]=SM( 95),[104]=SM( 96),[105]=SM( 97),
    [106]=SM( 85),[107]=SM( 87),[109]=SM( 86),[110]=SM( 99),[111]=SM( 84),
    /* F1-F12 */
    [112]=SM(58),[113]=SM(59),[114]=SM(60),[115]=SM(61),[116]=SM(62),[117]=SM(63),
    [118]=SM(64),[119]=SM(65),[120]=SM(66),[121]=SM(67),[122]=SM(68),[123]=SM(69),
    /* misc */
    [144]=SM(83), /* NUMLOCK */
    [145]=SM(71), /* SCROLLLOCK */
    /* extended modifiers */
    [160]=SM(225),[161]=SM(229),[162]=SM(224),[163]=SM(228),[164]=SM(226),[165]=SM(230),
    /* OEM keys (US layout) */
    [186]=SC(51,';'),[187]=SC(46,'='),[188]=SC(54,','),[189]=SC(45,'-'),[190]=SC(55,'.'),
    [191]=SC(56,'/'),[192]=SC(53,'`'),[219]=SC(47,'['),[220]=SC(49,'\\'),
    [221]=SC(48,']'),[222]=SC(52,'\''),
};
#undef SC
#undef SM

static HMODULE         g_sdl2_real = NULL;
static HANDLE          g_map_handle = NULL;
static HolloWindBridge *g_bridge   = NULL;
static uint8_t        *g_raw_buf   = NULL;
static int             g_raw_w     = 0;
static int             g_raw_h     = 0;

typedef void  (WINAPI *PFNGLGETINTEGERV)(unsigned int pname, int *params);
typedef void  (WINAPI *PFNGLREADPIXELS)(int x, int y, int w, int h,
                                         unsigned int fmt, unsigned int type,
                                         void *pixels);
typedef void  (__cdecl *PFN_SDL_GL_SwapWindow)(void *window);
typedef int   (__cdecl *PFN_SDL_PollEvent)(void *event);
typedef void  (__cdecl *PFN_SDL_GL_GetDrawableSize)(void *window, int *w, int *h);
typedef void* (__cdecl *PFN_SDL_CreateWindow)(const char *title, int x, int y, int w, int h, uint32_t flags);
typedef int   (__cdecl *PFN_SDL_PushEvent)(void *event);
typedef uint32_t (__cdecl *PFN_SDL_GetWindowID)(void *window);
typedef uint32_t (__cdecl *PFN_SDL_GetTicks)(void);
typedef int   (__cdecl *PFN_SDL_SetRelativeMouseMode)(int enabled);
typedef void  (__cdecl *PFN_SDL_WarpMouseInWindow)(void *window, int x, int y);

static PFNGLGETINTEGERV          g_glGetIntegerv          = NULL;
static PFNGLREADPIXELS           g_glReadPixels           = NULL;
static PFN_SDL_GL_SwapWindow     g_real_SDL_GL_SwapWindow = NULL;
static PFN_SDL_PollEvent         g_real_SDL_PollEvent     = NULL;
static PFN_SDL_GL_GetDrawableSize g_SDL_GL_GetDrawableSize = NULL;
static PFN_SDL_CreateWindow      g_real_SDL_CreateWindow  = NULL;
static PFN_SDL_PushEvent         g_SDL_PushEvent          = NULL;
static PFN_SDL_GetWindowID       g_SDL_GetWindowID        = NULL;
static PFN_SDL_GetTicks          g_SDL_GetTicks           = NULL;

static uint32_t g_windowID      = 0;
static uint8_t  g_prev_states[256];
static int      g_shift_held    = 0;
static float    g_mouse_x       = 438.0f;
static float    g_mouse_y       = 350.0f;
static int      g_sent_focus     = 0;
static int      g_relative_mode  = 0; /* tracks requested SDL relative mouse mode */
static int      g_cursor_visible = 1; /* tracks SDL_ShowCursor state: 1=GUI mode, 0=gameplay */

/* ---- Software cursor painted directly into raw pixel buffer ------------ */
/* 14x14 arrow cursor, pointing top-left. 'X'=white pixel, ' '=transparent */
#define CURSOR_W 14
#define CURSOR_H 14
static const char CURSOR_SHAPE[CURSOR_H][CURSOR_W + 1] = {
    "X             ",
    "XX            ",
    "XXX           ",
    "XXXX          ",
    "XXXXX         ",
    "XXXXXX        ",
    "XXXXXXX       ",
    "XXXXXXXX      ",
    "XXXXXXXXX     ",
    "XXXXXX        ",
    "XXX  XX       ",
    "XX    XX      ",
    "       XX     ",
    "       X      ",
};

/* SDL_WINDOW_HIDDEN = 0x00000008 — keep OpenMW off-screen */
#define SDL_WINDOW_HIDDEN_FLAG 0x00000008u

static FILE *g_log = NULL;
#define LOG(...) do { if(g_log){ fprintf(g_log, "[pipboy] " __VA_ARGS__); fflush(g_log); } } while(0)

/* Paint the arrow cursor into the raw GL pixel buffer (bottom-up, RGBA).
   Only draws when SDL_ShowCursor has been called with SDL_ENABLE (GUI/menu mode). */
static void paint_cursor(int srcW, int srcH) {
    if (!g_cursor_visible) return;
    int cx = (int)g_mouse_x;
    int cy = (int)g_mouse_y;
    for (int row = 0; row < CURSOR_H; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            if (CURSOR_SHAPE[row][col] != 'X') continue;
            int px = cx + col;
            int py = cy + row;
            if (px < 0 || px >= srcW || py < 0 || py >= srcH) continue;
            /* GL raw buffer is bottom-up: flip Y */
            int gy = srcH - 1 - py;
            uint8_t *p = g_raw_buf + ((size_t)gy * srcW + px) * 4;
            p[0] = 255; p[1] = 255; p[2] = 255; p[3] = 255;
        }
    }
}

static int open_bridge(void)
{
    g_map_handle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                      0, BRIDGE_TOTAL_SIZE, BRIDGE_NAME);
    if (!g_map_handle) {
        LOG("CreateFileMapping failed: %lu\n", GetLastError());
        return 0;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        LOG("Attached to existing mapping\n");

    g_bridge = (HolloWindBridge *)MapViewOfFile(g_map_handle,
                                                FILE_MAP_READ | FILE_MAP_WRITE,
                                                0, 0, BRIDGE_TOTAL_SIZE);
    if (!g_bridge) {
        LOG("MapViewOfFile failed: %lu\n", GetLastError());
        CloseHandle(g_map_handle);
        return 0;
    }
    // Clear stale state from any previous session — a force-killed OpenMW won't
    // run DLL_PROCESS_DETACH, so the mapping can persist with shutdownRequest=1.
    // If SDL_PollEvent sees that stale flag it returns SDL_QUIT on every call,
    // which kills OpenMW mid-load and looks like a freeze.
    g_bridge->shutdownRequest = 0;
    memset(&g_bridge->input, 0, sizeof(g_bridge->input));
    g_bridge->frameID = 0;
    g_bridge->morrowindReady = 1;
    LOG("Bridge ready: %s (%u bytes)\n", BRIDGE_NAME, BRIDGE_TOTAL_SIZE);
    return 1;
}

static void close_bridge(void)
{
    if (g_bridge) {
        g_bridge->morrowindReady = 0;
        UnmapViewOfFile(g_bridge);
        g_bridge = NULL;
    }
    if (g_map_handle) { CloseHandle(g_map_handle); g_map_handle = NULL; }
}

static void capture_frame(void *window)
{
    if (!g_bridge || !g_glReadPixels) return;

    /* Get actual framebuffer size */
    int srcW = 0, srcH = 0;
    if (g_SDL_GL_GetDrawableSize)
        g_SDL_GL_GetDrawableSize(window, &srcW, &srcH);
    if (srcW <= 0 || srcH <= 0) {
        if (g_glGetIntegerv) {
            int vp[4] = {0};
            g_glGetIntegerv(0x0BA2 /* GL_VIEWPORT */, vp);
            srcW = vp[2]; srcH = vp[3];
        }
    }
    if (srcW <= 0 || srcH <= 0) return;

    /* Allocate raw capture buffer if size changed */
    if (!g_raw_buf || g_raw_w != srcW || g_raw_h != srcH) {
        free(g_raw_buf);
        g_raw_buf = (uint8_t *)malloc((size_t)srcW * srcH * 4);
        if (!g_raw_buf) return;
        g_raw_w = srcW;
        g_raw_h = srcH;
        LOG("Capture size: %dx%d\n", srcW, srcH);
    }

    /* Read full framebuffer RGBA (GL bottom-up) */
    g_glReadPixels(0, 0, srcW, srcH, 0x1908u /* GL_RGBA */, 0x1401u /* GL_UNSIGNED_BYTE */, g_raw_buf);

    /* Paint software cursor directly into the raw buffer before scaling */
    paint_cursor(srcW, srcH);

    /* Scale to PIPBOY_WIDTH x PIPBOY_HEIGHT, flipping Y (GL bottom-up -> top-down) */
    for (int dstY = 0; dstY < PIPBOY_HEIGHT; dstY++) {
        /* flip: dstY=0 maps to srcY=srcH-1 (GL top = last row) */
        int srcY = (PIPBOY_HEIGHT - 1 - dstY) * srcH / PIPBOY_HEIGHT;
        if (srcY < 0) srcY = 0;
        if (srcY >= srcH) srcY = srcH - 1;
        uint8_t *dst = g_bridge->frameBuffer + (size_t)dstY * PIPBOY_WIDTH * 4;
        for (int dstX = 0; dstX < PIPBOY_WIDTH; dstX++) {
            int srcX = dstX * srcW / PIPBOY_WIDTH;
            if (srcX >= srcW) srcX = srcW - 1;
            memcpy(dst + dstX * 4, g_raw_buf + ((size_t)srcY * srcW + srcX) * 4, 4);
        }
    }

    /* frameID increment moved to SDL_GL_SwapWindow, after cursor overlay is drawn,
       so main.cpp never copies the buffer before the cursor pixels are written. */

    /* Log every 3 seconds */
    static ULONGLONG last_log = 0;
    ULONGLONG now = GetTickCount64();
    if (now - last_log >= 3000) {
        LOG("frame #%llu  src=%dx%d -> %dx%d\n",
            (unsigned long long)g_bridge->frameID, srcW, srcH, PIPBOY_WIDTH, PIPBOY_HEIGHT);
        last_log = now;
    }
}

__declspec(dllexport)
void* __cdecl SDL_CreateWindow(const char *title, int x, int y, int w, int h, uint32_t flags)
{
    // Strip fullscreen flags — SDL2/NVIDIA deadlocks trying to change display
    // mode on a hidden window. We only need a GL context, not a real display.
    flags &= ~0x00000001u; /* SDL_WINDOW_FULLSCREEN */
    flags &= ~0x00001000u; /* SDL_WINDOW_FULLSCREEN_DESKTOP upper bit */
    flags |= SDL_WINDOW_HIDDEN_FLAG;
    LOG("SDL_CreateWindow: %dx%d flags=0x%X (hidden+windowed forced)\n", w, h, flags);
    return g_real_SDL_CreateWindow ? g_real_SDL_CreateWindow(title, x, y, w, h, flags) : NULL;
}

__declspec(dllexport)
void __cdecl SDL_ShowWindow(void *window)
{
    (void)window;
    LOG("SDL_ShowWindow: suppressed\n");
}

__declspec(dllexport)
void __cdecl SDL_RaiseWindow(void *window)
{
    (void)window;
}

__declspec(dllexport)
int __cdecl SDL_SetRelativeMouseMode(int enabled)
{
    /* Track the requested state so OpenMW's cursor/mode logic works correctly.
       Don't call the real function — SDL's raw input won't work on a hidden window. */
    g_relative_mode = enabled ? 1 : 0;
    LOG("SDL_SetRelativeMouseMode(%d)\n", enabled);
    return 0;
}

__declspec(dllexport)
int __cdecl SDL_GetRelativeMouseMode(void)
{
    return g_relative_mode;
}

__declspec(dllexport)
int __cdecl SDL_ShowCursor(int toggle)
{
    /* SDL_QUERY=-1, SDL_DISABLE=0, SDL_ENABLE=1 */
    if (toggle >= 0) {
        g_cursor_visible = (toggle != 0) ? 1 : 0;
        LOG("SDL_ShowCursor(%d) cursor=%s\n", toggle, g_cursor_visible ? "SHOWN" : "HIDDEN");
    }
    return g_cursor_visible;
}

__declspec(dllexport)
void __cdecl SDL_WarpMouseInWindow(void *window, int x, int y)
{
    /* No-op: we own cursor position via g_mouse_x/g_mouse_y */
    (void)window; (void)x; (void)y;
}

static void process_bridge_input(void *window)
{
    if (!g_bridge || !g_SDL_PushEvent) return;

    if (g_windowID == 0 && g_SDL_GetWindowID && window)
        g_windowID = g_SDL_GetWindowID(window);

    uint32_t ts = g_SDL_GetTicks ? g_SDL_GetTicks() : 0;
    int i;

    /* On first frame: tell OpenMW the window is visible and focused so it
       enables input processing. Without this, the hidden window never gets
       a focus event and OpenMW ignores all mouse input. */
    if (!g_sent_focus && g_windowID) {
        g_sent_focus = 1;
        uint8_t ev[56];
        memset(ev, 0, sizeof(ev));
        SdlWindowEvent *we = (SdlWindowEvent*)ev;
        we->type = SDL_WINDOWEVENT; we->timestamp = ts; we->windowID = g_windowID;
        we->event = SDL_WINDOWEVENT_SHOWN;
        g_SDL_PushEvent(ev);
        memset(ev, 0, sizeof(ev));
        we->type = SDL_WINDOWEVENT; we->timestamp = ts; we->windowID = g_windowID;
        we->event = SDL_WINDOWEVENT_FOCUS_GAINED;
        g_SDL_PushEvent(ev);
        LOG("Injected SHOWN + FOCUS_GAINED for windowID=%u\n", g_windowID);
    }

    /* Update shift state for TEXTINPUT character case */
    g_shift_held = g_bridge->input.keyStates[0xA0] || /* VK_LSHIFT */
                   g_bridge->input.keyStates[0xA1] || /* VK_RSHIFT */
                   g_bridge->input.keyStates[0x10];   /* VK_SHIFT   */

    /* --- Iterate all VK codes, inject events for state changes --- */
    for (i = 0; i < 256; i++) {
        uint8_t cur  = g_bridge->input.keyStates[i];
        uint8_t prev = g_prev_states[i];
        if (cur == prev) continue;
        g_prev_states[i] = cur;
        int down = (cur != 0);

        /* Mouse buttons (VK_LBUTTON=1, VK_RBUTTON=2): inject mouse events */
        if (i == 1 || i == 2) {
            uint8_t ev[56];
            memset(ev, 0, sizeof(ev));
            SdlMouseButtonEvent *be = (SdlMouseButtonEvent*)ev;
            be->type    = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            be->timestamp = ts;
            be->windowID  = g_windowID;
            be->button  = (i == 1) ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
            be->state   = down ? SDL_PRESSED : SDL_RELEASED;
            be->clicks  = 1;
            be->x       = (int32_t)g_mouse_x;
            be->y       = (int32_t)g_mouse_y;
            g_SDL_PushEvent(ev);
            continue;
        }

        /* Keyboard keys: map VK → SDL scancode+sym */
        {
            VkSdlEntry e = g_vk_sdl[i];
            if (e.scan == 0) continue;

            {
                uint8_t ev[56];
                memset(ev, 0, sizeof(ev));
                SdlKeyEvent *ke = (SdlKeyEvent*)ev;
                ke->type     = down ? SDL_KEYDOWN : SDL_KEYUP;
                ke->timestamp = ts;
                ke->windowID = g_windowID;
                ke->state    = down ? SDL_PRESSED : SDL_RELEASED;
                ke->scancode = e.scan;
                ke->sym      = e.sym;
                g_SDL_PushEvent(ev);
            }

            /* SDL_TEXTINPUT for printable characters on key-down */
            if (down && e.sym >= 32 && e.sym < 127) {
                char ch = (char)e.sym;
                /* Uppercase letters when shift held */
                if (ch >= 'a' && ch <= 'z' && g_shift_held) ch -= 32;
                uint8_t ev[56];
                memset(ev, 0, sizeof(ev));
                SdlTextInputEvent *te = (SdlTextInputEvent*)ev;
                te->type      = SDL_TEXTINPUT;
                te->timestamp = ts;
                te->windowID  = g_windowID;
                te->text[0]   = ch;
                g_SDL_PushEvent(ev);
            }
        }
    }

    /* --- Mouse motion --- */
    {
        float dx = g_bridge->input.lookX;
        float dy = g_bridge->input.lookY;
        if (dx != 0.0f || dy != 0.0f) {
            g_mouse_x += dx; if (g_mouse_x < 0.0f) g_mouse_x = 0.0f; if (g_mouse_x > 875.0f) g_mouse_x = 875.0f;
            g_mouse_y += dy; if (g_mouse_y < 0.0f) g_mouse_y = 0.0f; if (g_mouse_y > 699.0f) g_mouse_y = 699.0f;
            g_bridge->input.lookX = 0.0f;
            g_bridge->input.lookY = 0.0f;
        }
        /* Always inject position so OpenMW/MyGUI cursor stays current,
           even when the mouse isn't moving (needed for menu cursor to appear). */
        uint8_t ev[56];
        memset(ev, 0, sizeof(ev));
        SdlMouseMotionEvent *me = (SdlMouseMotionEvent*)ev;
        me->type      = SDL_MOUSEMOTION;
        me->timestamp = ts;
        me->windowID  = g_windowID;
        me->x         = (int32_t)g_mouse_x;
        me->y         = (int32_t)g_mouse_y;
        me->xrel      = (int32_t)dx;
        me->yrel      = (int32_t)dy;
        g_SDL_PushEvent(ev);
    }
}

__declspec(dllexport)
void __cdecl SDL_GL_SwapWindow(void *window)
{
    capture_frame(window);

    if (g_bridge)
        g_bridge->frameID++;


    process_bridge_input(window);
    if (g_real_SDL_GL_SwapWindow) g_real_SDL_GL_SwapWindow(window);
}

__declspec(dllexport)
int __cdecl SDL_PollEvent(void *event)
{
    // Inject SDL_QUIT when F4SE requests shutdown so OpenMW exits its main loop.
    // SDL_QUIT = 0x100; SDL_Event first field is uint32_t type (56 bytes total).
    if (g_bridge && g_bridge->shutdownRequest && event) {
        memset(event, 0, 56);
        *(uint32_t*)event = 0x100u; /* SDL_QUIT */
        return 1;
    }
    return g_real_SDL_PollEvent ? g_real_SDL_PollEvent(event) : 0;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved)
{
    (void)hInst; (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);

        CreateDirectoryA("C:\\tmp", NULL);
        g_log = fopen("C:\\tmp\\pipboy_capture.log", "w");
        LOG("DLL_PROCESS_ATTACH\n");

        char real_path[MAX_PATH];
        GetModuleFileNameA(hInst, real_path, MAX_PATH);
        char *last = strrchr(real_path, '\\');
        if (last) { last[1] = '\0'; strcat_s(real_path, MAX_PATH, "SDL2_orig.dll"); }
        else strcpy_s(real_path, MAX_PATH, "SDL2_orig.dll");

        g_sdl2_real = LoadLibraryA(real_path);
        if (!g_sdl2_real) {
            LOG("Failed to load SDL2_orig.dll (%lu)\n", GetLastError());
            return FALSE;
        }
        LOG("SDL2_orig.dll loaded\n");

        g_real_SDL_GL_SwapWindow  = (PFN_SDL_GL_SwapWindow)    GetProcAddress(g_sdl2_real, "SDL_GL_SwapWindow");
        g_real_SDL_PollEvent      = (PFN_SDL_PollEvent)         GetProcAddress(g_sdl2_real, "SDL_PollEvent");
        g_SDL_GL_GetDrawableSize  = (PFN_SDL_GL_GetDrawableSize)GetProcAddress(g_sdl2_real, "SDL_GL_GetDrawableSize");
        g_real_SDL_CreateWindow   = (PFN_SDL_CreateWindow)      GetProcAddress(g_sdl2_real, "SDL_CreateWindow");
        g_SDL_PushEvent           = (PFN_SDL_PushEvent)         GetProcAddress(g_sdl2_real, "SDL_PushEvent");
        g_SDL_GetWindowID         = (PFN_SDL_GetWindowID)       GetProcAddress(g_sdl2_real, "SDL_GetWindowID");
        g_SDL_GetTicks            = (PFN_SDL_GetTicks)          GetProcAddress(g_sdl2_real, "SDL_GetTicks");
        LOG("SDL_PushEvent=%p  SDL_GetWindowID=%p\n", (void*)g_SDL_PushEvent, (void*)g_SDL_GetWindowID);

        HMODULE gl = GetModuleHandleA("opengl32.dll");
        if (!gl) gl = LoadLibraryA("opengl32.dll");
        if (gl) {
            g_glGetIntegerv = (PFNGLGETINTEGERV)GetProcAddress(gl, "glGetIntegerv");
            g_glReadPixels  = (PFNGLREADPIXELS) GetProcAddress(gl, "glReadPixels");
        }
        LOG("glGetIntegerv=%p  glReadPixels=%p  SDL_GL_GetDrawableSize=%p\n",
            (void*)g_glGetIntegerv, (void*)g_glReadPixels, (void*)g_SDL_GL_GetDrawableSize);

        open_bridge();

    } else if (reason == DLL_PROCESS_DETACH) {
        close_bridge();
        free(g_raw_buf);
        g_raw_buf = NULL;
        if (g_sdl2_real) { FreeLibrary(g_sdl2_real); g_sdl2_real = NULL; }
        LOG("DLL_PROCESS_DETACH\n");
        if (g_log) { fclose(g_log); g_log = NULL; }
    }
    return TRUE;
}
