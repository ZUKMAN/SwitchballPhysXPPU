/*
 * recorder25 - records moving-body physics on the ORIGINAL PhysX 2.5 runtime,
 * in the same format as SwitchballPhysXPPU's TrackMoving=1, so the two logs
 * can be compared (box pushing etc.).
 *
 * It is a transparent PhysXLoader.dll proxy: the game's original loader must
 * be renamed to PhysXLoader_orig.dll. Nothing is changed in the simulation.
 * Hardware is not faked - use it on software levels.
 *
 * Output: recorder25.log in the game folder
 *   body <actor>: ...            once per moving body (mass, damping, sleep, material)
 *   mv f<frame> <actor> ...      every frame, every body faster than 5 cm/s,
 *                                and every sleep/wake change
 *
 * Build (x86 Native Tools Command Prompt): build_recorder25.bat - no SDK needed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

typedef unsigned int NxU32;
#define FC __fastcall

/* PhysX 2.5 vtable slots (verified for Switchball, see src/b2_objects.cpp) */
enum {
    SDK_CREATE_SCENE = 4,
    SCENE_CREATE_ACTOR = 6, SCENE_RELEASE_ACTOR = 7, SCENE_MATERIAL_FROM_INDEX = 45, SCENE_FETCH_RESULTS = 95,
    ACT_GET_GLOBAL_POSE = 9, ACT_GET_NB_SHAPES = 19, ACT_GET_SHAPES = 20, ACT_IS_DYNAMIC = 26, ACT_GET_MASS = 43,
    ACT_GET_LIN_DAMP = 50, ACT_GET_ANG_DAMP = 52, ACT_GET_LIN_VEL = 55, ACT_GET_ANG_VEL = 56,
    ACT_IS_SLEEPING = 77, ACT_GET_SLEEP_LIN = 78, ACT_GET_SLEEP_ANG = 80, ACT_READ_BODY_FLAG = 86,
    ACT_GET_SOLVER_ITER = 89,
    SHAPE_GET_MATERIAL = 20, SHAPE_GET_SKIN = 22, SHAPE_GET_TYPE = 23,
    MAT_GET_DYN_F = 6, MAT_GET_STA_F = 8, MAT_GET_REST = 10
};
#define NX_BF_KINEMATIC 128

static char g_dir[MAX_PATH];
static HMODULE g_orig;
static FILE *g_log;
static CRITICAL_SECTION g_cs;
static volatile LONG g_init;

static void Log(const char *fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    EnterCriticalSection(&g_cs);
    fprintf(g_log, "[%10lu] ", (unsigned long)GetTickCount());
    va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
    LeaveCriticalSection(&g_cs);
}

static void *Slot(void *obj, int i) { return (*(void ***)obj)[i]; }

typedef void *(FC *PFN_P_P)(void *, void *, void *);
typedef void  (FC *PFN_V_P)(void *, void *, void *);
typedef NxU32 (FC *PFN_U)(void *, void *);
typedef NxU32 (FC *PFN_U_U)(void *, void *, NxU32);
typedef float (FC *PFN_F)(void *, void *);
typedef void *(FC *PFN_OUT)(void *, void *, void *);
typedef NxU32 (FC *PFN_FETCH)(void *, void *, NxU32, NxU32, void *);

/* ---- original slot values (saved when patching) ---- */
static void *o_createScene, *o_createActor, *o_releaseActor, *o_fetch;

static int WriteSlot(void **slot, void *v)
{
    DWORD old, tmp;
    if (*slot == v) return 1;
    if (!VirtualProtect(slot, 4, PAGE_EXECUTE_READWRITE, &old)) return 0;
    *slot = v;
    VirtualProtect(slot, 4, old, &tmp);
    return 1;
}

/* ---- tracked actors ---- */
#define MAX_ACT 8192
typedef struct { void *a; void *scene; int asleep; int known; } Act;
static Act g_act[MAX_ACT];
static int g_nAct;
static void *g_curScene; static unsigned g_frame, g_lines;
static int g_track = 1;

static void Track(void *scene)
{
    int i;
    if (scene != g_curScene) { g_curScene = scene; g_frame = 0; }
    g_frame++;
    if (g_lines > 60000) return;
    for (i = 0; i < g_nAct; i++) {
        Act *t = &g_act[i];
        void *a = t->a;
        float pose[12], v[3], av[3], sp;
        int asleep, changed;
        if (!a || t->scene != scene) continue;
        if (!(((PFN_U)Slot(a, ACT_IS_DYNAMIC))(a, 0) & 0xFF)) continue;
        if (((PFN_U_U)Slot(a, ACT_READ_BODY_FLAG))(a, 0, NX_BF_KINEMATIC) & 0xFF) continue;
        asleep = (((PFN_U)Slot(a, ACT_IS_SLEEPING))(a, 0) & 0xFF) ? 1 : 0;
        ((PFN_OUT)Slot(a, ACT_GET_LIN_VEL))(a, 0, v);
        ((PFN_OUT)Slot(a, ACT_GET_ANG_VEL))(a, 0, av);
        changed = t->known && t->asleep != asleep;
        if (!t->known) {
            void **shapes = (void **)((PFN_P_P)Slot(a, ACT_GET_SHAPES))(a, 0, 0);
            void *s0 = shapes ? shapes[0] : 0;
            NxU32 mi = s0 ? (((PFN_U)Slot(s0, SHAPE_GET_MATERIAL))(s0, 0) & 0xFFFF) : 0;
            void *mt = ((PFN_P_P)Slot(scene, SCENE_MATERIAL_FROM_INDEX))(scene, 0, (void *)(size_t)mi);
            t->known = 1;
            Log("body %p: %u shapes (first type %u), mass %.3f, linDamp %.3f angDamp %.3f, sleepLin %.3f sleepAng %.3f, "
                "iters %u, material %u (static %.2f dynamic %.2f rest %.2f), skin %.4f",
                a, ((PFN_U)Slot(a, ACT_GET_NB_SHAPES))(a, 0), s0 ? ((PFN_U)Slot(s0, SHAPE_GET_TYPE))(s0, 0) : 99,
                ((PFN_F)Slot(a, ACT_GET_MASS))(a, 0), ((PFN_F)Slot(a, ACT_GET_LIN_DAMP))(a, 0),
                ((PFN_F)Slot(a, ACT_GET_ANG_DAMP))(a, 0), ((PFN_F)Slot(a, ACT_GET_SLEEP_LIN))(a, 0),
                ((PFN_F)Slot(a, ACT_GET_SLEEP_ANG))(a, 0), ((PFN_U)Slot(a, ACT_GET_SOLVER_ITER))(a, 0),
                mi, mt ? ((PFN_F)Slot(mt, MAT_GET_STA_F))(mt, 0) : -1.0f, mt ? ((PFN_F)Slot(mt, MAT_GET_DYN_F))(mt, 0) : -1.0f,
                mt ? ((PFN_F)Slot(mt, MAT_GET_REST))(mt, 0) : -1.0f, s0 ? ((PFN_F)Slot(s0, SHAPE_GET_SKIN))(s0, 0) : -1.0f);
        }
        t->asleep = asleep;
        sp = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (!changed && (asleep || sp < 0.0025f)) continue;
        ((PFN_OUT)Slot(a, ACT_GET_GLOBAL_POSE))(a, 0, pose);
        Log("mv f%u %p (%u sh) pos %.3f %.3f %.3f vel %.3f %.3f %.3f |%.3f| angvel %.2f %.2f %.2f%s",
            g_frame, a, ((PFN_U)Slot(a, ACT_GET_NB_SHAPES))(a, 0), pose[9], pose[10], pose[11],
            v[0], v[1], v[2], sqrtf(sp), av[0], av[1], av[2],
            changed ? (asleep ? "  -> ASLEEP" : "  -> AWAKE") : "");
        g_lines++;
    }
}

/* ---- hooks ---- */
static void *FC H_CreateActor(void *self, void *edx, void *desc)
{
    void *a = ((PFN_P_P)o_createActor)(self, 0, desc);
    (void)edx;
    if (a) {
        EnterCriticalSection(&g_cs);
        if (g_nAct < MAX_ACT) { g_act[g_nAct].a = a; g_act[g_nAct].scene = self; g_act[g_nAct].known = 0; g_nAct++; }
        LeaveCriticalSection(&g_cs);
    }
    return a;
}

static void FC H_ReleaseActor(void *self, void *edx, void *a)
{
    int i;
    (void)edx;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < g_nAct; i++) if (g_act[i].a == a) { g_act[i] = g_act[--g_nAct]; break; }
    LeaveCriticalSection(&g_cs);
    ((PFN_V_P)o_releaseActor)(self, 0, a);
}

static NxU32 FC H_Fetch(void *self, void *edx, NxU32 st, NxU32 block, void *err)
{
    NxU32 r = ((PFN_FETCH)o_fetch)(self, 0, st, block, err);
    (void)edx;
    if (g_track && (r & 0xFF)) Track(self);
    return r;
}

static void *FC H_CreateScene(void *self, void *edx, void *desc)
{
    void *s = ((PFN_P_P)o_createScene)(self, 0, desc);
    (void)edx;
    if (s) {
        void **vt = *(void ***)s;
        if (vt[SCENE_CREATE_ACTOR] != (void *)H_CreateActor)  { o_createActor = vt[SCENE_CREATE_ACTOR];  WriteSlot(&vt[SCENE_CREATE_ACTOR], (void *)H_CreateActor); }
        if (vt[SCENE_RELEASE_ACTOR] != (void *)H_ReleaseActor) { o_releaseActor = vt[SCENE_RELEASE_ACTOR]; WriteSlot(&vt[SCENE_RELEASE_ACTOR], (void *)H_ReleaseActor); }
        if (vt[SCENE_FETCH_RESULTS] != (void *)H_Fetch)        { o_fetch = vt[SCENE_FETCH_RESULTS];        WriteSlot(&vt[SCENE_FETCH_RESULTS], (void *)H_Fetch); }
        Log("createScene -> %p (hooks installed)", s);
    }
    return s;
}

static void HookSdk(void *sdk)
{
    void **vt;
    if (!sdk) return;
    vt = *(void ***)sdk;
    /* the game releases/re-creates the SDK, and the 2.5 DLL may come back with original slots */
    if (vt[SDK_CREATE_SCENE] != (void *)H_CreateScene) {
        o_createScene = vt[SDK_CREATE_SCENE];
        WriteSlot(&vt[SDK_CREATE_SCENE], (void *)H_CreateScene);
    }
}

/* ---- exports (forwarded to PhysXLoader_orig.dll) ---- */
static void Init(void)
{
    char p[MAX_PATH];
    if (g_init) return;
    EnterCriticalSection(&g_cs);
    if (!g_init) {
        _snprintf(p, sizeof(p), "%srecorder25.log", g_dir); p[MAX_PATH - 1] = 0;
        g_log = fopen(p, "w");
        _snprintf(p, sizeof(p), "%sSwitchballPhysXPPU.ini", g_dir); p[MAX_PATH - 1] = 0;
        g_track = GetPrivateProfileIntA("Recorder25", "TrackMoving", 1, p); /* own section: on unless disabled */
        Log("==== recorder25 started (original PhysX 2.5), TrackMoving=%d ====", g_track);
        _snprintf(p, sizeof(p), "%sPhysXLoader_orig.dll", g_dir); p[MAX_PATH - 1] = 0;
        g_orig = LoadLibraryA(p);
        Log(g_orig ? "loaded %s" : "ERROR: cannot load %s", p);
        g_init = 1;
    }
    LeaveCriticalSection(&g_cs);
}
static void *Real(const char *n) { Init(); return g_orig ? (void *)GetProcAddress(g_orig, n) : 0; }

typedef void *(__cdecl *PFN_Create)(NxU32, void *, void *, const void *, void *);
typedef void *(__cdecl *PFN_CreateID)(NxU32, char *, char *, char *, char *, void *, void *, const void *, void *);
typedef void  (__cdecl *PFN_Release)(void *);
typedef void *(__cdecl *PFN_NoArg)(void);
typedef void *(__cdecl *PFN_Cook)(NxU32);
typedef void *(__cdecl *PFN_CookID)(NxU32, char *, char *, char *, char *);

void *__cdecl R_NxCreatePhysicsSDK(NxU32 v, void *al, void *out, const void *d, void *e)
{ PFN_Create f = (PFN_Create)Real("NxCreatePhysicsSDK"); void *s = f ? f(v, al, out, d, e) : 0; HookSdk(s); return s; }
void *__cdecl R_NxCreatePhysicsSDKWithID(NxU32 v, char *a, char *b, char *c, char *dd, void *al, void *out, const void *d, void *e)
{ PFN_CreateID f = (PFN_CreateID)Real("NxCreatePhysicsSDKWithID"); void *s = f ? f(v, a, b, c, dd, al, out, d, e) : 0; HookSdk(s); return s; }
void __cdecl R_NxReleasePhysicsSDK(void *s)
{ PFN_Release f = (PFN_Release)Real("NxReleasePhysicsSDK"); EnterCriticalSection(&g_cs); g_nAct = 0; LeaveCriticalSection(&g_cs); if (f) f(s); }
void *__cdecl R_NxGetCookingLib(NxU32 v)                    { PFN_Cook f = (PFN_Cook)Real("NxGetCookingLib"); return f ? f(v) : 0; }
void *__cdecl R_NxGetCookingLibWithID(NxU32 v, char *a, char *b, char *c, char *d)
{ PFN_CookID f = (PFN_CookID)Real("NxGetCookingLibWithID"); return f ? f(v, a, b, c, d) : 0; }
void *__cdecl R_NxGetUtilLib(void)           { PFN_NoArg f = (PFN_NoArg)Real("NxGetUtilLib");           return f ? f() : 0; }
void *__cdecl R_NxGetPhysicsSDK(void)        { PFN_NoArg f = (PFN_NoArg)Real("NxGetPhysicsSDK");        return f ? f() : 0; }
void *__cdecl R_NxGetFoundationSDK(void)     { PFN_NoArg f = (PFN_NoArg)Real("NxGetFoundationSDK");     return f ? f() : 0; }
void *__cdecl R_NxGetPhysicsSDKAllocator(void){ PFN_NoArg f = (PFN_NoArg)Real("NxGetPhysicsSDKAllocator"); return f ? f() : 0; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x)
{
    (void)x;
    if (r == DLL_PROCESS_ATTACH) {
        char *sl; HMODULE pin;
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&g_cs);
        GetModuleFileNameA(h, g_dir, sizeof(g_dir));
        sl = strrchr(g_dir, '\\'); if (sl) sl[1] = 0; else g_dir[0] = 0;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)&DllMain, &pin);
    }
    return TRUE;
}
