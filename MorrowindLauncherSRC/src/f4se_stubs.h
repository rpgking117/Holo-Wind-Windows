#pragma once

// ============================================================================
// f4se_stubs.h — Minimal type definitions needed for F4SE plugin compilation
//
// Instead of depending on the full F4SE source tree (which requires MSVC),
// we define only the types and interfaces our plugin actually uses. This
// allows cross-compilation with mingw-w64 from Linux.
//
// These definitions are ABI-compatible with F4SE 0.6.23 / FO4 1.10.163.
// ============================================================================

#include <cstdint>
#include <windows.h>

// --- Base types (matching F4SE's common/ITypes.h) ---
typedef uint8_t   UInt8;
typedef uint16_t  UInt16;
typedef uint32_t  UInt32;
typedef uint64_t  UInt64;
typedef int8_t    SInt8;
typedef int16_t   SInt16;
typedef int32_t   SInt32;
typedef int64_t   SInt64;

// --- FO4 runtime version packed ---
// F4SE encodes as: major<<24 | minor<<16 | build<<8 | sub
#define RUNTIME_VERSION_1_10_163  0x010AA300
#define RUNTIME_VERSION_1_11_191  0x010B0BF0

// --- Plugin API types (from f4se/PluginAPI.h) ---

typedef UInt32 PluginHandle;

enum { kPluginHandle_Invalid = 0xFFFFFFFF };

enum {
    kInterface_Invalid = 0,
    kInterface_Messaging,
    kInterface_Scaleform,
    kInterface_Papyrus,
    kInterface_Serialization,
    kInterface_Task,
    kInterface_Object,
    kInterface_Trampoline,
    kInterface_Max,
};

struct PluginInfo {
    enum { kInfoVersion = 1 };
    UInt32       infoVersion;
    const char*  name;
    UInt32       version;
};

struct F4SEInterface {
    UInt32       f4seVersion;
    UInt32       runtimeVersion;
    UInt32       editorVersion;
    UInt32       isEditor;
    void*        (*QueryInterface)(UInt32 id);
    PluginHandle (*GetPluginHandle)(void);
    UInt32       (*GetReleaseIndex)(void);
    const PluginInfo* (*GetPluginInfo)(const char* name);
};

// --- Messaging Interface ---

struct F4SEMessagingInterface {
    struct Message {
        const char* sender;
        UInt32      type;
        UInt32      dataLen;
        void*       data;
    };

    typedef void (*EventCallback)(Message* msg);

    enum { kInterfaceVersion = 1 };

    enum {
        kMessage_PostLoad,
        kMessage_PostPostLoad,
        kMessage_PreLoadGame,
        kMessage_PostLoadGame,
        kMessage_PreSaveGame,
        kMessage_PostSaveGame,
        kMessage_DeleteGame,
        kMessage_InputLoaded,
        kMessage_NewGame,
        kMessage_GameLoaded,
        kMessage_GameDataReady
    };

    UInt32 interfaceVersion;
    bool   (*RegisterListener)(PluginHandle listener, const char* sender, EventCallback handler);
    bool   (*Dispatch)(PluginHandle sender, UInt32 messageType, void* data, UInt32 dataLen, const char* receiver);
    void*  (*GetEventDispatcher)(UInt32 dispatcherId);
};

// --- Papyrus Interface ---

class VirtualMachine;

struct F4SEPapyrusInterface {
    enum { kInterfaceVersion = 2 };
    UInt32 interfaceVersion;
    typedef bool (*RegisterFunctions)(VirtualMachine* vm);
    bool (*Register)(RegisterFunctions callback);
    typedef void (*RegistrantFunctor)(UInt64 handle, const char* scriptName, const char* callbackName, void* data);
    void (*GetExternalEventRegistrations)(const char* eventName, void* data, RegistrantFunctor functor);
};

// --- Scaleform Interface ---

// GFxValue — standalone class matching F4SE's ScaleformValue.h layout (0x20 bytes)
class GFxValue {
public:
    enum {
        kType_Undefined = 0, kType_Null, kType_Bool, kType_Int,
        kType_UInt, kType_Number, kType_String, kType_Unknown7,
        kType_Object, kType_Array, kType_DisplayObject, kType_Function,
        kTypeFlag_Managed = 1 << 6,
        kMask_Type = 0x8F,
    };

    union Data {
        UInt32     u32;
        SInt32     s32;
        double     number;
        bool       boolean;
        const char* string;
        void*      obj;
    };

    void*   objectInterface; // 0x00
    UInt32  type;            // 0x08
    Data    data;            // 0x10
    void*   unk18;           // 0x18

    UInt32 GetType() const { return type & kMask_Type; }
    bool IsObject() const {
        auto t = GetType();
        return t == kType_Object || t == kType_Array || t == kType_DisplayObject;
    }
};

// GFxMovieRoot — virtual interface from F4SE ScaleformMovie.h
// Invoke is at vtable index 0x39; SetVariable at 0x31
//
// IMPORTANT: MSVC virtual destructors use 1 vtable slot, but GCC uses 2.
// Since FO4 is MSVC-compiled, we use a non-destructor placeholder to match.
class GFxMovieRoot;

class GFxFunctionHandler {
public:
    struct Args {
        GFxValue*       result;
        GFxMovieRoot*   movie;
        GFxValue*       thisObj;
        GFxValue*       args;
        UInt32          numArgs;
    };
    // No virtual destructor — Call must be at vtable[0] to match Scaleform ABI
    virtual void Call(const Args& args) = 0;
};

class GFxMovieRoot {
public:
    virtual void _msvc_destructor();
    virtual void Unk_01(); virtual void Unk_02(); virtual void Unk_03();
    virtual void Unk_04(); virtual void Unk_05(); virtual void Unk_06();
    virtual void Unk_07(); virtual void Unk_08(); virtual void Unk_09();
    virtual void Unk_0A(); virtual void Unk_0B(); virtual void Unk_0C();
    virtual void Unk_0D(); virtual void Unk_0E(); virtual void Unk_0F();
    virtual void Unk_10(); virtual void Unk_11(); virtual void Unk_12();
    virtual void Unk_13(); virtual void Unk_14(); virtual void Unk_15();
    virtual void Unk_16(); virtual void Unk_17(); virtual void Unk_18();
    virtual void Unk_19(); virtual void Unk_1A(); virtual void Unk_1B();
    virtual void Unk_1C(); virtual void Unk_1D(); virtual void Unk_1E();
    virtual void Unk_1F(); virtual void Unk_20(); virtual void Unk_21();
    virtual void Unk_22(); virtual void Unk_23(); virtual void Unk_24();
    virtual void Unk_25(); virtual void Unk_26(); virtual void Unk_27();
    virtual void Unk_28(); virtual void Unk_29(); virtual void Unk_2A();
    virtual void Unk_2B();
    virtual void CreateString(GFxValue* pValue, const char* pString);       // 0x2C
    virtual void CreateStringW(GFxValue* pValue, const wchar_t* pString);   // 0x2D
    virtual void CreateObject(GFxValue* pValue, const char* className = nullptr,
                              const GFxValue* pArgs = nullptr, UInt32 nArgs = 0); // 0x2E
    virtual void CreateArray(GFxValue* pValue);                             // 0x2F
    virtual void CreateFunction(GFxValue* pValue, GFxFunctionHandler* pFunc,
                                void* puserData = nullptr);                 // 0x30
    virtual bool SetVariable(const char* pVarPath, const GFxValue* value, UInt32 setType = 0); // 0x31
    virtual bool GetVariable(GFxValue* pValue, const char* pVarPath) const;                    // 0x32
    virtual void Unk_33(); virtual void Unk_34(); virtual void Unk_35();
    virtual bool GetVariableArray(UInt32 type, const char* pPathToVar,
                                  UInt32 index, void* pData, UInt32 count); // 0x36
    virtual void Unk_37(); virtual void Unk_38();
    virtual bool Invoke(const char* pMethodName, GFxValue* pResult,
                        const GFxValue* pArgs, UInt32 nArgs);               // 0x39
};

class GFxMovieView {
public:
    virtual void _msvc_destructor();
    UInt32        unk08;
    UInt32        unk0C;
    void*         unk10;
    GFxMovieRoot* movieRoot; // 0x18
};

struct F4SEScaleformInterface {
    enum { kInterfaceVersion = 1 };
    UInt32 interfaceVersion;
    typedef bool (*RegisterCallback)(GFxMovieView* view, GFxValue* root);
    bool (*Register)(const char* name, RegisterCallback callback);
};

// --- Task Interface ---

class ITaskDelegate {
public:
    virtual void Run() = 0;
    virtual void Dispose() { delete this; }
    virtual ~ITaskDelegate() {}
};

struct F4SETaskInterface {
    enum { kInterfaceVersion = 2 };
    UInt32 interfaceVersion;
    void (*AddTask)(ITaskDelegate* task);
    void (*AddUITask)(ITaskDelegate* task);
};

// --- Plugin Version Data (required by F4SE 0.7.x+) ---

struct F4SEPluginVersionData {
    enum { kVersion = 1 };

    enum {
        kAddressIndependence_Signatures                = 1 << 0,
        kAddressIndependence_AddressLibrary_1_10_980   = 1 << 1,
        kAddressIndependence_AddressLibrary_1_11_137   = 1 << 2,
    };

    enum {
        kStructureIndependence_NoStructs           = 1 << 0,
        kStructureIndependence_1_10_980Layout       = 1 << 1,
        kStructureIndependence_1_11_137Layout       = 1 << 2,
    };

    UInt32 dataVersion;
    UInt32 pluginVersion;
    char   name[256];
    char   author[256];
    UInt32 addressIndependence;
    UInt32 structureIndependence;
    UInt32 compatibleVersions[16];
    UInt32 seVersionRequired;
    UInt32 reservedNonBreaking;
    UInt32 reservedBreaking;
    UInt8  reserved[512];
};

// --- Minimal logging (replaces IDebugLog) ---

#include <cstdio>
#include <cstdarg>

class PluginLog {
public:
    FILE* m_file = nullptr;

    bool Open(const char* path) {
        m_file = fopen(path, "w");
        return m_file != nullptr;
    }

    void Close() {
        if (m_file) { fclose(m_file); m_file = nullptr; }
    }

    void Log(const char* fmt, ...) {
        if (!m_file) return;
        va_list args;
        va_start(args, fmt);
        vfprintf(m_file, fmt, args);
        va_end(args);
        fprintf(m_file, "\n");
        fflush(m_file);
    }
};

// Convenience macro matching F4SE's _MESSAGE
#define _MESSAGE(fmt, ...) g_log.Log(fmt, ##__VA_ARGS__)

extern PluginLog g_log;
