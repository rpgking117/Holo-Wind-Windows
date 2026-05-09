/*
 * launch_openmw.c  --  Hollo-Wind Windows launcher shim
 *
 * The F4SE plugin calls CreateProcess(OpenMWPath, ...) to start OpenMW.
 * Since CreateProcess cannot run .ps1/.bat directly, we compile this small
 * exe and point OpenMWPath at it.
 *
 * At runtime it reads hollowind_paths.cfg from its own directory to find:
 *   trigger_file   = <where to write the launch trigger>
 *   morrowind_dir  = <working directory for the watcher>
 *
 * Then it:
 *   1. Starts morrowind_watcher.ps1 (detached, keeps running)
 *   2. Writes the trigger file so the watcher immediately launches OpenMW
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void log_err(const char *msg, DWORD err)
{
    FILE *f = fopen("C:\\tmp\\launch_openmw.log", "a");
    if (f) { fprintf(f, "[launch_openmw] %s  err=%lu\n", msg, err); fclose(f); }
}

/* Read a key=value line from a simple config file */
static int read_cfg(const char *cfg_path, const char *key, char *out, int out_sz)
{
    FILE *f = fopen(cfg_path, "r");
    if (!f) return 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* strip newline */
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        /* skip comments and blanks */
        if (line[0] == '#' || line[0] == '\0') continue;
        /* find '=' */
        char *eq = strchr(line, '='); if (!eq) continue;
        /* compare key (trimmed) */
        *eq = '\0';
        char *k = line;
        while (*k == ' ' || *k == '\t') k++;
        char *ke = k + strlen(k) - 1;
        while (ke > k && (*ke == ' ' || *ke == '\t')) { *ke = '\0'; ke--; }
        if (_stricmp(k, key) != 0) continue;
        /* grab value (trimmed) */
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        char *ve = v + strlen(v) - 1;
        while (ve > v && (*ve == ' ' || *ve == '\t')) { *ve = '\0'; ve--; }
        strncpy_s(out, out_sz, v, out_sz - 1);
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hInst; (void)hPrev; (void)nShow;

    /* FO4 PID is passed as the first command line argument */
    int fo4pid = 0;
    if (lpCmd && lpCmd[0])
        fo4pid = atoi(lpCmd);

    /* Locate our own directory */
    char self_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, self_dir, MAX_PATH);
    char *bs = strrchr(self_dir, '\\');
    if (bs) bs[1] = '\0'; /* keep trailing slash */

    /* Config file lives next to this exe */
    char cfg_path[MAX_PATH];
    snprintf(cfg_path, MAX_PATH, "%shollowind_paths.cfg", self_dir);

    /* Read trigger_file and morrowind_dir from config */
    char trigger_file[MAX_PATH] = {0};
    char morrowind_dir[MAX_PATH] = {0};

    if (!read_cfg(cfg_path, "trigger_file", trigger_file, MAX_PATH)) {
        /* Fallback: use the exe's own directory for IPC */
        snprintf(trigger_file, MAX_PATH, "%s.morrowind_launch", self_dir);
    }
    if (!read_cfg(cfg_path, "morrowind_dir", morrowind_dir, MAX_PATH)) {
        /* Fallback: use own directory */
        strncpy_s(morrowind_dir, MAX_PATH, self_dir, MAX_PATH - 1);
    }

    /* Ensure IPC directory exists */
    char ipc_dir[MAX_PATH];
    strncpy_s(ipc_dir, MAX_PATH, trigger_file, MAX_PATH - 1);
    char *last_bs = strrchr(ipc_dir, '\\');
    if (last_bs) { *last_bs = '\0'; CreateDirectoryA(ipc_dir, NULL); }
    CreateDirectoryA("C:\\tmp", NULL);

    /* 1. Start the watcher (detached — keeps running after we exit) */
    char watcher_ps1[MAX_PATH];
    snprintf(watcher_ps1, MAX_PATH, "%smorrowind_watcher.ps1", self_dir);

    char watcher_cmd[MAX_PATH * 2];
    if (fo4pid > 0) {
        snprintf(watcher_cmd, sizeof(watcher_cmd),
            "powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass "
            "-File \"%s\" -ModDir \"%s\" -FO4PID %d",
            watcher_ps1, morrowind_dir, fo4pid);
    } else {
        snprintf(watcher_cmd, sizeof(watcher_cmd),
            "powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass "
            "-File \"%s\" -ModDir \"%s\"",
            watcher_ps1, morrowind_dir);
    }

    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessA(NULL, watcher_cmd, NULL, NULL, FALSE,
                        CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS,
                        NULL, morrowind_dir, &si, &pi)) {
        log_err("Failed to start watcher", GetLastError());
    } else {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    /* Small delay so watcher is watching before we write the trigger */
    Sleep(300);

    /* 2. Write trigger file — tells watcher to launch OpenMW */
    HANDLE h = CreateFileA(trigger_file, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    } else {
        log_err("Failed to write trigger", GetLastError());
    }

    return 0;
}
