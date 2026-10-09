/*
 * SwitchballPhysXPPU - Switchball's PhysX 2.5.0 interface implemented on PhysX SDK 2.8.4
 * (software only). Common declarations.
 *
 * Every 2.5 object the game sees is a small wrapper whose first member is a
 * vtable with the 2.5 slot order, followed by the public data members the
 * game reads directly (userData / appData). The wrapper owns a pointer to
 * the 2.8.4 object; the 2.8.4 object's userData points back to the wrapper.
 *
 * 2.5 methods are __fastcall functions (ecx = this, edx unused, the stack
 * arguments are popped by the callee) - binary compatible with MSVC
 * __thiscall. Methods that return NxVec3 / NxMat34 / ... by value get the
 * hidden result pointer as first stack argument and return it.
 */
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <vector>

#include "NxPhysics.h"
#include "NxCooking.h"
#include "NxStream.h"

#define B2_VERSION "SwitchballPhysXPPU V23"

#define FC __fastcall
typedef void *EDX;                       /* the unused edx of __fastcall methods */

void B2Log(const char *fmt, ...);
void B2LogOnce(const char *key, const char *fmt, ...);

/* ------------------------------------------------------------------------ */
/* 2.5 descriptor layouts (= 2.6.4 layouts, checked against Switchball.exe)  */
/* ------------------------------------------------------------------------ */

#if defined(_M_IX86)                     /* layouts are for the 32-bit game */
#define CHECK_OFF(T, f, o) typedef char chk_##T##_##f[(offsetof(T, f) == (o)) ? 1 : -1]
#define CHECK_SIZE(T, s)   typedef char chk_size_##T[(sizeof(T) == (s)) ? 1 : -1]
#else
#define CHECK_OFF(T, f, o)
#define CHECK_SIZE(T, s)
#endif

struct ShapeDesc25 {                     /* NxShapeDesc, 0x68 + specific   */
    void         *vptr;
    NxU32         type;
    NxMat34       localPose;
    NxU32         shapeFlags;
    NxU16         group;
    NxU16         materialIndex;
    void         *ccdSkeleton;
    NxReal        density;
    NxReal        mass;
    NxReal        skinWidth;
    void         *userData;
    const char   *name;
    NxGroupsMask  groupsMask;
    union {                              /* @0x68 */
        struct { float dimensions[3]; } box;
        struct { NxReal radius; } sphere;
        struct { NxReal radius, height; NxU32 flags; } capsule;
        struct { void *meshData; NxU32 meshFlags; } convex;
        struct { void *meshData; NxU32 meshFlags; NxU32 meshPagingMode; } mesh;
        struct { float normal[3]; NxReal d; } plane;
    } u;
};
CHECK_OFF(ShapeDesc25, shapeFlags, 0x38); CHECK_OFF(ShapeDesc25, groupsMask, 0x58); CHECK_OFF(ShapeDesc25, u, 0x68);

struct BodyDesc25 {                      /* NxBodyDesc 0x84 */
    NxMat34 massLocalPose;
    NxVec3  massSpaceInertia;
    NxReal  mass;
    NxVec3  linearVelocity;
    NxVec3  angularVelocity;
    NxReal  wakeUpCounter, linearDamping, angularDamping, maxAngularVelocity, CCDMotionThreshold;
    NxU32   flags;
    NxReal  sleepLinearVelocity, sleepAngularVelocity;
    NxU32   solverIterationCount;
    NxReal  sleepEnergyThreshold, sleepDamping;
};
CHECK_SIZE(BodyDesc25, 0x84); CHECK_OFF(BodyDesc25, flags, 0x6C);

struct ActorDesc25 {                     /* NxActorDesc 0x5C */
    NxMat34       globalPose;
    BodyDesc25   *body;
    NxReal        density;
    NxU32         flags;
    NxU16         group;
    NxU16         pad;
    void         *userData;
    const char   *name;
    void         *compartment;
    NxU32         type;
    ShapeDesc25 **shapesBegin, **shapesEnd, **shapesCap;
};
CHECK_OFF(ActorDesc25, body, 0x30); CHECK_OFF(ActorDesc25, shapesBegin, 0x50);

struct MaterialDesc25 {                  /* NxMaterialDesc 0x30 */
    NxReal dynamicFriction, staticFriction, restitution, dynamicFrictionV, staticFrictionV;
    NxVec3 dirOfAnisotropy;
    NxU32  flags, frictionCombineMode, restitutionCombineMode;
    void  *spring;
};
CHECK_SIZE(MaterialDesc25, 0x30);

struct SceneDesc25 {                     /* NxSceneDesc (first part) */
    NxVec3  gravity;
    void   *userNotify, *userContactModify, *userTriggerReport, *userContactReport;
    NxReal  maxTimestep;
    NxU32   maxIter, timeStepMethod;
    void   *maxBounds, *limits;
    NxU32   simType, groundPlane, boundsPlanes, flags;
};
CHECK_OFF(SceneDesc25, simType, 0x30);

struct LimitSoft25     { NxReal value, restitution, spring, damping; };
struct LimitSoftPair25 { LimitSoft25 low, high; };
struct Drive25         { NxU32 driveType; NxReal spring, damping, forceLimit; };

struct D6JointDesc25 {                   /* NxD6JointDesc 0x17C */
    void       *vptr;
    NxU32       type;
    void       *actor[2];                /* game's 2.5 actors (our wrappers) */
    NxVec3      localNormal[2], localAxis[2], localAnchor[2];
    NxReal      maxForce, maxTorque;
    void       *userData;
    const char *name;
    NxU32       jointFlags;
    NxU32       xMotion, yMotion, zMotion, swing1Motion, swing2Motion, twistMotion;
    LimitSoft25     linearLimit, swing1Limit, swing2Limit;
    LimitSoftPair25 twistLimit;
    Drive25     xDrive, yDrive, zDrive, swingDrive, twistDrive, slerpDrive;
    NxVec3      drivePosition;
    NxQuat      driveOrientation;
    NxVec3      driveLinearVelocity, driveAngularVelocity;
    NxU32       projectionMode;
    NxReal      projectionDistance, projectionAngle, gearRatio;
    NxU32       flags;
};
CHECK_SIZE(D6JointDesc25, 0x17C); CHECK_OFF(D6JointDesc25, jointFlags, 0x68);
CHECK_OFF(D6JointDesc25, xDrive, 0xD4); CHECK_OFF(D6JointDesc25, drivePosition, 0x134);

struct PD25 {                           /* NxParticleData, 2.5 layout (11 dwords) */
    NxU32  maxParticles;
    NxU32 *numParticlesPtr;
    float *bufferPos, *bufferVel, *bufferLife, *bufferDensity;
    NxU32  posStride, velStride, lifeStride, densityStride;
    const char *name;
};
CHECK_SIZE(PD25, 0x2C);

struct ContactPair25 {
    void        *actors[2];
    const NxU32 *stream;
    NxVec3       sumNormalForce, sumFrictionForce;
};

struct TriangleMeshDesc25 {              /* NxTriangleMeshDesc 0x34 */
    NxU32 numVertices, numTriangles, pointStrideBytes, triangleStrideBytes;
    const void *points, *triangles;
    NxU32 flags;
    NxU32 materialIndexStride;
    const void *materialIndices;
    NxU32 heightFieldVerticalAxis;
    NxReal heightFieldVerticalExtent;
    void *pmap;
    NxReal convexEdgeThreshold;
};
CHECK_SIZE(TriangleMeshDesc25, 0x34);

struct ConvexMeshDesc25 {                /* NxConvexMeshDesc 0x1C */
    NxU32 numVertices, numTriangles, pointStrideBytes, triangleStrideBytes;
    const void *points, *triangles;
    NxU32 flags;
};

struct ClothMeshDesc25 {                 /* NxClothMeshDesc (2.6 layout up to vertexFlags) */
    NxU32 numVertices, numTriangles, pointStrideBytes, triangleStrideBytes;
    const void *points, *triangles;
    NxU32 flags;
    NxU32 vertexMassStrideBytes, vertexFlagStrideBytes;
    const void *vertexMasses, *vertexFlags;
};

struct MeshData25 {                      /* NxMeshData: 16 dwords in 2.5 (2.6: 17) */
    void  *verticesPosBegin, *verticesNormalBegin;
    NxI32  verticesPosByteStride, verticesNormalByteStride;
    NxU32  maxVertices; NxU32 *numVerticesPtr;
    void  *indicesBegin; NxI32 indicesByteStride; NxU32 maxIndices; NxU32 *numIndicesPtr;
    void  *parentIndicesBegin; NxI32 parentIndicesByteStride; NxU32 maxParentIndices; NxU32 *numParentIndicesPtr;
    NxU32  flags;
    const char *name;
};
CHECK_SIZE(MeshData25, 0x40);

struct ClothDesc25 {                     /* NxClothDesc 0xD8 (game ctor) */
    void   *clothMesh;
    NxMat34 globalPose;
    NxReal  thickness, density, bendingStiffness, stretchingStiffness, dampingCoefficient,
            friction, pressure, tearFactor, collisionResponseCoefficient,
            attachmentResponseCoefficient, attachmentTearFactor;
    NxU32   solverIterations;
    NxVec3  externalAcceleration;
    NxReal  wakeUpCounter, sleepLinearVelocity;
    MeshData25 meshData;
    NxU16   collisionGroup; NxU16 pad;
    NxGroupsMask groupsMask;
    NxU32   flags;
    void   *userData;
    const char *name;
};
CHECK_SIZE(ClothDesc25, 0xD8); CHECK_OFF(ClothDesc25, meshData, 0x78); CHECK_OFF(ClothDesc25, flags, 0xCC);

/* ------------------------------------------------------------------------ */
/* Wrappers                                                                  */
/* ------------------------------------------------------------------------ */

struct WScene;
struct WShape;
struct WActor   { void **vtbl; void *userData; NxActor *a; WScene *scene; std::vector<WShape *> *shapes;
                  int dynamic, kinematic; };
struct WShape   { void **vtbl; void *userData; void *appData; NxShape *s; WActor *actor; NxU32 type; };
struct WJoint   { void **vtbl; void *userData; void *appData; NxJoint *j; WScene *scene; };
struct WMaterial{ void **vtbl; void *userData; NxMaterial *m; WScene *scene; };
struct WTriMesh { void **vtbl; NxTriangleMesh *m; };
struct WConvex  { void **vtbl; NxConvexMesh *m; };
struct WClothMesh { void **vtbl; NxClothMesh *m; };
struct WCloth   { void **vtbl; void *userData; NxCloth *c; WScene *scene; MeshData25 md; };
struct WSdk     { void **vtbl; NxPhysicsSDK *sdk; };
class  ContactAdapter;
struct WScene   { void **vtbl; void *userData; NxScene *s; ContactAdapter *contact;
                  std::vector<WMaterial *> *materials; int hw; };
struct WFluid;
struct WEmitter { void **vtbl; void *userData; NxFluidEmitter *e; WFluid *fluid; };
struct WFluid   { void **vtbl; void *userData; NxFluid *f; WScene *scene; PD25 wd;
                  std::vector<WEmitter *> *emitters; };
struct WCooking { void **vtbl; NxCookingInterface *c; };
struct WUtil    { void **vtbl; NxUtilLib *u; };

/* 2.8.4 object -> our wrapper */
inline WActor  *WA(NxActor *a)  { return a ? (WActor *)a->userData : 0; }
inline WShape  *WS(NxShape *s)  { return s ? (WShape *)s->userData : 0; }
inline WJoint  *WJ(NxJoint *j)  { return j ? (WJoint *)j->userData : 0; }

/* our wrapper (from the game) -> 2.8.4 object */
inline NxActor *RA(void *w) { return w ? ((WActor *)w)->a : 0; }
inline NxShape *RS(void *w) { return w ? ((WShape *)w)->s : 0; }

/* globals */
extern NxPhysicsSDK       *g_sdk;
extern NxCookingInterface *g_cook;
extern NxUtilLib          *g_util;
extern WSdk                g_wsdk;
extern int                 g_clothTwoWay;   /* ini: moving bodies feel cloth (2.5 behaviour) */
extern int                 g_hwMode;        /* ini HardwareMode: emulate a PhysX PPU (fluids) */
void  B2InitFluidVtables();
void *B2CreateFluid(WScene *sc, const unsigned char *desc25);
void  B2ReleaseFluid(WScene *sc, WFluid *f);

/* vtables and object factories (b2_objects.cpp) */
void   B2InitVtables();
void **B2Stubs(const char *cls, int n);            /* n "unimplemented" stubs */
extern void **vt_sdk, **vt_scene, **vt_actor, **vt_joint_d6, **vt_material,
            **vt_trimesh, **vt_convex, **vt_clothmesh, **vt_cloth, **vt_cooking, **vt_util,
            **vt_shape[8];

WShape    *NewShapeWrapper(NxShape *s, WActor *owner, void *userData);
WActor    *NewActorWrapper(NxActor *a, WScene *scene, const ActorDesc25 *d);
WMaterial *MaterialWrapper(WScene *sc, NxMaterial *m);
void       DeleteActorWrapper(WActor *w);
WTriMesh  *TriMeshWrapperOf(NxTriangleMesh *m);
WConvex   *ConvexWrapperOf(NxConvexMesh *m);
WClothMesh*ClothMeshWrapperOf(NxClothMesh *m);

/* descriptor conversion (b2_convert.cpp) */
NxShapeDesc *Shape25To28(const ShapeDesc25 *d, std::vector<void *> &owned);
void         Shape28To25(NxShape *s, ShapeDesc25 *out);
void         Body25To28(const BodyDesc25 *in, NxBodyDesc &out);
void         Body28To25(const NxBodyDesc &in, BodyDesc25 *out);
void         Material25To28(const MaterialDesc25 *in, NxMaterialDesc &out);
void         Material28To25(const NxMaterialDesc &in, MaterialDesc25 *out);
bool         D625To28(const D6JointDesc25 *in, NxD6JointDesc &out);
void         D628To25(const NxD6JointDesc &in, D6JointDesc25 *out, NxJoint *j);
void         MeshData25To28(const MeshData25 *in, NxMeshData &out);
void         MeshData28To25(const NxMeshData &in, MeshData25 *out);
NxParameter  Param25To28(NxU32 p, bool *ok);

/* contact report adapter */
class ContactAdapter : public NxUserContactReport {
public:
    void *game;                       /* game's NxUserContactReport (2.5 vtable) */
    std::vector<NxU32> buf;
    ContactAdapter(void *g) : game(g) {}
    void onContactNotify(NxContactPair &pair, NxU32 events);
};
