/*
 * test284 - smoke test for SwitchballPhysXPPU: can PhysX 2.8.4 run inside this
 * process on this machine, without the PhysX System Software installed?
 *
 * SwitchballPhysXPPU will not use the 2.8.4 PhysXLoader.dll (same file name as the
 * game's loader / our proxy, and it only accepts a "local" core when a
 * registry value holds this machine's MAC address). Instead it loads the
 * core directly, the same way the 2.8.4 loader does internally:
 *
 *   PhysXCore.dll    : NpCreatePhysicsSDK(version, allocator, output, desc, &err)
 *                      NpReleasePhysicsSDK(sdk)
 *   PhysXCooking.dll : NxGetCookingInterface(version)
 *
 * The DLLs are expected in a sub folder "PhysX284" next to this exe (later:
 * next to the game's PhysXLoader.dll).
 *
 * Build: build_test.bat (MSVC x86, PHYSX284_SDK = path to ...\SDKs)
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "NxPhysics.h"
#include "NxCooking.h"
#include "NxStream.h"

typedef NxPhysicsSDK *(__cdecl *PFN_NpCreate)(NxU32, NxUserAllocator *, NxUserOutputStream *,
                                              const NxPhysicsSDKDesc &, NxSDKCreateError *);
typedef void (__cdecl *PFN_NpRelease)(NxPhysicsSDK *);
typedef NxCookingInterface *(__cdecl *PFN_GetCooking)(NxU32);

class Out : public NxUserOutputStream {
public:
    void reportError(NxErrorCode c, const char *m, const char *f, int l)
    { printf("  [2.8.4 error %d] %s (%s:%d)\n", (int)c, m ? m : "", f ? f : "", l); }
    NxAssertResponse reportAssertViolation(const char *m, const char *f, int l)
    { printf("  [2.8.4 assert] %s (%s:%d)\n", m ? m : "", f ? f : "", l); return NX_AR_CONTINUE; }
    void print(const char *m) { printf("  [2.8.4] %s\n", m ? m : ""); }
} g_out;

class MemWrite : public NxStream {
public:
    NxU8 *buf; NxU32 size, cap;
    MemWrite() : buf(0), size(0), cap(0) {}
    ~MemWrite() { free(buf); }
    NxU8 readByte() const { return 0; } NxU16 readWord() const { return 0; }
    NxU32 readDword() const { return 0; } NxF32 readFloat() const { return 0; }
    NxF64 readDouble() const { return 0; } void readBuffer(void *, NxU32) const {}
    NxStream &storeByte(NxU8 b) { return storeBuffer(&b, 1); }
    NxStream &storeWord(NxU16 w) { return storeBuffer(&w, 2); }
    NxStream &storeDword(NxU32 d) { return storeBuffer(&d, 4); }
    NxStream &storeFloat(NxF32 f) { return storeBuffer(&f, 4); }
    NxStream &storeDouble(NxF64 f) { return storeBuffer(&f, 8); }
    NxStream &storeBuffer(const void *p, NxU32 n) {
        if (size + n > cap) { cap = (size + n) * 2 + 256; buf = (NxU8 *)realloc(buf, cap); }
        memcpy(buf + size, p, n); size += n; return *this;
    }
};
class MemRead : public NxStream {
public:
    mutable const NxU8 *p;
    MemRead(const NxU8 *d) : p(d) {}
    NxU8 readByte() const { NxU8 v; memcpy(&v, p, 1); p += 1; return v; }
    NxU16 readWord() const { NxU16 v; memcpy(&v, p, 2); p += 2; return v; }
    NxU32 readDword() const { NxU32 v; memcpy(&v, p, 4); p += 4; return v; }
    NxF32 readFloat() const { NxF32 v; memcpy(&v, p, 4); p += 4; return v; }
    NxF64 readDouble() const { NxF64 v; memcpy(&v, p, 8); p += 8; return v; }
    void readBuffer(void *b, NxU32 n) const { memcpy(b, p, n); p += n; }
    NxStream &storeByte(NxU8) { return *this; } NxStream &storeWord(NxU16) { return *this; }
    NxStream &storeDword(NxU32) { return *this; } NxStream &storeFloat(NxF32) { return *this; }
    NxStream &storeDouble(NxF64) { return *this; } NxStream &storeBuffer(const void *, NxU32) { return *this; }
};

static int fail(const char *what) { printf("FAILED: %s\n", what); return 1; }

int main()
{
    char dir[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    *(strrchr(dir, '\\') + 1) = 0;
    strcat(dir, "PhysX284\\");
    printf("SwitchballPhysXPPU test284 - PhysX SDK %d.%d.%d (version 0x%08X)\nDLL folder: %s\n\n",
           NX_SDK_VERSION_MAJOR, NX_SDK_VERSION_MINOR, NX_SDK_VERSION_BUGFIX, NX_PHYSICS_SDK_VERSION, dir);

    /* LOAD_WITH_ALTERED_SEARCH_PATH: PhysXCore's own imports (cudart32_30_9.dll)
       are searched in the DLL's folder */
    sprintf(path, "%sPhysXCore.dll", dir);
    HMODULE core = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    printf("1. load PhysXCore.dll            : %s (error %lu)\n", core ? "ok" : "FAILED", core ? 0 : GetLastError());
    if (!core) return fail("PhysXCore.dll");
    PFN_NpCreate  npCreate  = (PFN_NpCreate)GetProcAddress(core, "NpCreatePhysicsSDK");
    PFN_NpRelease npRelease = (PFN_NpRelease)GetProcAddress(core, "NpReleasePhysicsSDK");
    if (!npCreate || !npRelease) return fail("core exports");

    NxSDKCreateError err = NXCE_NO_ERROR;
    NxPhysicsSDKDesc sdkDesc;
    sdkDesc.flags |= NX_SDKF_NO_HARDWARE;
    NxPhysicsSDK *sdk = npCreate(NX_PHYSICS_SDK_VERSION, NULL, &g_out, sdkDesc, &err);
    printf("2. NpCreatePhysicsSDK            : %p (error %d)\n", sdk, (int)err);
    if (!sdk) return fail("SDK");
    printf("   hardware version              : %d (0 = none, software only)\n", (int)sdk->getHWVersion());

    sprintf(path, "%sPhysXCooking.dll", dir);
    HMODULE cookDll = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    PFN_GetCooking getCooking = cookDll ? (PFN_GetCooking)GetProcAddress(cookDll, "NxGetCookingInterface") : 0;
    NxCookingInterface *cook = getCooking ? getCooking(NX_PHYSICS_SDK_VERSION) : 0;
    printf("3. PhysXCooking.dll / interface  : %p / %p\n", cookDll, cook);
    if (!cook || !cook->NxInitCooking(NULL, &g_out)) return fail("cooking");

    NxSceneDesc sd;
    sd.gravity = NxVec3(0, -9.81f, 0);
    sd.simType = NX_SIMULATION_SW;
    sd.flags &= ~NX_SF_SIMULATE_SEPARATE_THREAD;
    NxScene *scene = sdk->createScene(sd);
    printf("4. software scene                : %p\n", scene);
    if (!scene) return fail("scene");

    /* ground plane */
    NxPlaneShapeDesc plane; NxActorDesc ground; ground.shapes.pushBack(&plane);
    scene->createActor(ground);

    /* cooked convex box, dropped from 5 m */
    NxVec3 pts[8];
    for (int i = 0; i < 8; i++) pts[i] = NxVec3(i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f);
    NxConvexMeshDesc cd; cd.numVertices = 8; cd.pointStrideBytes = sizeof(NxVec3); cd.points = pts;
    cd.flags = NX_CF_COMPUTE_CONVEX;
    MemWrite w;
    bool cooked = cook->NxCookConvexMesh(cd, w);
    MemRead r(w.buf);
    NxConvexMesh *cm = cooked ? sdk->createConvexMesh(r) : 0;
    printf("5. cook + create convex mesh     : %s, %p (%u bytes)\n", cooked ? "ok" : "FAILED", cm, w.size);
    if (!cm) return fail("convex");

    NxConvexShapeDesc cs; cs.meshData = cm;
    NxBodyDesc bd; NxActorDesc ad; ad.shapes.pushBack(&cs); ad.body = &bd; ad.density = 1.0f;
    ad.globalPose.t = NxVec3(0, 5, 0);
    NxActor *box = scene->createActor(ad);
    printf("6. dynamic convex actor          : %p\n", box);
    if (!box) return fail("actor");

    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    for (int i = 1; i <= 180; i++) {
        scene->simulate(1.0f / 60.0f);
        scene->flushStream();
        scene->fetchResults(NX_RIGID_BODY_FINISHED, true);
        if (i % 30 == 0) printf("   t=%.1fs  box y=%.3f\n", i / 60.0f, box->getGlobalPosition().y);
    }
    QueryPerformanceCounter(&t1);
    float y = box->getGlobalPosition().y;
    printf("7. 180 steps in %.2f ms, box rests at y=%.3f (expected ~0.5)\n",
           1000.0 * (t1.QuadPart - t0.QuadPart) / f.QuadPart, y);

    sdk->releaseScene(*scene);
    cook->NxCloseCooking();
    npRelease(sdk);
    printf("\n%s\n", (y > 0.4f && y < 0.6f) ? "RESULT: PhysX 2.8.4 works here - OK" : "RESULT: unexpected result");
    return 0;
}
