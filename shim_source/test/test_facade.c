/* 32-bit stdcall host test for facade.c. The "game" calls only through raw 2.3 slot
 * numbers taken from the F.E.A.R. 2 disassembly; the mock "core" has the 2.7 layout. */
#include "../shim/facade.h"

static long sys3(long n, long a, long b, long c) {
    long r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory"); return r; }
static void out(const char *s) { unsigned n = 0; while (s[n]) ++n; sys3(4, 1, (long)s, n); }
static void outn(const char *s, unsigned v) {
    char b[16]; int i = 14; b[15] = 0; if (!v) b[i--] = '0'; while (v) { b[i--] = '0' + v % 10; v /= 10; }
    out(s); out(b + i + 1); out("\n"); }

static unsigned char arena[65536]; static unsigned used; static int frees;
void *xa_alloc(xa_u32 n) { void *p = arena + used; used += (n + 15) & ~15u; return p; }
void  xa_free(void *p) { (void)p; ++frees; }
void  xa_log(const char *m, xa_u32 v) { (void)m; (void)v; }

static int fails;
#define CHECK(c, name) do { if (!(c)) { ++fails; out("FAIL: " name "\n"); } else out("ok:   " name "\n"); } while (0)

/* ---- mock 2.7 core ---- */
static int last_slot = -1; static unsigned a1, a2, a3, a4; static float fa; static void *p1, *p2;
static int released, destroyed;
static void *mock_voice_src[1], *mock_voice_mix[1], *mock_voice_mst[1];
static const void *mock_vt[29]; static const void *mock_root_vt[16];
static struct { const void *const *vt; } m_src = { mock_vt }, m_mix = { mock_vt }, m_mst = { mock_vt }, m_root = { mock_root_vt };
static unsigned seen_sendcount, seen_flags[4]; static void *seen_out[4];

static void XSTD bad(void *t) { (void)t; ++fails; out("FAIL: unexpected slot hit\n"); }
static xa_u32 XSTD m_release(void *t) { (void)t; released++; return 0; }
static xa_hr XSTD m_createsrc(void *t, void **pp, const void *f, xa_u32 fl, float mx, void *cb, const void *sends, const void *ch)
{ (void)t;(void)f;(void)cb;(void)ch; *pp = &m_src; last_slot = 8; a1 = fl; fa = mx;
  const struct { xa_u32 n; struct { xa_u32 f; void *v; } *s; } *s27 = sends;
  seen_sendcount = s27 ? s27->n : 99; for (unsigned i = 0; s27 && i < s27->n && i < 4; ++i) { seen_flags[i] = s27->s[i].f; seen_out[i] = s27->s[i].v; }
  return 0; }
static xa_hr XSTD m_createsub(void *t, void **pp, xa_u32 c, xa_u32 r, xa_u32 fl, xa_u32 st, const void *sends, const void *ch)
{ (void)t;(void)fl;(void)sends;(void)ch; *pp = &m_mix; last_slot = 9; a1 = c; a2 = r; a3 = st; return 0; }
static xa_hr XSTD m_createmst(void *t, void **pp, xa_u32 c, xa_u32 r, xa_u32 fl, xa_u32 d, const void *ch)
{ (void)t;(void)fl;(void)ch; *pp = &m_mst; last_slot = 10; a1 = c; a2 = r; a3 = d; return 0; }
static xa_hr XSTD m_setvol(void *t, float v, xa_u32 op) { (void)t; last_slot = 12; fa = v; a1 = op; return 0; }
static xa_hr XSTD m_setmatrix(void *t, void *dest, xa_u32 s, xa_u32 d, const float *m, xa_u32 op)
{ (void)t; last_slot = 16; p1 = dest; a1 = s; a2 = d; fa = m[1]; a3 = op; return 0; }
static void XSTD m_destroy(void *t) { (void)t; last_slot = 18; destroyed++; }
static xa_hr XSTD m_start(void *t, xa_u32 f, xa_u32 op) { (void)t; last_slot = 19; a1 = f; a2 = op; return 0; }
static xa_hr XSTD m_submit(void *t, const void *b, const void *w) { (void)t; last_slot = 21; p1 = (void *)b; p2 = (void *)w; return 0; }
static void XSTD m_getstate(void *t, void *st) { (void)t; last_slot = 25; p1 = st; }
static xa_hr XSTD m_setfreq(void *t, float r, xa_u32 op) { (void)t; last_slot = 26; fa = r; a1 = op; return 0; }
static xa_hr XSTD m_seteffparams(void *t, xa_u32 i, const void *p, xa_u32 sz, xa_u32 op) { (void)t;(void)p; last_slot = 6; a1 = i; a2 = sz; a3 = op; return 0; }
static xa_hr XSTD m_setoutvoices(void *t, const void *sends) { (void)t; last_slot = 1; const struct { xa_u32 n; struct { xa_u32 f; void *v; } *s; } *s27 = sends;
  seen_sendcount = s27->n; for (unsigned i = 0; i < s27->n && i < 4; ++i) { seen_flags[i] = s27->s[i].f; seen_out[i] = s27->s[i].v; } return 0; }

/* ---- the "game": raw 2.3 vtable calls ---- */
#define VT(o) (*(void ***)(o))
typedef xa_hr (XSTD *f_hr_2)(void *, xa_u32);

int main(void)
{
    unsigned i;
    for (i = 0; i < 29; ++i) mock_vt[i] = (void *)bad;
    for (i = 0; i < 16; ++i) mock_root_vt[i] = (void *)bad;
    mock_root_vt[2] = (void *)m_release; mock_root_vt[8] = (void *)m_createsrc;
    mock_root_vt[9] = (void *)m_createsub; mock_root_vt[10] = (void *)m_createmst;
    mock_vt[1] = (void *)m_setoutvoices; mock_vt[6] = (void *)m_seteffparams; mock_vt[12] = (void *)m_setvol;
    mock_vt[16] = (void *)m_setmatrix; mock_vt[18] = (void *)m_destroy; mock_vt[19] = (void *)m_start;
    mock_vt[21] = (void *)m_submit; mock_vt[25] = (void *)m_getstate; mock_vt[26] = (void *)m_setfreq;

    void *root = 0;
    CHECK(xa_wrap_root(&m_root, &XA_IID_IXAudio2, &root) == 0 && root, "wrap root");

    /* game: CreateMasteringVoice = slot 10 (0x28), 7 args incl this */
    void *mst = 0;
    CHECK(((xa_hr (XSTD *)(void *, void **, xa_u32, xa_u32, xa_u32, xa_u32, void *))VT(root)[10])(root, &mst, 6, 48000, 0, 0, 0) == 0 && mst && mst != &m_mst, "CreateMasteringVoice returns facade");
    CHECK(last_slot == 10 && a1 == 6 && a2 == 48000, "CreateMastering args forwarded");

    /* game: CreateSubmixVoice = slot 9 (0x24), pSendList NULL, effect chain */
    void *mix = 0;
    CHECK(((xa_hr (XSTD *)(void *, void **, xa_u32, xa_u32, xa_u32, xa_u32, void *, void *))VT(root)[9])(root, &mix, 2, 44100, 0, 0, 0, 0) == 0 && mix, "CreateSubmixVoice");
    CHECK(last_slot == 9 && a1 == 2 && a2 == 44100, "CreateSubmix args forwarded");

    /* game: CreateSourceVoice = slot 8 (0x20) with 2.3 sends {2, &[mst, mix]} */
    void *pair[2] = { mst, mix };
    struct { xa_u32 n; void **v; } sends23 = { 2, pair };
    void *src = 0;
    CHECK(((xa_hr (XSTD *)(void *, void **, const void *, xa_u32, float, void *, void *, void *))VT(root)[8])(root, &src, 0, 0, 2.0f, 0, &sends23, 0) == 0 && src && src != &m_src, "CreateSourceVoice");
    CHECK(seen_sendcount == 2 && seen_flags[0] == 0 && seen_flags[1] == 0, "sends converted to descriptors (flags=0)");
    CHECK(seen_out[0] == &m_mst && seen_out[1] == &m_mix, "send targets translated to inner voices");
    CHECK(fa == 2.0f, "MaxFrequencyRatio float forwarded");

    /* voice slots in 2.3: SetVolume=10 -> core slot 12 */
    CHECK(((xa_hr (XSTD *)(void *, float, xa_u32))VT(src)[10])(src, 0.25f, 7) == 0 && last_slot == 12 && fa == 0.25f && a1 == 7, "SetVolume slot 10 -> core 12");
    /* SetOutputMatrix 2.3 slot 14 -> core 16, dest translated */
    float mat[2] = { 0.5f, 0.75f };
    CHECK(((xa_hr (XSTD *)(void *, void *, xa_u32, xa_u32, const float *, xa_u32))VT(src)[14])(src, mix, 1, 2, mat, 3) == 0 && last_slot == 16 && p1 == &m_mix && a1 == 1 && a2 == 2 && fa == 0.75f && a3 == 3, "SetOutputMatrix slot 14 -> core 16, dest translated");
    /* SetEffectParameters unchanged slot 6 */
    CHECK(((xa_hr (XSTD *)(void *, xa_u32, const void *, xa_u32, xa_u32))VT(src)[6])(src, 0, mat, 0x34, 0) == 0 && last_slot == 6 && a2 == 0x34, "SetEffectParameters slot 6");
    /* SetOutputVoices slot 1 */
    void *one[1] = { mst }; struct { xa_u32 n; void **v; } s1 = { 1, one };
    CHECK(((xa_hr (XSTD *)(void *, const void *))VT(src)[1])(src, &s1) == 0 && last_slot == 1 && seen_sendcount == 1 && seen_out[0] == &m_mst, "SetOutputVoices converts sends");
    /* source slots: Start 17->19, SubmitSourceBuffer 19->21, GetState 23->25, SetFrequencyRatio 24->26 */
    CHECK(((xa_hr (XSTD *)(void *, xa_u32, xa_u32))VT(src)[17])(src, 0, 0) == 0 && last_slot == 19, "Start slot 17 -> core 19");
    CHECK(((xa_hr (XSTD *)(void *, const void *, const void *))VT(src)[19])(src, mat, 0) == 0 && last_slot == 21 && p1 == mat, "SubmitSourceBuffer slot 19 -> core 21");
    ((void (XSTD *)(void *, void *))VT(src)[23])(src, mat); CHECK(last_slot == 25, "GetState slot 23 -> core 25");
    CHECK(((xa_hr (XSTD *)(void *, float, xa_u32))VT(src)[24])(src, 1.5f, 0) == 0 && last_slot == 26 && fa == 1.5f, "SetFrequencyRatio slot 24 -> core 26");

    /* DestroyVoice 2.3 slot 16 -> core 18, facade freed */
    int f0 = frees;
    ((void (XSTD *)(void *))VT(src)[16])(src);
    CHECK(last_slot == 18 && destroyed == 1 && frees == f0 + 1, "DestroyVoice slot 16 -> core 18 and facade freed");

    /* QueryInterface / Release on root */
    void *q = 0;
    CHECK(((xa_hr (XSTD *)(void *, const xa_guid *, void **))VT(root)[0])(root, &XA_IID_IXAudio2, &q) == 0 && q == root, "QueryInterface(IXAudio2)");
    ((xa_u32 (XSTD *)(void *))VT(root)[2])(root); CHECK(released == 0, "first Release keeps inner");
    ((xa_u32 (XSTD *)(void *))VT(root)[2])(root); CHECK(released == 1, "last Release releases inner");

    outn("failures: ", (unsigned)fails);
    sys3(1, fails ? 1 : 0, 0, 0);
    return 0;
}

void __attribute__((naked)) _start(void) { __asm__ volatile("call main\n"); }
