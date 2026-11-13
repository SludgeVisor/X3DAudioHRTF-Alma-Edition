/*
 * X3DAudio1_5.dll  -  HRTF compatibility shim for F.E.A.R. 2: Project Origin
 *
 * What it does
 *  1. Exports X3DAudioInitialize / X3DAudioCalculate as PE *forwarders* to the
 *     renamed X3DAudio HRTF DLL (x3daudio_hrtf_core.dll).  Loading this DLL
 *     therefore loads the HRTF DLL, whose DllMain installs its XAudio2 hooks.
 *  2. Patches the game EXE's import of ole32!CoCreateInstance.  When the game
 *     asks for CLSID XAudio2_3 the request is passed on (the HRTF DLL turns it
 *     into an XAudio2 2.7 proxy) and the result is wrapped in an object that
 *     speaks the XAudio2 2.3 binary interface the game was compiled against.
 *
 * No CRT, no Windows SDK headers: only a handful of kernel32 imports.
 *
 * Debugging: create an empty file named X3DAudio1_5_shim.debug in the game
 * folder and a log named X3DAudio1_5_shim.log is written next to the EXE.
 * (OutputDebugString output is always emitted as well.)
 */
#include "facade.h"

typedef unsigned long  DWORD;
typedef int            BOOL;
typedef void          *HANDLE;
typedef void          *HMODULE;
typedef unsigned char  BYTE;

#define K32 __declspec(dllimport) __stdcall
K32 HANDLE  GetProcessHeap(void);
K32 void   *HeapAlloc(HANDLE, DWORD, unsigned int);
K32 BOOL    HeapFree(HANDLE, DWORD, void *);
K32 HMODULE GetModuleHandleA(const char *);
K32 BOOL    GetModuleHandleExA(DWORD, const void *, HMODULE *);
K32 void   *GetProcAddress(HMODULE, const char *);
K32 BOOL    VirtualProtect(void *, unsigned int, DWORD, DWORD *);
K32 void    OutputDebugStringA(const char *);
K32 HANDLE  CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
K32 BOOL    WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
K32 BOOL    CloseHandle(HANDLE);
K32 DWORD   GetFileAttributesA(const char *);
K32 HANDLE  CreateThread(void *, unsigned int, DWORD (__stdcall *)(void *), void *, DWORD, DWORD *);
K32 void    Sleep(DWORD);

/* MSVC-ABI marker the compiler references when floats are used (no CRT here). */
int _fltused = 0;

/* ---- host services for facade.c ---------------------------------------- */

void *xa_alloc(xa_u32 n) { return HeapAlloc(GetProcessHeap(), 0, n); }
void  xa_free(void *p)   { if (p) HeapFree(GetProcessHeap(), 0, p); }

static int g_log_to_file;

void xa_log(const char *msg, xa_u32 value)
{
    char buf[200];
    unsigned n = 0, i;
    static const char hex[] = "0123456789abcdef";
    const char *prefix = "[X3DAudio1_5 shim] ";
    for (i = 0; prefix[i]; ++i) buf[n++] = prefix[i];
    for (i = 0; msg[i] && n < 150; ++i) buf[n++] = msg[i];
    buf[n++] = ' '; buf[n++] = '='; buf[n++] = ' '; buf[n++] = '0'; buf[n++] = 'x';
    for (i = 0; i < 8; ++i) buf[n++] = hex[(value >> (28 - 4 * i)) & 15];
    buf[n++] = '\r'; buf[n++] = '\n'; buf[n] = 0;
    OutputDebugStringA(buf);
    if (g_log_to_file) {
        HANDLE h = CreateFileA("X3DAudio1_5_shim.log", 0x00000004 /*FILE_APPEND_DATA*/, 3, 0,
                               4 /*OPEN_ALWAYS*/, 0x80, 0);
        if (h != (HANDLE)(long)-1) {
            DWORD wrote;
            WriteFile(h, buf, n, &wrote, 0);
            CloseHandle(h);
        }
    }
}

/* ---- CoCreateInstance hook --------------------------------------------- */

typedef xa_hr (__stdcall *CoCreateInstance_t)(const xa_guid *, void *, DWORD, const xa_guid *, void **);
static CoCreateInstance_t g_real_cci;

/* If the vtable lives inside the genuine XAudio2_3.dll the HRTF layer is not
 * active; hand the object back unwrapped so the game still works. */
static int is_genuine_xaudio2_3(void *obj)
{
    HMODULE owner = 0, genuine = GetModuleHandleA("XAudio2_3.dll");
    void *vtbl = *(void **)obj;
    if (!genuine) return 0;
    if (!GetModuleHandleExA(0x4 /*FROM_ADDRESS*/ | 0x2 /*UNCHANGED_REFCOUNT*/, vtbl, &owner)) return 0;
    return owner == genuine;
}

static xa_hr __stdcall Hook_CoCreateInstance(const xa_guid *clsid, void *outer, DWORD ctx,
                                             const xa_guid *iid, void **ppv)
{
    if (clsid && ppv && !outer && xa_guid_eq(clsid, &XA_CLSID_XAudio2_3)) {
        void *inner = 0;
        xa_hr hr;
        xa_log("game requested CLSID XAudio2_3", 0);
        hr = g_real_cci(clsid, outer, ctx, iid, &inner);
        if (XA_FAILED(hr) || !inner) { xa_log("CoCreateInstance failed", (xa_u32)hr); return hr; }
        if (is_genuine_xaudio2_3(inner)) {
            xa_log("HRTF layer not active - returning genuine XAudio2_3", 0);
            *ppv = inner;
            return hr;
        }
        return xa_wrap_root(inner, iid, ppv);
    }
    return g_real_cci(clsid, outer, ctx, iid, ppv);
}

/* ---- IAT patching ------------------------------------------------------ */

static int ieq(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
}

static int seq(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

/* returns 1 = patched, 0 = import not found, -1 = IAT not yet bound by the loader */
static int patch_import(HMODULE exe, const char *dll, const char *func, void *hook)
{
    BYTE *base = (BYTE *)exe;
    BYTE *nt, *opt;
    DWORD impRva, sizeOfImage;
    BYTE *desc;

    if (base[0] != 'M' || base[1] != 'Z') return 0;
    nt = base + *(DWORD *)(base + 0x3c);
    if (nt[0] != 'P' || nt[1] != 'E') return 0;
    opt = nt + 24;
    if (*(unsigned short *)opt != 0x10b) return 0;           /* PE32 only */
    sizeOfImage = *(DWORD *)(opt + 56);
    impRva = *(DWORD *)(opt + 0x68);
    if (!impRva) return 0;

    for (desc = base + impRva; *(DWORD *)(desc + 12); desc += 20) {
        const char *name = (const char *)(base + *(DWORD *)(desc + 12));
        DWORD oft = *(DWORD *)desc, ft = *(DWORD *)(desc + 16), i;
        DWORD *names, *iat;
        if (!ieq(name, dll)) continue;
        names = (DWORD *)(base + (oft ? oft : ft));
        iat = (DWORD *)(base + ft);
        for (i = 0; names[i]; ++i) {
            const char *fn;
            if (names[i] & 0x80000000u) continue;            /* by ordinal */
            fn = (const char *)(base + names[i] + 2);
            if (!seq(fn, func)) continue;
            if (iat[i] < sizeOfImage) return -1;             /* still an RVA: loader has not bound it */
            {
                DWORD old;
                if (!VirtualProtect(&iat[i], 4, 0x04 /*PAGE_READWRITE*/, &old)) return 0;
                iat[i] = (DWORD)(unsigned long)hook;
                VirtualProtect(&iat[i], 4, old, &old);
            }
            return 1;
        }
    }
    return 0;
}

static int install_hook(void)
{
    HMODULE ole = GetModuleHandleA("ole32.dll");
    HMODULE exe = GetModuleHandleA(0);
    if (!ole || !exe) return 0;
    if (!g_real_cci)
        g_real_cci = (CoCreateInstance_t)GetProcAddress(ole, "CoCreateInstance");
    if (!g_real_cci) return 0;
    return patch_import(exe, "ole32.dll", "CoCreateInstance", (void *)Hook_CoCreateInstance);
}

static DWORD __stdcall retry_thread(void *unused)
{
    int i, r = -1;
    (void)unused;
    for (i = 0; i < 200 && r == -1; ++i) {
        Sleep(25);
        r = install_hook();
    }
    xa_log("deferred IAT patch result", (xa_u32)r);
    return 0;
}

/* ---- entry point ------------------------------------------------------- */

BOOL __stdcall DllMainCRTStartup(HMODULE inst, DWORD reason, void *reserved)
{
    (void)inst; (void)reserved;
    if (reason == 1 /*DLL_PROCESS_ATTACH*/) {
        int r;
        g_log_to_file = (GetFileAttributesA("X3DAudio1_5_shim.debug") != 0xFFFFFFFFu);
        xa_log("shim loaded", 0);
        r = install_hook();
        xa_log("IAT patch result (1=ok 0=missing -1=deferred)", (xa_u32)r);
        if (r == -1) {
            HANDLE t = CreateThread(0, 0, retry_thread, 0, 0, 0);
            if (t) CloseHandle(t);
        }
    }
    return 1;
}
