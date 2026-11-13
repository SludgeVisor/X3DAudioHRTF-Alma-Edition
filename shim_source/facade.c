/*
 * facade.c - see facade.h for the rationale.
 *
 * Vtable maps (verified against F.E.A.R. 2's disassembly):
 *
 *  IXAudio2 root                  identical in 2.3 and 2.7 (slots 0..15)
 *
 *  IXAudio2Voice   2.3 slot   2.7 slot
 *   GetVoiceDetails      0        0
 *   SetOutputVoices      1        1     (sends struct converted)
 *   SetEffectChain       2        2
 *   EnableEffect         3        3
 *   DisableEffect        4        4
 *   GetEffectState       5        5
 *   SetEffectParameters  6        6
 *   GetEffectParameters  7        7
 *   SetFilterParameters  8        8
 *   GetFilterParameters  9        9
 *   (SetOutputFilterParameters)  -       10   <- absent in 2.3
 *   (GetOutputFilterParameters)  -       11   <- absent in 2.3
 *   SetVolume           10       12
 *   GetVolume           11       13
 *   SetChannelVolumes   12       14
 *   GetChannelVolumes   13       15
 *   SetOutputMatrix     14       16     (destination voice pointer translated)
 *   GetOutputMatrix     15       17     (destination voice pointer translated)
 *   DestroyVoice        16       18
 *  IXAudio2SourceVoice adds:
 *   Start               17       19
 *   Stop                18       20
 *   SubmitSourceBuffer  19       21
 *   FlushSourceBuffers  20       22
 *   Discontinuity       21       23
 *   ExitLoop            22       24
 *   GetState            23       25
 *   SetFrequencyRatio   24       26
 *   GetFrequencyRatio   25       27
 *   SetSourceSampleRate 26       28     (extra trailing slot, harmless if unused)
 */
#include "facade.h"

const xa_guid XA_CLSID_XAudio2_3 =
    { 0x4c5e637a, 0x16c7, 0x4de3, { 0x9c, 0x46, 0x5e, 0xd2, 0x21, 0x81, 0x96, 0x2d } };
const xa_guid XA_IID_IXAudio2 =
    { 0x8bcf1f58, 0x9fe7, 0x4583, { 0x8a, 0xc6, 0xe2, 0xad, 0xc4, 0x65, 0xc8, 0xbb } };
static const xa_guid IID_IUnknown_ =
    { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

int xa_guid_eq(const xa_guid *a, const xa_guid *b)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    unsigned i;
    for (i = 0; i < sizeof(xa_guid); ++i)
        if (x[i] != y[i]) return 0;
    return 1;
}

/* ---- structures -------------------------------------------------------- */

typedef struct { xa_u32 OutputCount; void **pOutputVoices; } sends23;       /* game  */
typedef struct { xa_u32 Flags; void *pOutputVoice; }          send27;       /* 2.7   */
typedef struct { xa_u32 SendCount; send27 *pSends; }          sends27;

#define VOICE_MAGIC 0x56324146u   /* "FA2V" */
#define ROOT_MAGIC  0x52324146u   /* "FA2R" */

typedef struct voicef {
    const void *const *vtbl;      /* must be first: this is what the game sees */
    xa_u32 magic;
    void  *inner;                 /* 2.7-layout voice owned by the HRTF proxy   */
} voicef;

typedef struct rootf {
    const void *const *vtbl;
    xa_u32 magic;
    int    ref;
    void  *inner;                 /* 2.7-layout IXAudio2 */
} rootf;

/* 2.7 slot numbers */
enum {
    R27_Release = 2, R27_GetDeviceCount = 3, R27_GetDeviceDetails = 4, R27_Initialize = 5,
    R27_RegisterForCallbacks = 6, R27_UnregisterForCallbacks = 7,
    R27_CreateSourceVoice = 8, R27_CreateSubmixVoice = 9, R27_CreateMasteringVoice = 10,
    R27_StartEngine = 11, R27_StopEngine = 12, R27_CommitChanges = 13,
    R27_GetPerformanceData = 14, R27_SetDebugConfiguration = 15,

    V27_GetVoiceDetails = 0, V27_SetOutputVoices = 1, V27_SetEffectChain = 2,
    V27_EnableEffect = 3, V27_DisableEffect = 4, V27_GetEffectState = 5,
    V27_SetEffectParameters = 6, V27_GetEffectParameters = 7,
    V27_SetFilterParameters = 8, V27_GetFilterParameters = 9,
    /* 10, 11 = Set/GetOutputFilterParameters (not in 2.3) */
    V27_SetVolume = 12, V27_GetVolume = 13, V27_SetChannelVolumes = 14,
    V27_GetChannelVolumes = 15, V27_SetOutputMatrix = 16, V27_GetOutputMatrix = 17,
    V27_DestroyVoice = 18,

    S27_Start = 19, S27_Stop = 20, S27_SubmitSourceBuffer = 21, S27_FlushSourceBuffers = 22,
    S27_Discontinuity = 23, S27_ExitLoop = 24, S27_GetState = 25,
    S27_SetFrequencyRatio = 26, S27_GetFrequencyRatio = 27, S27_SetSourceSampleRate = 28
};

#define SLOT(obj, i) ((*(void *const *const *)(obj))[i])

/* ---- helpers ----------------------------------------------------------- */

static void *to_inner(void *v)
{
    voicef *f;
    if (!v) return v;
    f = (voicef *)v;
    return (f->magic == VOICE_MAGIC) ? f->inner : v;   /* foreign pointers pass through */
}

typedef struct { sends27 s; send27 d[16]; send27 *heap; } sends_tmp;

static const sends27 *sends_in(const sends23 *in, sends_tmp *t, xa_hr *hr)
{
    xa_u32 n, i;
    send27 *arr;
    t->heap = 0;
    *hr = XA_S_OK;
    if (!in) return 0;
    n = in->OutputCount;
    arr = t->d;
    if (n > 16) {
        arr = (send27 *)xa_alloc(n * (xa_u32)sizeof(send27));
        if (!arr) { *hr = XA_E_OUTOFMEMORY; return 0; }
        t->heap = arr;
    }
    for (i = 0; i < n; ++i) {
        arr[i].Flags = 0;
        arr[i].pOutputVoice = to_inner(in->pOutputVoices ? in->pOutputVoices[i] : 0);
    }
    t->s.SendCount = n;
    t->s.pSends = n ? arr : 0;
    return &t->s;
}

static void sends_done(sends_tmp *t) { if (t->heap) xa_free(t->heap); }

/* ---- voice methods (shared by source / submix / mastering facades) ------ */

#define INNER(s) ((s)->inner)

static void XSTD v_GetVoiceDetails(voicef *s, void *details)
{ ((void (XSTD *)(void *, void *))SLOT(INNER(s), V27_GetVoiceDetails))(INNER(s), details); }

static xa_hr XSTD v_SetOutputVoices(voicef *s, const sends23 *list)
{
    sends_tmp t; xa_hr hr; const sends27 *p = sends_in(list, &t, &hr);
    if (XA_FAILED(hr)) return hr;
    hr = ((xa_hr (XSTD *)(void *, const sends27 *))SLOT(INNER(s), V27_SetOutputVoices))(INNER(s), p);
    sends_done(&t);
    return hr;
}

static xa_hr XSTD v_SetEffectChain(voicef *s, const void *chain)
{ return ((xa_hr (XSTD *)(void *, const void *))SLOT(INNER(s), V27_SetEffectChain))(INNER(s), chain); }

static xa_hr XSTD v_EnableEffect(voicef *s, xa_u32 idx, xa_u32 op)
{ return ((xa_hr (XSTD *)(void *, xa_u32, xa_u32))SLOT(INNER(s), V27_EnableEffect))(INNER(s), idx, op); }

static xa_hr XSTD v_DisableEffect(voicef *s, xa_u32 idx, xa_u32 op)
{ return ((xa_hr (XSTD *)(void *, xa_u32, xa_u32))SLOT(INNER(s), V27_DisableEffect))(INNER(s), idx, op); }

static void XSTD v_GetEffectState(voicef *s, xa_u32 idx, int *enabled)
{ ((void (XSTD *)(void *, xa_u32, int *))SLOT(INNER(s), V27_GetEffectState))(INNER(s), idx, enabled); }

static xa_hr XSTD v_SetEffectParameters(voicef *s, xa_u32 idx, const void *p, xa_u32 size, xa_u32 op)
{ return ((xa_hr (XSTD *)(void *, xa_u32, const void *, xa_u32, xa_u32))SLOT(INNER(s), V27_SetEffectParameters))(INNER(s), idx, p, size, op); }

static xa_hr XSTD v_GetEffectParameters(voicef *s, xa_u32 idx, void *p, xa_u32 size)
{ return ((xa_hr (XSTD *)(void *, xa_u32, void *, xa_u32))SLOT(INNER(s), V27_GetEffectParameters))(INNER(s), idx, p, size); }

static xa_hr XSTD v_SetFilterParameters(voicef *s, const void *p, xa_u32 op)
{ return ((xa_hr (XSTD *)(void *, const void *, xa_u32))SLOT(INNER(s), V27_SetFilterParameters))(INNER(s), p, op); }

static void XSTD v_GetFilterParameters(voicef *s, void *p)
{ ((void (XSTD *)(void *, void *))SLOT(INNER(s), V27_GetFilterParameters))(INNER(s), p); }

static xa_hr XSTD v_SetVolume(voicef *s, float vol, xa_u32 op)
{ return ((xa_hr (XSTD *)(void *, float, xa_u32))SLOT(INNER(s), V27_SetVolume))(INNER(s), vol, op); }

static void XSTD v_GetVolume(voicef *s, float *vol)
{ ((void (XSTD *)(void *, float *))SLOT(INNER(s), V27_GetVolume))(INNER(s), vol); }

static xa_hr XSTD v_SetChannelVolumes(voicef *s, xa_u32 ch, const float *vols, xa_u32 op)
{ return ((xa_hr (XSTD *)(void *, xa_u32, const float *, xa_u32))SLOT(INNER(s), V27_SetChannelVolumes))(INNER(s), ch, vols, op); }

static void XSTD v_GetChannelVolumes(voicef *s, xa_u32 ch, float *vols)
{ ((void (XSTD *)(void *, xa_u32, float *))SLOT(INNER(s), V27_GetChannelVolumes))(INNER(s), ch, vols); }

static xa_hr XSTD v_SetOutputMatrix(voicef *s, void *dest, xa_u32 src, xa_u32 dst, const float *m, xa_u32 op)
{ return ((xa_hr (XSTD *)(void *, void *, xa_u32, xa_u32, const float *, xa_u32))SLOT(INNER(s), V27_SetOutputMatrix))(INNER(s), to_inner(dest), src, dst, m, op); }

static void XSTD v_GetOutputMatrix(voicef *s, void *dest, xa_u32 src, xa_u32 dst, float *m)
{ ((void (XSTD *)(void *, void *, xa_u32, xa_u32, float *))SLOT(INNER(s), V27_GetOutputMatrix))(INNER(s), to_inner(dest), src, dst, m); }

static void XSTD v_DestroyVoice(voicef *s)
{
    void *inner = s->inner;
    s->magic = 0;
    ((void (XSTD *)(void *))SLOT(inner, V27_DestroyVoice))(inner);
    xa_free(s);
}

/* ---- source voice methods ---------------------------------------------- */

static xa_hr XSTD s_Start(voicef *s, xa_u32 flags, xa_u32 op)
{ return ((xa_hr (XSTD *)(void *, xa_u32, xa_u32))SLOT(INNER(s), S27_Start))(INNER(s), flags, op); }

static xa_hr XSTD s_Stop(voicef *s, xa_u32 flags, xa_u32 op)
{ return ((xa_hr (XSTD *)(void *, xa_u32, xa_u32))SLOT(INNER(s), S27_Stop))(INNER(s), flags, op); }

static xa_hr XSTD s_SubmitSourceBuffer(voicef *s, const void *buf, const void *wma)
{ return ((xa_hr (XSTD *)(void *, const void *, const void *))SLOT(INNER(s), S27_SubmitSourceBuffer))(INNER(s), buf, wma); }

static xa_hr XSTD s_FlushSourceBuffers(voicef *s)
{ return ((xa_hr (XSTD *)(void *))SLOT(INNER(s), S27_FlushSourceBuffers))(INNER(s)); }

static xa_hr XSTD s_Discontinuity(voicef *s)
{ return ((xa_hr (XSTD *)(void *))SLOT(INNER(s), S27_Discontinuity))(INNER(s)); }

static xa_hr XSTD s_ExitLoop(voicef *s, xa_u32 op)
{ return ((xa_hr (XSTD *)(void *, xa_u32))SLOT(INNER(s), S27_ExitLoop))(INNER(s), op); }

static void XSTD s_GetState(voicef *s, void *state)
{ ((void (XSTD *)(void *, void *))SLOT(INNER(s), S27_GetState))(INNER(s), state); }

static xa_hr XSTD s_SetFrequencyRatio(voicef *s, float ratio, xa_u32 op)
{ return ((xa_hr (XSTD *)(void *, float, xa_u32))SLOT(INNER(s), S27_SetFrequencyRatio))(INNER(s), ratio, op); }

static void XSTD s_GetFrequencyRatio(voicef *s, float *ratio)
{ ((void (XSTD *)(void *, float *))SLOT(INNER(s), S27_GetFrequencyRatio))(INNER(s), ratio); }

static xa_hr XSTD s_SetSourceSampleRate(voicef *s, xa_u32 rate)
{ return ((xa_hr (XSTD *)(void *, xa_u32))SLOT(INNER(s), S27_SetSourceSampleRate))(INNER(s), rate); }

/* ---- 2.3-layout vtables ------------------------------------------------ */

#define VOICE_BASE_ENTRIES \
    (const void *)v_GetVoiceDetails,      /*  0 */ \
    (const void *)v_SetOutputVoices,      /*  1 */ \
    (const void *)v_SetEffectChain,       /*  2 */ \
    (const void *)v_EnableEffect,         /*  3 */ \
    (const void *)v_DisableEffect,        /*  4 */ \
    (const void *)v_GetEffectState,       /*  5 */ \
    (const void *)v_SetEffectParameters,  /*  6 */ \
    (const void *)v_GetEffectParameters,  /*  7 */ \
    (const void *)v_SetFilterParameters,  /*  8 */ \
    (const void *)v_GetFilterParameters,  /*  9 */ \
    (const void *)v_SetVolume,            /* 10 */ \
    (const void *)v_GetVolume,            /* 11 */ \
    (const void *)v_SetChannelVolumes,    /* 12 */ \
    (const void *)v_GetChannelVolumes,    /* 13 */ \
    (const void *)v_SetOutputMatrix,      /* 14 */ \
    (const void *)v_GetOutputMatrix,      /* 15 */ \
    (const void *)v_DestroyVoice          /* 16 */

static const void *const voice_vtbl[17] = { VOICE_BASE_ENTRIES };

static const void *const source_vtbl[27] = {
    VOICE_BASE_ENTRIES,
    (const void *)s_Start,                /* 17 */
    (const void *)s_Stop,                 /* 18 */
    (const void *)s_SubmitSourceBuffer,   /* 19 */
    (const void *)s_FlushSourceBuffers,   /* 20 */
    (const void *)s_Discontinuity,        /* 21 */
    (const void *)s_ExitLoop,             /* 22 */
    (const void *)s_GetState,             /* 23 */
    (const void *)s_SetFrequencyRatio,    /* 24 */
    (const void *)s_GetFrequencyRatio,    /* 25 */
    (const void *)s_SetSourceSampleRate   /* 26 */
};

static xa_hr wrap_voice(void *inner, const void *const *vtbl, void **out, xa_u32 kind)
{
    voicef *f = (voicef *)xa_alloc((xa_u32)sizeof(voicef));
    if (!f) {
        if (inner) ((void (XSTD *)(void *))SLOT(inner, V27_DestroyVoice))(inner);
        return XA_E_OUTOFMEMORY;
    }
    f->vtbl = vtbl;
    f->magic = VOICE_MAGIC;
    f->inner = inner;
    *out = f;
    xa_log("voice wrapped (1=source 2=submix 3=mastering)", kind);
    return XA_S_OK;
}

/* ---- IXAudio2 root facade ---------------------------------------------- */

#define RINNER(s) ((s)->inner)

static xa_u32 XSTD r_AddRef(rootf *s)
{ return (xa_u32)__atomic_add_fetch(&s->ref, 1, __ATOMIC_SEQ_CST); }

static xa_u32 XSTD r_Release(rootf *s)
{
    int n = __atomic_sub_fetch(&s->ref, 1, __ATOMIC_SEQ_CST);
    if (n == 0) {
        void *inner = s->inner;
        s->magic = 0;
        ((xa_u32 (XSTD *)(void *))SLOT(inner, R27_Release))(inner);
        xa_free(s);
        xa_log("root facade released", 0);
    }
    return (xa_u32)n;
}

static xa_hr XSTD r_QueryInterface(rootf *s, const xa_guid *riid, void **ppv)
{
    if (!ppv) return XA_E_POINTER;
    if (riid && (xa_guid_eq(riid, &IID_IUnknown_) || xa_guid_eq(riid, &XA_IID_IXAudio2))) {
        r_AddRef(s);
        *ppv = s;
        return XA_S_OK;
    }
    *ppv = 0;
    return XA_E_NOINTERFACE;
}

static xa_hr XSTD r_GetDeviceCount(rootf *s, xa_u32 *count)
{ return ((xa_hr (XSTD *)(void *, xa_u32 *))SLOT(RINNER(s), R27_GetDeviceCount))(RINNER(s), count); }

static xa_hr XSTD r_GetDeviceDetails(rootf *s, xa_u32 index, void *details)
{ return ((xa_hr (XSTD *)(void *, xa_u32, void *))SLOT(RINNER(s), R27_GetDeviceDetails))(RINNER(s), index, details); }

static xa_hr XSTD r_Initialize(rootf *s, xa_u32 flags, xa_u32 processor)
{ return ((xa_hr (XSTD *)(void *, xa_u32, xa_u32))SLOT(RINNER(s), R27_Initialize))(RINNER(s), flags, processor); }

static xa_hr XSTD r_RegisterForCallbacks(rootf *s, void *cb)
{ return ((xa_hr (XSTD *)(void *, void *))SLOT(RINNER(s), R27_RegisterForCallbacks))(RINNER(s), cb); }

static void XSTD r_UnregisterForCallbacks(rootf *s, void *cb)
{ ((void (XSTD *)(void *, void *))SLOT(RINNER(s), R27_UnregisterForCallbacks))(RINNER(s), cb); }

static xa_hr XSTD r_CreateSourceVoice(rootf *s, void **ppVoice, const void *fmt, xa_u32 flags,
                                      float maxRatio, void *cb, const sends23 *sends, const void *chain)
{
    void *iv = 0; sends_tmp t; xa_hr hr; const sends27 *sp;
    if (!ppVoice) return XA_E_POINTER;
    sp = sends_in(sends, &t, &hr);
    if (XA_FAILED(hr)) return hr;
    hr = ((xa_hr (XSTD *)(void *, void **, const void *, xa_u32, float, void *, const sends27 *, const void *))
          SLOT(RINNER(s), R27_CreateSourceVoice))(RINNER(s), &iv, fmt, flags, maxRatio, cb, sp, chain);
    sends_done(&t);
    if (XA_FAILED(hr) || !iv) return hr;
    return wrap_voice(iv, source_vtbl, ppVoice, 1);
}

static xa_hr XSTD r_CreateSubmixVoice(rootf *s, void **ppVoice, xa_u32 channels, xa_u32 rate, xa_u32 flags,
                                      xa_u32 stage, const sends23 *sends, const void *chain)
{
    void *iv = 0; sends_tmp t; xa_hr hr; const sends27 *sp;
    if (!ppVoice) return XA_E_POINTER;
    sp = sends_in(sends, &t, &hr);
    if (XA_FAILED(hr)) return hr;
    hr = ((xa_hr (XSTD *)(void *, void **, xa_u32, xa_u32, xa_u32, xa_u32, const sends27 *, const void *))
          SLOT(RINNER(s), R27_CreateSubmixVoice))(RINNER(s), &iv, channels, rate, flags, stage, sp, chain);
    sends_done(&t);
    if (XA_FAILED(hr) || !iv) return hr;
    return wrap_voice(iv, voice_vtbl, ppVoice, 2);
}

static xa_hr XSTD r_CreateMasteringVoice(rootf *s, void **ppVoice, xa_u32 channels, xa_u32 rate, xa_u32 flags,
                                         xa_u32 deviceIndex, const void *chain)
{
    void *iv = 0; xa_hr hr;
    if (!ppVoice) return XA_E_POINTER;
    hr = ((xa_hr (XSTD *)(void *, void **, xa_u32, xa_u32, xa_u32, xa_u32, const void *))
          SLOT(RINNER(s), R27_CreateMasteringVoice))(RINNER(s), &iv, channels, rate, flags, deviceIndex, chain);
    if (XA_FAILED(hr) || !iv) return hr;
    return wrap_voice(iv, voice_vtbl, ppVoice, 3);
}

static xa_hr XSTD r_StartEngine(rootf *s)
{ return ((xa_hr (XSTD *)(void *))SLOT(RINNER(s), R27_StartEngine))(RINNER(s)); }

static void XSTD r_StopEngine(rootf *s)
{ ((void (XSTD *)(void *))SLOT(RINNER(s), R27_StopEngine))(RINNER(s)); }

static xa_hr XSTD r_CommitChanges(rootf *s, xa_u32 opset)
{ return ((xa_hr (XSTD *)(void *, xa_u32))SLOT(RINNER(s), R27_CommitChanges))(RINNER(s), opset); }

/* The 2.7 XAUDIO2_PERFORMANCE_DATA is 64 bytes (it appends two XMA counters to
 * the older 56-byte layout). Use a roomy temp and give the caller only the
 * common 56-byte prefix so a smaller 2.3-era buffer can never be overrun. */
static void XSTD r_GetPerformanceData(rootf *s, void *out)
{
    unsigned char tmp[128];
    unsigned i;
    if (!out) return;
    for (i = 0; i < sizeof(tmp); ++i) tmp[i] = 0;
    ((void (XSTD *)(void *, void *))SLOT(RINNER(s), R27_GetPerformanceData))(RINNER(s), tmp);
    for (i = 0; i < 56; ++i) ((unsigned char *)out)[i] = tmp[i];
}

static void XSTD r_SetDebugConfiguration(rootf *s, const void *cfg, void *reserved)
{ ((void (XSTD *)(void *, const void *, void *))SLOT(RINNER(s), R27_SetDebugConfiguration))(RINNER(s), cfg, reserved); }

static const void *const root_vtbl[16] = {
    (const void *)r_QueryInterface,          /*  0 */
    (const void *)r_AddRef,                  /*  1 */
    (const void *)r_Release,                 /*  2 */
    (const void *)r_GetDeviceCount,          /*  3 */
    (const void *)r_GetDeviceDetails,        /*  4 */
    (const void *)r_Initialize,              /*  5 */
    (const void *)r_RegisterForCallbacks,    /*  6 */
    (const void *)r_UnregisterForCallbacks,  /*  7 */
    (const void *)r_CreateSourceVoice,       /*  8 */
    (const void *)r_CreateSubmixVoice,       /*  9 */
    (const void *)r_CreateMasteringVoice,    /* 10 */
    (const void *)r_StartEngine,             /* 11 */
    (const void *)r_StopEngine,              /* 12 */
    (const void *)r_CommitChanges,           /* 13 */
    (const void *)r_GetPerformanceData,      /* 14 */
    (const void *)r_SetDebugConfiguration    /* 15 */
};

xa_hr xa_wrap_root(void *inner, const xa_guid *riid, void **ppv)
{
    rootf *r;
    if (!ppv || !inner) return XA_E_POINTER;
    if (riid && !xa_guid_eq(riid, &IID_IUnknown_) && !xa_guid_eq(riid, &XA_IID_IXAudio2)) {
        ((xa_u32 (XSTD *)(void *))SLOT(inner, R27_Release))(inner);
        return XA_E_NOINTERFACE;
    }
    r = (rootf *)xa_alloc((xa_u32)sizeof(rootf));
    if (!r) {
        ((xa_u32 (XSTD *)(void *))SLOT(inner, R27_Release))(inner);
        return XA_E_OUTOFMEMORY;
    }
    r->vtbl = root_vtbl;
    r->magic = ROOT_MAGIC;
    r->ref = 1;
    r->inner = inner;
    *ppv = r;
    xa_log("root IXAudio2 facade created", 0);
    return XA_S_OK;
}
