#pragma once

// ============================================================================
// papyrus_native.h — Manual Papyrus native function registration
//
// F4SE's NativeFunction templates require MSVC due to deep game type
// dependencies. This file provides a cross-compilable alternative that
// constructs ABI-compatible NativeFunction objects by:
//
//   1. Calling the game's own NativeFunction constructor (at a known address)
//      to handle BSFixedString interning and parameter setup
//   2. Copying the game's vtable and replacing only the Run() entry
//      with our custom implementation
//   3. Registering the result with VirtualMachine::RegisterFunction
//
// This approach works because the game's constructor does all the heavy
// lifting — we only override the callback dispatch.
//
// Target: FO4 1.10.163 + F4SE 0.6.23
// ============================================================================

#include "f4se_stubs.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Game function addresses (relative to Fallout4.exe base)
// Offsets for FO4 runtime 1.11.191 (next-gen, F4SE 0.7.7)
// ---------------------------------------------------------------------------

namespace GameAddr {
    // NativeFunction::Impl_ctor(fnName, className, isStatic, numParams)
    // FO4 1.11.191 (next-gen, F4SE 0.7.7)
    constexpr uintptr_t NativeFunction_Ctor  = 0x020F9D00;

    // VirtualMachine::RegisterFunction vtable index
    constexpr int VM_RegisterFunction_Index  = 27;
}

// ---------------------------------------------------------------------------
// VMValue — minimal ABI-compatible layout for setting return values
// ---------------------------------------------------------------------------

struct VMValueCompat {
    uint64_t type;  // 0x00 — kType_None=0, kType_Bool=5, etc.
    union {
        bool     b;
        int32_t  i;
        float    f;
        void*    p;
    } data;         // 0x08
};

static_assert(sizeof(VMValueCompat) == 16, "VMValue must be 16 bytes");

// Forward declarations for opaque game types
struct VMState;

// ---------------------------------------------------------------------------
// NativeFunction memory layout (0x58 bytes)
// Must match MSVC struct layout exactly
// ---------------------------------------------------------------------------

#pragma pack(push, 8)
struct NativeFunctionCompat {
    void*       vtable;       // 0x00 — pointer to vtable array

    uint32_t    refCount;     // 0x08 — BSIntrusiveRefCounted
    uint32_t    pad0C;        // 0x0C

    // BSFixedString is a single pointer (StringCache::Entry*)
    void*       m_fnName;     // 0x10
    void*       m_className;  // 0x18
    void*       m_unk20;      // 0x20

    uint64_t    m_retnType;   // 0x28

    // ParameterInfo
    void*       m_paramsData; // 0x30 — ParameterInfo::Entry*
    uint16_t    m_numParams;  // 0x38
    uint16_t    m_realNumParams; // 0x3A
    uint32_t    m_paramsPad;  // 0x3C

    bool        m_isStatic;   // 0x40
    uint8_t     m_unk41;      // 0x41
    bool        m_isLatent;   // 0x42
    uint8_t     m_pad43;      // 0x43
    uint32_t    m_unk44;      // 0x44
    void*       m_unk48;      // 0x48 — BSFixedString

    void*       m_callback;   // 0x50
};
#pragma pack(pop)

static_assert(sizeof(NativeFunctionCompat) == 0x58, "NativeFunction must be 0x58 bytes");

// ---------------------------------------------------------------------------
// Callback type for zero-argument static functions returning bool
// ---------------------------------------------------------------------------

typedef bool (*PapyrusBoolCallback)();

// ---------------------------------------------------------------------------
// Custom Run() implementation
//
// MSVC x64 vtable calling convention:
//   RCX = this, RDX = baseValue, R8 = vm, R9 = stackId
//   [RSP+0x28] = resultValue, [RSP+0x30] = state
//
// On mingw-w64 targeting Windows x64, the calling convention is identical.
// ---------------------------------------------------------------------------

// We store per-function state in a simple global lookup since we only
// register a handful of functions
struct NativeFuncEntry {
    NativeFunctionCompat* obj;
    PapyrusBoolCallback   callback;
};

// Support up to 8 native functions
static NativeFuncEntry g_nativeFuncs[8] = {};
static int g_nativeFuncCount = 0;

static PapyrusBoolCallback FindCallbackForObj(void* obj) {
    for (int i = 0; i < g_nativeFuncCount; i++) {
        if (g_nativeFuncs[i].obj == obj) return g_nativeFuncs[i].callback;
    }
    return nullptr;
}

// The Run() function that replaces vtable[22]
// Signature must match: bool Run(this, VMValue* base, VirtualMachine* vm,
//                                uint32_t stackId, VMValue* result, VMState* state)
static bool CustomRun(void* _this, void* baseValue, void* vm,
                      uint32_t stackId, VMValueCompat* resultValue, VMState* state) {
    auto cb = FindCallbackForObj(_this);
    bool result = cb ? cb() : false;

    // Pack bool result into VMValue
    resultValue->type   = 5; // kType_Bool
    resultValue->data.b = result;

    return true;
}

// ---------------------------------------------------------------------------
// Register a zero-arg bool-returning static Papyrus native function
//
// This is the main entry point. Call it from the F4SE Papyrus registration
// callback with the VirtualMachine pointer.
// ---------------------------------------------------------------------------

// Static storage for NativeFunction objects — avoids game heap entirely.
// The game never frees these (they live for the process lifetime), so
// static allocation is safe.
static NativeFunctionCompat s_nativeFuncStorage[8];
static uintptr_t            g_customVtables[8][24];

static bool RegisterNativeBoolFunction(
    VirtualMachine* vm,
    const char* fnName,
    const char* className,
    PapyrusBoolCallback callback)
{
    if (g_nativeFuncCount >= 8) return false;

    uintptr_t fo4Base = (uintptr_t)GetModuleHandleA("Fallout4.exe");
    if (!fo4Base) { _MESSAGE("RegisterNative: Fallout4.exe base not found"); return false; }

    _MESSAGE("RegisterNative: registering %s::%s fo4Base=0x%p", className, fnName, (void*)fo4Base);

    // Step 1: Use static storage instead of the game heap.
    // The game's custom heap offset changes between versions; static memory is
    // always valid and the game never needs to free these objects.
    int idx = g_nativeFuncCount;
    NativeFunctionCompat* fn = &s_nativeFuncStorage[idx];
    memset(fn, 0, sizeof(NativeFunctionCompat));
    _MESSAGE("RegisterNative: using static fn at %p", fn);

    // Step 2: Call game NativeFunction constructor to intern BSFixedStrings
    // and initialise the vtable pointer.
    const char* fnStr  = fnName;
    const char* clsStr = className;
    typedef void* (*NativeFnCtor_t)(void* _this, const char** fn, const char** cls,
                                     uint32_t isStatic, uint32_t numParams);
    auto GameCtor = (NativeFnCtor_t)(fo4Base + GameAddr::NativeFunction_Ctor);
    __try { GameCtor(fn, &fnStr, &clsStr, 1, 0); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        _MESSAGE("RegisterNative: EXCEPTION in NativeFunction_Ctor for %s::%s", className, fnName);
        return false;
    }
    _MESSAGE("RegisterNative: ctor OK, vtable=%p", fn->vtable);
    if (!fn->vtable) { _MESSAGE("RegisterNative: vtable is null after ctor"); return false; }

    // Step 3: Copy game vtable, replace Run() at index 22 with our dispatcher
    uintptr_t* origVtable = (uintptr_t*)fn->vtable;
    __try { memcpy(g_customVtables[idx], origVtable, sizeof(uintptr_t) * 23); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        _MESSAGE("RegisterNative: EXCEPTION copying vtable"); return false;
    }
    g_customVtables[idx][22] = (uintptr_t)&CustomRun;
    fn->vtable     = g_customVtables[idx];
    fn->m_callback = (void*)callback;
    fn->m_retnType = 5; // kType_Bool

    g_nativeFuncs[idx].obj      = fn;
    g_nativeFuncs[idx].callback = callback;
    g_nativeFuncCount++;
    _MESSAGE("RegisterNative: vtable patched, calling RegisterFunction vtable[%d]",
             GameAddr::VM_RegisterFunction_Index);

    // Step 4: Register with the VM
    uintptr_t* vmVtable = *(uintptr_t**)vm;
    typedef void (*VM_RegisterFunction_t)(void* _this, void* fn);
    auto RegisterFunction = (VM_RegisterFunction_t)vmVtable[GameAddr::VM_RegisterFunction_Index];
    __try { RegisterFunction(vm, fn); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        _MESSAGE("RegisterNative: EXCEPTION in RegisterFunction"); return false;
    }

    _MESSAGE("RegisterNative: %s::%s registered OK", className, fnName);
    return true;
}
