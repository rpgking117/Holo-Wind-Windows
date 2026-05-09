"""
generate_proxy_def.py
Reads all exports from SDL2_orig.dll and generates:
  SDL2_orig.def   -- import lib definition (all exports of SDL2_orig)
  SDL2_proxy.def  -- our proxy DLL exports; forwards all except intercepted ones
  sdl2_forwards.h -- optional pragma hints

MSVC requires an import library (SDL2_orig.lib) to resolve FORWARD entries at
link time.  Build it with:
  lib.exe /def:SDL2_orig.def /machine:x64 /out:SDL2_orig.lib

Usage:
    python generate_proxy_def.py <path_to_SDL2_orig.dll>
    (defaults to SDL2_orig.dll in current directory)
"""

import sys, os

INTERCEPTED = {
    "SDL_GL_SwapWindow", "SDL_PollEvent",
    "SDL_CreateWindow", "SDL_ShowWindow", "SDL_RaiseWindow",
    "SDL_SetRelativeMouseMode", "SDL_GetRelativeMouseMode", "SDL_WarpMouseInWindow",
    "SDL_ShowCursor",
}

def get_exports(dll_path):
    try:
        import pefile
        pe = pefile.PE(dll_path)
        names = []
        for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            if exp.name:
                names.append(exp.name.decode())
        return names
    except ImportError:
        pass

    dumpbin = (r"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools"
               r"\VC\Tools\MSVC\14.29.30133\bin\Hostx64\x64\dumpbin.exe")
    if not os.path.exists(dumpbin):
        print("ERROR: pefile not installed and dumpbin not found.")
        sys.exit(1)
    import subprocess
    out = subprocess.check_output([dumpbin, "/EXPORTS", dll_path], text=True)
    names = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[-1].startswith("SDL_"):
            names.append(parts[-1])
    return names


def main():
    dll = sys.argv[1] if len(sys.argv) > 1 else "SDL2_orig.dll"
    if not os.path.exists(dll):
        print(f"ERROR: {dll} not found.")
        sys.exit(1)

    exports = get_exports(dll)
    if not exports:
        print(f"ERROR: No exports found in {dll}")
        sys.exit(1)

    print(f"Found {len(exports)} exports in {dll}")

    # 1. SDL2_orig.def: used to create SDL2_orig.lib via lib.exe /def:
    with open("SDL2_orig.def", "w") as f:
        f.write("LIBRARY SDL2_orig\n")
        f.write("EXPORTS\n")
        for name in exports:
            f.write(f"    {name}\n")
    print("Written: SDL2_orig.def")

    # 2. SDL2_proxy.def: our proxy exports
    #    Intercepted = implemented in pipboy_capture.c
    #    Others = PE-level FORWARD to SDL2_orig (needs SDL2_orig.lib at link time)
    with open("SDL2_proxy.def", "w") as f:
        f.write("LIBRARY SDL2\n")
        f.write("EXPORTS\n")
        for name in exports:
            if name in INTERCEPTED:
                f.write(f"    {name}\n")
            else:
                f.write(f"    {name} = SDL2_orig.{name}\n")
    print("Written: SDL2_proxy.def")

    # 3. sdl2_forwards.h: pragma-based alternative (optional backup)
    with open("sdl2_forwards.h", "w") as f:
        f.write("/* Auto-generated -- do not edit */\n")
        for name in exports:
            if name not in INTERCEPTED:
                f.write(f'#pragma comment(linker, "/export:{name}=SDL2_orig.{name}")\n')
    print("Written: sdl2_forwards.h")


if __name__ == "__main__":
    main()
