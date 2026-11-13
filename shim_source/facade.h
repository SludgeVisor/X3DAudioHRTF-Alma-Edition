/*
 * facade.h - XAudio2 "2.3 ABI" facade over an XAudio2 "2.7 ABI" object.
 *
 * F.E.A.R. 2 was built against the Nov 2008 DirectX SDK (XAudio2_3). The HRTF
 * proxy DLL (x3daudio1_7_hrtf) hands out objects with the June 2010 (XAudio2_7)
 * layout. Differences that matter, all verified from the game's own machine code:
 *
 *   1. IXAudio2Voice has no Set/GetOutputFilterParameters in 2.3, so every voice
 *      method from SetVolume onward sits two vtable slots lower than in 2.7.
 *   2. XAUDIO2_VOICE_SENDS in 2.3 is { UINT32 OutputCount; IXAudio2Voice** pOutputVoices; }
 *      (array of raw voice pointers). 2.7 uses an array of XAUDIO2_SEND_DESCRIPTOR
 *      { UINT32 Flags; IXAudio2Voice* pOutputVoice; }.
 *
 * Everything else the game uses (IXAudio2 root vtable, XAUDIO2_BUFFER, voice
 * callbacks, effect chains, device details, debug configuration) is layout-
 * identical and is passed through untouched.
 *
 * This file is plain C with no CRT dependency so it can be built with any
 * compiler (MSVC, clang, MinGW) and also unit-tested on a 32-bit stdcall host.
 */
#ifndef FACADE_H
#define FACADE_H

#ifdef _WIN32
#define XSTD __stdcall
#else
#define XSTD __attribute__((stdcall))
#endif

typedef unsigned int  xa_u32;
typedef int           xa_hr;

typedef struct xa_guid {
    xa_u32 d1; unsigned short d2, d3; unsigned char d4[8];
} xa_guid;

#define XA_S_OK            ((xa_hr)0)
#define XA_E_NOINTERFACE   ((xa_hr)0x80004002)
#define XA_E_POINTER       ((xa_hr)0x80004003)
#define XA_E_OUTOFMEMORY   ((xa_hr)0x8007000E)
#define XA_E_INVALIDARG    ((xa_hr)0x80070057)
#define XA_FAILED(hr)      ((hr) < 0)

/* provided by the host (Win32 glue or test harness) */
void *xa_alloc(xa_u32 bytes);
void  xa_free(void *p);
void  xa_log(const char *msg, xa_u32 value);

/* CLSID the game asks for (XAudio2_3). */
extern const xa_guid XA_CLSID_XAudio2_3;
extern const xa_guid XA_IID_IXAudio2;

int xa_guid_eq(const xa_guid *a, const xa_guid *b);

/*
 * Takes ownership of one reference to `inner` (a 2.7-layout IXAudio2*) and
 * returns, in *ppv, a 2.3-layout IXAudio2 facade for the interface `riid`.
 */
xa_hr xa_wrap_root(void *inner, const xa_guid *riid, void **ppv);

#endif
