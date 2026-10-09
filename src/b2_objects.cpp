/*
 * SwitchballPhysXPPU - the 2.5 interface objects. Slot numbers are the 2.5 vtable
 * slots (inventory of levels 1-1..1-6 + 2.6.4 order with the 2.5 shifts
 * found in Switchball.exe). Unused slots are "unimplemented" stubs that log
 * the class, slot and caller (and then return 0).
 */
#include "b2_common.h"
#include <map>
#include <math.h>

void **vt_sdk, **vt_scene, **vt_actor, **vt_joint_d6, **vt_material, **vt_trimesh, **vt_convex,
     **vt_clothmesh, **vt_cloth, **vt_cooking, **vt_util, **vt_shape[8];

/* ------------------------------------------------------------------------ */
/* Unimplemented-slot stubs                                                  */
/* ------------------------------------------------------------------------ */

struct StubInfo { const char *cls; int slot; };

static void __cdecl B2Unimpl(StubInfo *si, void *caller)
{
    char key[64];
    _snprintf(key, sizeof(key), "%s.%d.%p", si->cls, si->slot, caller); key[63] = 0;
    B2LogOnce(key, "UNIMPLEMENTED %s slot %d (+0x%X) called from %p", si->cls, si->slot, si->slot * 4, caller);
}

void **B2Stubs(const char *cls, int n)
{
    void **vt = (void **)calloc(n, sizeof(void *));
    unsigned char *code = (unsigned char *)VirtualAlloc(NULL, n * 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    for (int i = 0; i < n; i++) {
        StubInfo *si = new StubInfo; si->cls = cls; si->slot = i;
        unsigned char *t = code + i * 32;
        t[0] = 0xFF; t[1] = 0x34; t[2] = 0x24;                  /* push [esp]  (caller)  */
        t[3] = 0x68; *(StubInfo **)(t + 4) = si;                /* push si               */
        t[8] = 0xE8; *(LONG *)(t + 9) = (LONG)((unsigned char *)B2Unimpl - (t + 13));
        t[13] = 0x83; t[14] = 0xC4; t[15] = 0x08;               /* add esp, 8            */
        t[16] = 0x33; t[17] = 0xC0;                             /* xor eax, eax          */
        t[18] = 0xC3;                                           /* ret                   */
        vt[i] = t;
    }
    FlushInstructionCache(GetCurrentProcess(), code, n * 32);
    return vt;
}

#define SELF(T) T *self = (T *)t
#define V(f) ((void *)(f))

/* ------------------------------------------------------------------------ */
/* Mesh wrappers                                                             */
/* ------------------------------------------------------------------------ */

static std::map<void *, void *> g_meshMap;     /* 2.8 mesh -> wrapper */

WTriMesh   *TriMeshWrapperOf(NxTriangleMesh *m) { return m ? (WTriMesh *)g_meshMap[m] : 0; }
WConvex    *ConvexWrapperOf(NxConvexMesh *m)    { return m ? (WConvex *)g_meshMap[m] : 0; }
WClothMesh *ClothMeshWrapperOf(NxClothMesh *m)  { return m ? (WClothMesh *)g_meshMap[m] : 0; }

static NxU32 FC tm_getRefCount(void *t, EDX)  { SELF(WTriMesh); return self->m->getReferenceCount(); }
static NxU32 FC tm_getSubmeshCount(void *t, EDX) { SELF(WTriMesh); return self->m->getSubmeshCount(); }
static NxU32 FC tm_getCount(void *t, EDX, NxU32 sm, NxU32 a) { SELF(WTriMesh); return self->m->getCount(sm, (NxInternalArray)a); }
static NxU32 FC tm_getFormat(void *t, EDX, NxU32 sm, NxU32 a) { SELF(WTriMesh); return self->m->getFormat(sm, (NxInternalArray)a); }
static const void *FC tm_getBase(void *t, EDX, NxU32 sm, NxU32 a) { SELF(WTriMesh); return self->m->getBase(sm, (NxInternalArray)a); }
static NxU32 FC tm_getStride(void *t, EDX, NxU32 sm, NxU32 a) { SELF(WTriMesh); return self->m->getStride(sm, (NxInternalArray)a); }

static NxU32 FC cm_getRefCount(void *t, EDX)  { SELF(WConvex); return self->m->getReferenceCount(); }
static NxU32 FC cm_getSubmeshCount(void *t, EDX) { SELF(WConvex); return self->m->getSubmeshCount(); }
static NxU32 FC cm_getCount(void *t, EDX, NxU32 sm, NxU32 a) { SELF(WConvex); return self->m->getCount(sm, (NxInternalArray)a); }
static NxU32 FC cm_getFormat(void *t, EDX, NxU32 sm, NxU32 a) { SELF(WConvex); return self->m->getFormat(sm, (NxInternalArray)a); }
static const void *FC cm_getBase(void *t, EDX, NxU32 sm, NxU32 a) { SELF(WConvex); return self->m->getBase(sm, (NxInternalArray)a); }
static NxU32 FC cm_getStride(void *t, EDX, NxU32 sm, NxU32 a) { SELF(WConvex); return self->m->getStride(sm, (NxInternalArray)a); }

static NxU32 FC clm_saveToDesc(void *t, EDX, ClothMeshDesc25 *d)
{
    SELF(WClothMesh);
    NxClothMeshDesc o;
    bool ok = self->m->saveToDesc(o);
    d->numVertices = o.numVertices; d->numTriangles = o.numTriangles;
    d->pointStrideBytes = o.pointStrideBytes; d->triangleStrideBytes = o.triangleStrideBytes;
    d->points = o.points; d->triangles = o.triangles; d->flags = o.flags;
    d->vertexMassStrideBytes = o.vertexMassStrideBytes; d->vertexFlagStrideBytes = o.vertexFlagStrideBytes;
    d->vertexMasses = o.vertexMasses; d->vertexFlags = o.vertexFlags;
    return ok;
}
static NxU32 FC clm_getRefCount(void *t, EDX) { SELF(WClothMesh); return self->m->getReferenceCount(); }

void *NewMeshWrapper(void *real, int kind)          /* 0 tri, 1 convex, 2 cloth */
{
    /* all three wrappers are { vtbl, real } - allocated with malloc */
    void **w;
    if (!real) return 0;
    w = (void **)malloc(2 * sizeof(void *));
    w[0] = kind == 0 ? (void *)vt_trimesh : kind == 1 ? (void *)vt_convex : (void *)vt_clothmesh;
    w[1] = real;
    g_meshMap[real] = w;
    return w;
}
void DeleteMeshWrapper(void *real) { void *w = g_meshMap[real]; g_meshMap.erase(real); free(w); }

/* ------------------------------------------------------------------------ */
/* Material                                                                  */
/* ------------------------------------------------------------------------ */

WMaterial *MaterialWrapper(WScene *sc, NxMaterial *m)
{
    if (!m) return 0;
    if (m->userData) return (WMaterial *)m->userData;
    WMaterial *w = new WMaterial; w->vtbl = vt_material; w->userData = 0; w->m = m; w->scene = sc;
    m->userData = w;
    return w;
}

#define MAT(t) (((WMaterial *)(t))->m)
static NxU16 FC mt_getIndex(void *t, EDX)                 { return MAT(t)->getMaterialIndex(); }
static void  FC mt_load(void *t, EDX, MaterialDesc25 *d)  { NxMaterialDesc o; Material25To28(d, o); MAT(t)->loadFromDesc(o); }
static void  FC mt_save(void *t, EDX, MaterialDesc25 *d)  { NxMaterialDesc o; MAT(t)->saveToDesc(o); Material28To25(o, d); }
static void *FC mt_getScene(void *t, EDX)                 { return ((WMaterial *)t)->scene; }
extern float g_frictionScale, g_staticFrictionScale;
static void  FC mt_setDynF(void *t, EDX, NxReal v)        { MAT(t)->setDynamicFriction(v * g_frictionScale); B2Log("material %u setDynamicFriction(%.3f)", MAT(t)->getMaterialIndex(), v); }
static float FC mt_getDynF(void *t, EDX)                  { return MAT(t)->getDynamicFriction(); }
static void  FC mt_setStaF(void *t, EDX, NxReal v)        { MAT(t)->setStaticFriction(v * g_frictionScale * g_staticFrictionScale); B2Log("material %u setStaticFriction(%.3f)", MAT(t)->getMaterialIndex(), v); }
static float FC mt_getStaF(void *t, EDX)                  { return MAT(t)->getStaticFriction(); }
static void  FC mt_setRest(void *t, EDX, NxReal v)        { MAT(t)->setRestitution(v); B2Log("material %u setRestitution(%.3f)", MAT(t)->getMaterialIndex(), v); }
static float FC mt_getRest(void *t, EDX)                  { return MAT(t)->getRestitution(); }
static void  FC mt_setDynFV(void *t, EDX, NxReal v)       { MAT(t)->setDynamicFrictionV(v); }
static float FC mt_getDynFV(void *t, EDX)                 { return MAT(t)->getDynamicFrictionV(); }
static void  FC mt_setStaFV(void *t, EDX, NxReal v)       { MAT(t)->setStaticFrictionV(v); }
static float FC mt_getStaFV(void *t, EDX)                 { return MAT(t)->getStaticFrictionV(); }
static void  FC mt_setDir(void *t, EDX, const NxVec3 *v)  { MAT(t)->setDirOfAnisotropy(*v); }
static NxVec3 *FC mt_getDir(void *t, EDX, NxVec3 *r)      { *r = MAT(t)->getDirOfAnisotropy(); return r; }
static void  FC mt_setFlags(void *t, EDX, NxU32 f)        { MAT(t)->setFlags(f); }
static NxU32 FC mt_getFlags(void *t, EDX)                 { return MAT(t)->getFlags(); }
static void  FC mt_setFCM(void *t, EDX, NxU32 m)          { MAT(t)->setFrictionCombineMode((NxCombineMode)m); }
static NxU32 FC mt_getFCM(void *t, EDX)                   { return MAT(t)->getFrictionCombineMode(); }
static void  FC mt_setRCM(void *t, EDX, NxU32 m)          { MAT(t)->setRestitutionCombineMode((NxCombineMode)m); }
static NxU32 FC mt_getRCM(void *t, EDX)                   { return MAT(t)->getRestitutionCombineMode(); }

/* ------------------------------------------------------------------------ */
/* Shapes                                                                    */
/* ------------------------------------------------------------------------ */

#define SH(t) (((WShape *)(t))->s)
static void *FC sh_getActor(void *t, EDX)                 { return ((WShape *)t)->actor; }
static void  FC sh_setGroup(void *t, EDX, NxU32 g)        { SH(t)->setGroup((NxCollisionGroup)g); }
static NxU32 FC sh_getGroup(void *t, EDX)                 { return SH(t)->getGroup(); }
static void  FC sh_getWorldBounds(void *t, EDX, NxBounds3 *b) { SH(t)->getWorldBounds(*b); }
static void  FC sh_setFlag(void *t, EDX, NxU32 f, NxU32 v){ SH(t)->setFlag((NxShapeFlag)f, (v & 0xFF) != 0); }
static NxU32 FC sh_getFlag(void *t, EDX, NxU32 f)         { return SH(t)->getFlag((NxShapeFlag)f); }
static void  FC sh_setLocalPose(void *t, EDX, const NxMat34 *m)  { SH(t)->setLocalPose(*m); }
static void  FC sh_setLocalPos(void *t, EDX, const NxVec3 *v)    { SH(t)->setLocalPosition(*v); }
static void  FC sh_setLocalOri(void *t, EDX, const NxMat33 *m)   { SH(t)->setLocalOrientation(*m); }
static NxMat34 *FC sh_getLocalPose(void *t, EDX, NxMat34 *r)     { *r = SH(t)->getLocalPose(); return r; }
static NxVec3  *FC sh_getLocalPos(void *t, EDX, NxVec3 *r)       { *r = SH(t)->getLocalPosition(); return r; }
static NxMat33 *FC sh_getLocalOri(void *t, EDX, NxMat33 *r)      { *r = SH(t)->getLocalOrientation(); return r; }
static void  FC sh_setGlobalPose(void *t, EDX, const NxMat34 *m) { SH(t)->setGlobalPose(*m); }
static void  FC sh_setGlobalPos(void *t, EDX, const NxVec3 *v)   { SH(t)->setGlobalPosition(*v); }
static void  FC sh_setGlobalOri(void *t, EDX, const NxMat33 *m)  { SH(t)->setGlobalOrientation(*m); }
static NxMat34 *FC sh_getGlobalPose(void *t, EDX, NxMat34 *r)    { *r = SH(t)->getGlobalPose(); return r; }
static NxVec3  *FC sh_getGlobalPos(void *t, EDX, NxVec3 *r)      { *r = SH(t)->getGlobalPosition(); return r; }
static NxMat33 *FC sh_getGlobalOri(void *t, EDX, NxMat33 *r)     { *r = SH(t)->getGlobalOrientation(); return r; }
static void  FC sh_setMaterial(void *t, EDX, NxU32 m)     { SH(t)->setMaterial((NxMaterialIndex)m); }
static NxU32 FC sh_getMaterial(void *t, EDX)              { return SH(t)->getMaterial(); }
static void  FC sh_setSkin(void *t, EDX, NxReal v)        { SH(t)->setSkinWidth(v); }
static float FC sh_getSkin(void *t, EDX)                  { return SH(t)->getSkinWidth(); }
static NxU32 FC sh_getType(void *t, EDX)                  { return SH(t)->getType(); }
static void  FC sh_setCCD(void *t, EDX, void *s)          { (void)t; (void)s; }
static void *FC sh_getCCD(void *t, EDX)                   { (void)t; return 0; }
static void  FC sh_setName(void *t, EDX, const char *n)   { SH(t)->setName(n); }
static const char *FC sh_getName(void *t, EDX)            { return SH(t)->getName(); }
static NxU32 FC sh_ovlSphere(void *t, EDX, const NxSphere *s)   { return SH(t)->checkOverlapSphere(*s); }
static NxU32 FC sh_ovlOBB(void *t, EDX, const NxBox *b)         { return SH(t)->checkOverlapOBB(*b); }
static NxU32 FC sh_ovlAABB(void *t, EDX, const NxBounds3 *b)    { return SH(t)->checkOverlapAABB(*b); }
static NxU32 FC sh_ovlCapsule(void *t, EDX, const NxCapsule *c) { return SH(t)->checkOverlapCapsule(*c); }
static void  FC sh_setGroupsMask(void *t, EDX, const NxGroupsMask *m) { SH(t)->setGroupsMask(*m); }
static NxGroupsMask *FC sh_getGroupsMask(void *t, EDX, NxGroupsMask *r) { *r = SH(t)->getGroupsMask(); return r; }

/* subclasses */
static void  FC box_setDim(void *t, EDX, const NxVec3 *v)      { SH(t)->isBox()->setDimensions(*v); }
static NxVec3 *FC box_getDim(void *t, EDX, NxVec3 *r)          { *r = SH(t)->isBox()->getDimensions(); return r; }
static void  FC box_getOBB(void *t, EDX, NxBox *b)             { SH(t)->isBox()->getWorldOBB(*b); }
static void  FC sph_setRadius(void *t, EDX, NxReal r)          { SH(t)->isSphere()->setRadius(r); }
static float FC sph_getRadius(void *t, EDX)                    { return SH(t)->isSphere()->getRadius(); }
static void  FC sph_getWorld(void *t, EDX, NxSphere *s)        { SH(t)->isSphere()->getWorldSphere(*s); }
static void  FC cap_setDims(void *t, EDX, NxReal r, NxReal h)  { SH(t)->isCapsule()->setDimensions(r, h); }
static void  FC cap_setRadius(void *t, EDX, NxReal r)          { SH(t)->isCapsule()->setRadius(r); }
static float FC cap_getRadius(void *t, EDX)                    { return SH(t)->isCapsule()->getRadius(); }
static void  FC cap_setHeight(void *t, EDX, NxReal h)          { SH(t)->isCapsule()->setHeight(h); }
static float FC cap_getHeight(void *t, EDX)                    { return SH(t)->isCapsule()->getHeight(); }
static void  FC cap_getWorld(void *t, EDX, NxCapsule *c)       { SH(t)->isCapsule()->getWorldCapsule(*c); }
static void *FC cvx_getMesh(void *t, EDX)                      { return ConvexWrapperOf(&SH(t)->isConvexMesh()->getConvexMesh()); }
static void *FC trm_getMesh(void *t, EDX)                      { return TriMeshWrapperOf(&SH(t)->isTriangleMesh()->getTriangleMesh()); }
static void  FC any_saveToDesc(void *t, EDX, ShapeDesc25 *d)   { Shape28To25(SH(t), d); }

WShape *NewShapeWrapper(NxShape *s, WActor *owner, void *userData)
{
    WShape *w = new WShape;
    w->type = s->getType();
    w->vtbl = vt_shape[w->type < 8 ? w->type : 0];
    w->userData = userData; w->appData = 0; w->s = s; w->actor = owner;
    s->userData = w;
    return w;
}

/* ------------------------------------------------------------------------ */
/* Actor                                                                     */
/* ------------------------------------------------------------------------ */

#define ACT(t) (((WActor *)(t))->a)

WActor *NewActorWrapper(NxActor *a, WScene *scene, const ActorDesc25 *d)
{
    WActor *w = new WActor;
    w->vtbl = vt_actor; w->userData = d ? d->userData : 0; w->a = a; w->scene = scene;
    w->shapes = new std::vector<WShape *>;
    w->dynamic = a->isDynamic();
    w->kinematic = w->dynamic && a->readBodyFlag(NX_BF_KINEMATIC);
    a->userData = w;
    NxU32 n = a->getNbShapes();
    NxShape *const *ss = a->getShapes();
    for (NxU32 i = 0; i < n; i++) {
        void *ud = 0;
        if (d && d->shapesBegin && d->shapesBegin + i < d->shapesEnd) ud = d->shapesBegin[i]->userData;
        w->shapes->push_back(NewShapeWrapper(ss[i], w, ud));
    }
    return w;
}

void DeleteActorWrapper(WActor *w)
{
    for (size_t i = 0; i < w->shapes->size(); i++) delete (*w->shapes)[i];
    delete w->shapes;
    delete w;
}

static void *FC ac_getScene(void *t, EDX)                          { return ((WActor *)t)->scene; }
static void FC ac_saveToDesc(void *t, EDX, ActorDesc25 *d)
{
    NxActorDesc o; ACT(t)->saveToDesc(o);
    d->globalPose = o.globalPose; d->density = o.density; d->flags = o.flags;
    d->group = o.group; d->userData = ((WActor *)t)->userData; d->name = o.name; d->compartment = 0;
}
static void  FC ac_setName(void *t, EDX, const char *n)            { ACT(t)->setName(n); }
static const char *FC ac_getName(void *t, EDX)                     { return ACT(t)->getName(); }
static void  FC ac_setGlobalPose(void *t, EDX, const NxMat34 *m)   { ACT(t)->setGlobalPose(*m); }
static void  FC ac_setGlobalPos(void *t, EDX, const NxVec3 *v)     { ACT(t)->setGlobalPosition(*v); }
static void  FC ac_setGlobalOri(void *t, EDX, const NxMat33 *m)    { ACT(t)->setGlobalOrientation(*m); }
static void  FC ac_setGlobalOriQ(void *t, EDX, const NxQuat *q)    { ACT(t)->setGlobalOrientationQuat(*q); }
static NxMat34 *FC ac_getGlobalPose(void *t, EDX, NxMat34 *r)      { *r = ACT(t)->getGlobalPose(); return r; }
static NxVec3  *FC ac_getGlobalPos(void *t, EDX, NxVec3 *r)        { *r = ACT(t)->getGlobalPosition(); return r; }
static NxMat33 *FC ac_getGlobalOri(void *t, EDX, NxMat33 *r)       { *r = ACT(t)->getGlobalOrientation(); return r; }
static NxQuat  *FC ac_getGlobalOriQ(void *t, EDX, NxQuat *r)       { *r = ACT(t)->getGlobalOrientationQuat(); return r; }
static void  FC ac_moveGlobalPose(void *t, EDX, const NxMat34 *m)  { SELF(WActor); if (self->kinematic) self->a->moveGlobalPose(*m); else self->a->setGlobalPose(*m); }
static void  FC ac_moveGlobalPos(void *t, EDX, const NxVec3 *v)    { SELF(WActor); if (self->kinematic) self->a->moveGlobalPosition(*v); else self->a->setGlobalPosition(*v); }
static void  FC ac_moveGlobalOri(void *t, EDX, const NxMat33 *m)   { SELF(WActor); if (self->kinematic) self->a->moveGlobalOrientation(*m); else self->a->setGlobalOrientation(*m); }
static void  FC ac_moveGlobalOriQ(void *t, EDX, const NxQuat *q)   { SELF(WActor); if (self->kinematic) self->a->moveGlobalOrientationQuat(*q); else self->a->setGlobalOrientationQuat(*q); }
static void *FC ac_createShape(void *t, EDX, const ShapeDesc25 *d)
{
    SELF(WActor);
    std::vector<void *> owned;
    NxShapeDesc *sd = Shape25To28(d, owned);
    if (sd && self->dynamic && g_clothTwoWay) sd->shapeFlags |= NX_SF_CLOTH_TWOWAY;
    if (sd && self->dynamic && g_hwMode)      sd->shapeFlags |= NX_SF_FLUID_TWOWAY;
    NxShape *s = sd ? self->a->createShape(*sd) : 0;
    for (size_t i = 0; i < owned.size(); i++) delete (NxShapeDesc *)owned[i];
    if (!s) { B2Log("actor.createShape(type %u) FAILED", d->type); return 0; }
    WShape *w = NewShapeWrapper(s, self, d->userData);
    self->shapes->push_back(w);
    return w;
}
static void FC ac_releaseShape(void *t, EDX, WShape *s)
{
    SELF(WActor);
    for (size_t i = 0; i < self->shapes->size(); i++)
        if ((*self->shapes)[i] == s) { self->shapes->erase(self->shapes->begin() + i); break; }
    self->a->releaseShape(*s->s);
    delete s;
}
static NxU32 FC ac_getNbShapes(void *t, EDX)                       { return (NxU32)((WActor *)t)->shapes->size(); }
static void *FC ac_getShapes(void *t, EDX)
{ SELF(WActor); return self->shapes->empty() ? 0 : &(*self->shapes)[0]; }
static void  FC ac_setGroup(void *t, EDX, NxU32 g)                 { ACT(t)->setGroup((NxActorGroup)g); }
static NxU32 FC ac_getGroup(void *t, EDX)                          { return ACT(t)->getGroup(); }
static void  FC ac_raiseActorFlag(void *t, EDX, NxU32 f)           { ACT(t)->raiseActorFlag((NxActorFlag)f); }
static void  FC ac_clearActorFlag(void *t, EDX, NxU32 f)           { ACT(t)->clearActorFlag((NxActorFlag)f); }
static NxU32 FC ac_readActorFlag(void *t, EDX, NxU32 f)            { return ACT(t)->readActorFlag((NxActorFlag)f); }
static NxU32 FC ac_isDynamic(void *t, EDX)                         { return ACT(t)->isDynamic(); }
static void  FC ac_setCMOLP(void *t, EDX, const NxMat34 *m)        { if (!((WActor *)t)->dynamic) return;  ACT(t)->setCMassOffsetLocalPose(*m); }
static void  FC ac_setCMOLPos(void *t, EDX, const NxVec3 *v)       { if (!((WActor *)t)->dynamic) return;  ACT(t)->setCMassOffsetLocalPosition(*v); }
static void  FC ac_setCMOLOri(void *t, EDX, const NxMat33 *m)      { if (!((WActor *)t)->dynamic) return;  ACT(t)->setCMassOffsetLocalOrientation(*m); }
static void  FC ac_setCMOGP(void *t, EDX, const NxMat34 *m)        { if (!((WActor *)t)->dynamic) return;  ACT(t)->setCMassOffsetGlobalPose(*m); }
static void  FC ac_setCMOGPos(void *t, EDX, const NxVec3 *v)       { if (!((WActor *)t)->dynamic) return;  ACT(t)->setCMassOffsetGlobalPosition(*v); }
static void  FC ac_setCMOGOri(void *t, EDX, const NxMat33 *m)      { if (!((WActor *)t)->dynamic) return;  ACT(t)->setCMassOffsetGlobalOrientation(*m); }
static void  FC ac_setCMGP(void *t, EDX, const NxMat34 *m)         { if (!((WActor *)t)->dynamic) return;  ACT(t)->setCMassGlobalPose(*m); }
static void  FC ac_setCMGPos(void *t, EDX, const NxVec3 *v)        { if (!((WActor *)t)->dynamic) return;  ACT(t)->setCMassGlobalPosition(*v); }
static void  FC ac_setCMGOri(void *t, EDX, const NxMat33 *m)       { if (!((WActor *)t)->dynamic) return;  ACT(t)->setCMassGlobalOrientation(*m); }
static NxMat34 *FC ac_getCMLP(void *t, EDX, NxMat34 *r)            { *r = ACT(t)->getCMassLocalPose(); return r; }
static NxVec3  *FC ac_getCMLPos(void *t, EDX, NxVec3 *r)           { *r = ACT(t)->getCMassLocalPosition(); return r; }
static NxMat33 *FC ac_getCMLOri(void *t, EDX, NxMat33 *r)          { *r = ACT(t)->getCMassLocalOrientation(); return r; }
static NxMat34 *FC ac_getCMGP(void *t, EDX, NxMat34 *r)            { *r = ACT(t)->getCMassGlobalPose(); return r; }
static NxVec3  *FC ac_getCMGPos(void *t, EDX, NxVec3 *r)           { *r = ACT(t)->getCMassGlobalPosition(); return r; }
static NxMat33 *FC ac_getCMGOri(void *t, EDX, NxMat33 *r)          { *r = ACT(t)->getCMassGlobalOrientation(); return r; }
static void  FC ac_setMass(void *t, EDX, NxReal m)                 { if (!((WActor *)t)->dynamic) return;  ACT(t)->setMass(m); }
static float FC ac_getMass(void *t, EDX)                           { if (!((WActor *)t)->dynamic) return 0.0f; return ACT(t)->getMass(); }
static void  FC ac_setMSIT(void *t, EDX, const NxVec3 *v)          { if (!((WActor *)t)->dynamic) return;  ACT(t)->setMassSpaceInertiaTensor(*v); }
static NxVec3  *FC ac_getMSIT(void *t, EDX, NxVec3 *r)             { *r = ACT(t)->getMassSpaceInertiaTensor(); return r; }
static NxMat33 *FC ac_getGIT(void *t, EDX, NxMat33 *r)             { *r = ACT(t)->getGlobalInertiaTensor(); return r; }
static NxMat33 *FC ac_getGITI(void *t, EDX, NxMat33 *r)            { *r = ACT(t)->getGlobalInertiaTensorInverse(); return r; }
static void  FC ac_updateMass(void *t, EDX, NxReal d, NxReal m)    { if (!((WActor *)t)->dynamic) return;  ACT(t)->updateMassFromShapes(d, m); }
static void  FC ac_setLinDamp(void *t, EDX, NxReal v)              { if (!((WActor *)t)->dynamic) return;  ACT(t)->setLinearDamping(v); }
static float FC ac_getLinDamp(void *t, EDX)                        { if (!((WActor *)t)->dynamic) return 0.0f; return ACT(t)->getLinearDamping(); }
static void  FC ac_setAngDamp(void *t, EDX, NxReal v)              { if (!((WActor *)t)->dynamic) return;  ACT(t)->setAngularDamping(v); }
static float FC ac_getAngDamp(void *t, EDX)                        { if (!((WActor *)t)->dynamic) return 0.0f; return ACT(t)->getAngularDamping(); }
static void  FC ac_setLinVel(void *t, EDX, const NxVec3 *v)        { if (!((WActor *)t)->dynamic || ((WActor *)t)->kinematic) return;  ACT(t)->setLinearVelocity(*v); }
static void  FC ac_setAngVel(void *t, EDX, const NxVec3 *v)        { if (!((WActor *)t)->dynamic || ((WActor *)t)->kinematic) return;  ACT(t)->setAngularVelocity(*v); }
static NxVec3 *FC ac_getLinVel(void *t, EDX, NxVec3 *r)
{ if (((WActor *)t)->dynamic) *r = ACT(t)->getLinearVelocity(); else r->zero(); return r; }
static NxVec3 *FC ac_getAngVel(void *t, EDX, NxVec3 *r)
{ if (((WActor *)t)->dynamic) *r = ACT(t)->getAngularVelocity(); else r->zero(); return r; }
/* Switchball passes maxAngularVelocity SQUARED when "Physic - HW installed" is set
   (0x43B93B, 0x4451F5, 0x448AF8: v*v for the PPU, v otherwise) - the PPU
   evidently compared the squared angular speed. Our hardware scenes run on the
   software solver, which wants the plain value. */
static void  FC ac_setMaxAngVel(void *t, EDX, NxReal v)
{
    SELF(WActor);
    if (!self->dynamic) return;
    if (self->scene && self->scene->hw && v > 0) v = sqrtf(v);
    self->a->setMaxAngularVelocity(v);
}
static float FC ac_getMaxAngVel(void *t, EDX)
{
    SELF(WActor);
    if (!self->dynamic) return 0.0f;
    float v = self->a->getMaxAngularVelocity();
    return (self->scene && self->scene->hw && v > 0) ? v * v : v;
}
static void  FC ac_setCCD(void *t, EDX, NxReal v)                  { if (!((WActor *)t)->dynamic) return;  ACT(t)->setCCDMotionThreshold(v); }
static float FC ac_getCCD(void *t, EDX)                            { if (!((WActor *)t)->dynamic) return 0.0f; return ACT(t)->getCCDMotionThreshold(); }
static void  FC ac_setLinMom(void *t, EDX, const NxVec3 *v)        { if (!((WActor *)t)->dynamic || ((WActor *)t)->kinematic) return;  ACT(t)->setLinearMomentum(*v); }
static void  FC ac_setAngMom(void *t, EDX, const NxVec3 *v)        { if (!((WActor *)t)->dynamic || ((WActor *)t)->kinematic) return;  ACT(t)->setAngularMomentum(*v); }
static NxVec3 *FC ac_getLinMom(void *t, EDX, NxVec3 *r)            { *r = ACT(t)->getLinearMomentum(); return r; }
static NxVec3 *FC ac_getAngMom(void *t, EDX, NxVec3 *r)            { *r = ACT(t)->getAngularMomentum(); return r; }
/* 2.5: force methods take (vector(s), mode) - no 'wakeup' argument (Switchball.exe 0x42A1EA) */
static void FC ac_addForceAtPos(void *t, EDX, const NxVec3 *f, const NxVec3 *p, NxU32 m)        { if (!((WActor *)t)->dynamic || ((WActor *)t)->kinematic) return;  ACT(t)->addForceAtPos(*f, *p, (NxForceMode)m); }
static void FC ac_addForceAtLocalPos(void *t, EDX, const NxVec3 *f, const NxVec3 *p, NxU32 m)   { if (!((WActor *)t)->dynamic || ((WActor *)t)->kinematic) return;  ACT(t)->addForceAtLocalPos(*f, *p, (NxForceMode)m); }
static void FC ac_addLocalForceAtPos(void *t, EDX, const NxVec3 *f, const NxVec3 *p, NxU32 m)   { if (!((WActor *)t)->dynamic || ((WActor *)t)->kinematic) return;  ACT(t)->addLocalForceAtPos(*f, *p, (NxForceMode)m); }
static void FC ac_addLocalForceAtLocalPos(void *t, EDX, const NxVec3 *f, const NxVec3 *p, NxU32 m) { if (!((WActor *)t)->dynamic || ((WActor *)t)->kinematic) return;  ACT(t)->addLocalForceAtLocalPos(*f, *p, (NxForceMode)m); }
static void FC ac_addForce(void *t, EDX, const NxVec3 *f, NxU32 m)        { if (!((WActor *)t)->dynamic || ((WActor *)t)->kinematic) return;  ACT(t)->addForce(*f, (NxForceMode)m); }
static void FC ac_addLocalForce(void *t, EDX, const NxVec3 *f, NxU32 m)   { if (!((WActor *)t)->dynamic || ((WActor *)t)->kinematic) return;  ACT(t)->addLocalForce(*f, (NxForceMode)m); }
static void FC ac_addTorque(void *t, EDX, const NxVec3 *f, NxU32 m)       { if (!((WActor *)t)->dynamic || ((WActor *)t)->kinematic) return;  ACT(t)->addTorque(*f, (NxForceMode)m); }
static void FC ac_addLocalTorque(void *t, EDX, const NxVec3 *f, NxU32 m)  { if (!((WActor *)t)->dynamic || ((WActor *)t)->kinematic) return;  ACT(t)->addLocalTorque(*f, (NxForceMode)m); }
static float FC ac_kinetic(void *t, EDX)                           { if (!((WActor *)t)->dynamic) return 0.0f; return ACT(t)->computeKineticEnergy(); }
static NxVec3 *FC ac_getPointVel(void *t, EDX, NxVec3 *r, const NxVec3 *p)
{ if (((WActor *)t)->dynamic) *r = ACT(t)->getPointVelocity(*p); else r->zero(); return r; }
static NxVec3 *FC ac_getLocalPointVel(void *t, EDX, NxVec3 *r, const NxVec3 *p)
{ if (((WActor *)t)->dynamic) *r = ACT(t)->getLocalPointVelocity(*p); else r->zero(); return r; }
static NxU32 FC ac_isGroupSleeping(void *t, EDX)                   { if (!((WActor *)t)->dynamic) return 0; return ACT(t)->isGroupSleeping(); }
static NxU32 FC ac_isSleeping(void *t, EDX)                        { if (!((WActor *)t)->dynamic) return 0; return ACT(t)->isSleeping(); }
static float FC ac_getSleepLin(void *t, EDX)                       { if (!((WActor *)t)->dynamic) return 0.0f; return ACT(t)->getSleepLinearVelocity(); }
static void  FC ac_setSleepLin(void *t, EDX, NxReal v)             { if (!((WActor *)t)->dynamic) return;  ACT(t)->setSleepLinearVelocity(v); }
static float FC ac_getSleepAng(void *t, EDX)                       { if (!((WActor *)t)->dynamic) return 0.0f; return ACT(t)->getSleepAngularVelocity(); }
static void  FC ac_setSleepAng(void *t, EDX, NxReal v)             { if (!((WActor *)t)->dynamic) return;  ACT(t)->setSleepAngularVelocity(v); }
static void  FC ac_wakeUp(void *t, EDX, NxReal v)                  { if (!((WActor *)t)->dynamic) return;  ACT(t)->wakeUp(v); }
static void  FC ac_putToSleep(void *t, EDX)                        { if (!((WActor *)t)->dynamic) return;  ACT(t)->putToSleep(); }
static void  FC ac_raiseBodyFlag(void *t, EDX, NxU32 f)
{ SELF(WActor); if (!self->dynamic) return; self->a->raiseBodyFlag((NxBodyFlag)f); if (f & NX_BF_KINEMATIC) self->kinematic = 1; }
static void  FC ac_clearBodyFlag(void *t, EDX, NxU32 f)
{ SELF(WActor); if (!self->dynamic) return; self->a->clearBodyFlag((NxBodyFlag)f); if (f & NX_BF_KINEMATIC) self->kinematic = 0; }
static NxU32 FC ac_readBodyFlag(void *t, EDX, NxU32 f)             { if (!((WActor *)t)->dynamic) return 0; return ACT(t)->readBodyFlag((NxBodyFlag)f); }
static NxU32 FC ac_saveBodyToDesc(void *t, EDX, BodyDesc25 *d)
{ if (!((WActor *)t)->dynamic) return 0; NxBodyDesc o; bool ok = ACT(t)->saveBodyToDesc(o); if (ok) Body28To25(o, d); return ok; }
static void  FC ac_setSolverIter(void *t, EDX, NxU32 n)            { if (!((WActor *)t)->dynamic) return;  ACT(t)->setSolverIterationCount(n); }
static NxU32 FC ac_getSolverIter(void *t, EDX)                     { if (!((WActor *)t)->dynamic) return 0; return ACT(t)->getSolverIterationCount(); }

/* ------------------------------------------------------------------------ */
/* Joint (D6)                                                                */
/* ------------------------------------------------------------------------ */

#define JNT(t) (((WJoint *)(t))->j)
static void FC jt_getActors(void *t, EDX, void **a1, void **a2)
{ NxActor *x = 0, *y = 0; JNT(t)->getActors(&x, &y); if (a1) *a1 = WA(x); if (a2) *a2 = WA(y); }
static void  FC jt_setGlobalAnchor(void *t, EDX, const NxVec3 *v) { JNT(t)->setGlobalAnchor(*v); }
static void  FC jt_setGlobalAxis(void *t, EDX, const NxVec3 *v)   { JNT(t)->setGlobalAxis(*v); }
static NxVec3 *FC jt_getGlobalAnchor(void *t, EDX, NxVec3 *r)     { *r = JNT(t)->getGlobalAnchor(); return r; }
static NxVec3 *FC jt_getGlobalAxis(void *t, EDX, NxVec3 *r)       { *r = JNT(t)->getGlobalAxis(); return r; }
static NxU32 FC jt_getState(void *t, EDX)                         { return JNT(t)->getState(); }
static void  FC jt_setBreakable(void *t, EDX, NxReal f, NxReal q) { JNT(t)->setBreakable(f, q); }
static void  FC jt_getBreakable(void *t, EDX, NxReal *f, NxReal *q) { JNT(t)->getBreakable(*f, *q); }
extern int g_logJoints;
static void  FC jt_setLimitPoint(void *t, EDX, const NxVec3 *p, NxU32 on2)
{
    if (g_logJoints) B2Log("joint %p setLimitPoint(%.3f %.3f %.3f, onActor2 %u)", JNT(t), p->x, p->y, p->z, on2 & 0xFF);
    JNT(t)->setLimitPoint(*p, (on2 & 0xFF) != 0);
}
static NxU32 FC jt_getLimitPoint(void *t, EDX, NxVec3 *p)         { return JNT(t)->getLimitPoint(*p); }
static NxU32 FC jt_addLimitPlane(void *t, EDX, const NxVec3 *n, const NxVec3 *p)
{
    NxU32 r = JNT(t)->addLimitPlane(*n, *p);
    if (g_logJoints) B2Log("joint %p addLimitPlane(n %.3f %.3f %.3f, p %.3f %.3f %.3f) -> %u",
                           JNT(t), n->x, n->y, n->z, p->x, p->y, p->z, r);
    return r;
}
static void  FC jt_purgeLimitPlanes(void *t, EDX)                 { JNT(t)->purgeLimitPlanes(); }
static void  FC jt_resetLimitPlaneIt(void *t, EDX)                { JNT(t)->resetLimitPlaneIterator(); }
static NxU32 FC jt_hasMoreLimitPlanes(void *t, EDX)               { return JNT(t)->hasMoreLimitPlanes(); }
static NxU32 FC jt_getNextLimitPlane(void *t, EDX, NxVec3 *n, NxReal *d) { return JNT(t)->getNextLimitPlane(*n, *d); }
static NxU32 FC jt_getType(void *t, EDX)                          { return JNT(t)->getType(); }
static void  FC jt_setName(void *t, EDX, const char *n)           { JNT(t)->setName(n); }
static const char *FC jt_getName(void *t, EDX)                    { return JNT(t)->getName(); }
static void *FC jt_getScene(void *t, EDX)                         { return ((WJoint *)t)->scene; }
static void  FC d6_load(void *t, EDX, const D6JointDesc25 *d)
{ NxD6JointDesc o; D625To28(d, o); JNT(t)->isD6Joint()->loadFromDesc(o); }
static void  FC d6_save(void *t, EDX, D6JointDesc25 *d)
{ NxD6JointDesc o; JNT(t)->isD6Joint()->saveToDesc(o); D628To25(o, d, JNT(t)); }
static void  FC d6_setDrivePos(void *t, EDX, const NxVec3 *v)     { JNT(t)->isD6Joint()->setDrivePosition(*v); }
static void  FC d6_setDriveOri(void *t, EDX, const NxQuat *q)     { JNT(t)->isD6Joint()->setDriveOrientation(*q); }
static void  FC d6_setDriveLinVel(void *t, EDX, const NxVec3 *v)  { JNT(t)->isD6Joint()->setDriveLinearVelocity(*v); }
static void  FC d6_setDriveAngVel(void *t, EDX, const NxVec3 *v)  { JNT(t)->isD6Joint()->setDriveAngularVelocity(*v); }

/* ------------------------------------------------------------------------ */
/* Cloth                                                                     */
/* ------------------------------------------------------------------------ */

#define CLO(t) (((WCloth *)(t))->c)
static NxU32 FC cl_saveToDesc(void *t, EDX, ClothDesc25 *d)
{
    NxClothDesc o; bool ok = CLO(t)->saveToDesc(o);
    d->clothMesh = ClothMeshWrapperOf(o.clothMesh); d->globalPose = o.globalPose;
    d->thickness = o.thickness; d->density = o.density; d->bendingStiffness = o.bendingStiffness;
    d->stretchingStiffness = o.stretchingStiffness; d->dampingCoefficient = o.dampingCoefficient;
    d->friction = o.friction; d->pressure = o.pressure; d->tearFactor = o.tearFactor;
    d->collisionResponseCoefficient = o.collisionResponseCoefficient;
    d->attachmentResponseCoefficient = o.attachmentResponseCoefficient;
    d->attachmentTearFactor = o.attachmentTearFactor; d->solverIterations = o.solverIterations;
    d->externalAcceleration = o.externalAcceleration; d->wakeUpCounter = o.wakeUpCounter;
    d->sleepLinearVelocity = o.sleepLinearVelocity; MeshData28To25(o.meshData, &d->meshData);
    d->collisionGroup = o.collisionGroup; d->groupsMask = o.groupsMask; d->flags = o.flags;
    d->userData = ((WCloth *)t)->userData; d->name = o.name;
    return ok;
}
static void *FC cl_getClothMesh(void *t, EDX)                      { return ClothMeshWrapperOf(CLO(t)->getClothMesh()); }
static void  FC cl_setBend(void *t, EDX, NxReal v)                 { CLO(t)->setBendingStiffness(v); }
static float FC cl_getBend(void *t, EDX)                           { return CLO(t)->getBendingStiffness(); }
static void  FC cl_setStretch(void *t, EDX, NxReal v)              { CLO(t)->setStretchingStiffness(v); }
static float FC cl_getStretch(void *t, EDX)                        { return CLO(t)->getStretchingStiffness(); }
static void  FC cl_setDamp(void *t, EDX, NxReal v)                 { CLO(t)->setDampingCoefficient(v); }
static float FC cl_getDamp(void *t, EDX)                           { return CLO(t)->getDampingCoefficient(); }
static void  FC cl_setFriction(void *t, EDX, NxReal v)             { CLO(t)->setFriction(v); }
static float FC cl_getFriction(void *t, EDX)                       { return CLO(t)->getFriction(); }
static void  FC cl_setPressure(void *t, EDX, NxReal v)             { CLO(t)->setPressure(v); }
static float FC cl_getPressure(void *t, EDX)                       { return CLO(t)->getPressure(); }
static void  FC cl_setTear(void *t, EDX, NxReal v)                 { CLO(t)->setTearFactor(v); }
static float FC cl_getTear(void *t, EDX)                           { return CLO(t)->getTearFactor(); }
static void  FC cl_setAttTear(void *t, EDX, NxReal v)              { CLO(t)->setAttachmentTearFactor(v); }
static float FC cl_getAttTear(void *t, EDX)                        { return CLO(t)->getAttachmentTearFactor(); }
static void  FC cl_setThickness(void *t, EDX, NxReal v)            { CLO(t)->setThickness(v); }
static float FC cl_getThickness(void *t, EDX)                      { return CLO(t)->getThickness(); }
static float FC cl_getDensity(void *t, EDX)                        { return CLO(t)->getDensity(); }
static NxU32 FC cl_getSolverIter(void *t, EDX)                     { return CLO(t)->getSolverIterations(); }
static void  FC cl_setSolverIter(void *t, EDX, NxU32 n)            { CLO(t)->setSolverIterations(n); }
static void  FC cl_getWorldBounds(void *t, EDX, NxBounds3 *b)      { CLO(t)->getWorldBounds(*b); }
static void  FC cl_attachToShape(void *t, EDX, void *s, NxU32 f)   { CLO(t)->attachToShape(RS(s), f); }
static void  FC cl_attachToColliding(void *t, EDX, NxU32 f)        { CLO(t)->attachToCollidingShapes(f); }
static void  FC cl_detachFromShape(void *t, EDX, void *s)          { CLO(t)->detachFromShape(RS(s)); }
static void  FC cl_attachVertexToShape(void *t, EDX, NxU32 v, void *s, const NxVec3 *p, NxU32 f) { CLO(t)->attachVertexToShape(v, RS(s), *p, f); }
static void  FC cl_attachVertexToPos(void *t, EDX, NxU32 v, const NxVec3 *p) { CLO(t)->attachVertexToGlobalPosition(v, *p); }
static void  FC cl_freeVertex(void *t, EDX, NxU32 v)               { CLO(t)->freeVertex(v); }
static void  FC cl_setMeshData(void *t, EDX, MeshData25 *md)
{ NxMeshData o; MeshData25To28(md, o); CLO(t)->setMeshData(o); ((WCloth *)t)->md = *md; }
static MeshData25 *FC cl_getMeshData(void *t, EDX, MeshData25 *r)
{ NxMeshData o = CLO(t)->getMeshData(); MeshData28To25(o, r); return r; }
static void  FC cl_setPositions(void *t, EDX, void *b, NxU32 st)   { CLO(t)->setPositions(b, st); }
static void  FC cl_getPositions(void *t, EDX, void *b, NxU32 st)   { CLO(t)->getPositions(b, st); }
static void  FC cl_setVelocities(void *t, EDX, void *b, NxU32 st)  { CLO(t)->setVelocities(b, st); }
static void  FC cl_getVelocities(void *t, EDX, void *b, NxU32 st)  { CLO(t)->getVelocities(b, st); }
static NxU32 FC cl_getNbParticles(void *t, EDX)                    { return CLO(t)->getNumberOfParticles(); }
static NxU32 FC cl_isSleeping(void *t, EDX)                        { return CLO(t)->isSleeping(); }
static float FC cl_getSleepLin(void *t, EDX)                       { return CLO(t)->getSleepLinearVelocity(); }
static void  FC cl_setSleepLin(void *t, EDX, NxReal v)             { CLO(t)->setSleepLinearVelocity(v); }
static void  FC cl_wakeUp(void *t, EDX, NxReal v)                  { CLO(t)->wakeUp(v); }
static void  FC cl_putToSleep(void *t, EDX)                        { CLO(t)->putToSleep(); }
static void  FC cl_setFlags(void *t, EDX, NxU32 f)                 { CLO(t)->setFlags(f & ~(NxU32)NX_CLF_HARDWARE); }
static NxU32 FC cl_getFlags(void *t, EDX)                          { return CLO(t)->getFlags(); }
static void  FC cl_setGroup(void *t, EDX, NxU32 g)                 { CLO(t)->setGroup((NxCollisionGroup)g); }
static NxU32 FC cl_getGroup(void *t, EDX)                          { return CLO(t)->getGroup(); }
static void  FC cl_setGroupsMask(void *t, EDX, const NxGroupsMask *m) { CLO(t)->setGroupsMask(*m); }
static NxGroupsMask *FC cl_getGroupsMask(void *t, EDX, NxGroupsMask *r) { *r = CLO(t)->getGroupsMask(); return r; }

/* ------------------------------------------------------------------------ */
/* vtables                                                                   */
/* ------------------------------------------------------------------------ */

void B2InitVtables_Scene();            /* in b2_scene.cpp */

void B2InitVtables()
{
    /* materials: 23 slots, same order as 2.6 */
    vt_material = B2Stubs("material", 23);
    void *mt[] = { 0, V(mt_getIndex), V(mt_load), V(mt_save), V(mt_getScene), V(mt_setDynF), V(mt_getDynF),
                   V(mt_setStaF), V(mt_getStaF), V(mt_setRest), V(mt_getRest), V(mt_setDynFV), V(mt_getDynFV),
                   V(mt_setStaFV), V(mt_getStaFV), V(mt_setDir), V(mt_getDir), V(mt_setFlags), V(mt_getFlags),
                   V(mt_setFCM), V(mt_getFCM), V(mt_setRCM), V(mt_getRCM) };
    for (int i = 1; i < 23; i++) vt_material[i] = mt[i];

    /* shapes: 35 base slots (2.6 order) + type specific + 1 trailing */
    void *base[35] = { 0, V(sh_getActor), V(sh_setGroup), V(sh_getGroup), V(sh_getWorldBounds), V(sh_setFlag),
        V(sh_getFlag), V(sh_setLocalPose), V(sh_setLocalPos), V(sh_setLocalOri), V(sh_getLocalPose),
        V(sh_getLocalPos), V(sh_getLocalOri), V(sh_setGlobalPose), V(sh_setGlobalPos), V(sh_setGlobalOri),
        V(sh_getGlobalPose), V(sh_getGlobalPos), V(sh_getGlobalOri), V(sh_setMaterial), V(sh_getMaterial),
        V(sh_setSkin), V(sh_getSkin), V(sh_getType), V(sh_setCCD), V(sh_getCCD), V(sh_setName), V(sh_getName),
        0 /* raycast */, V(sh_ovlSphere), V(sh_ovlOBB), V(sh_ovlAABB), V(sh_ovlCapsule), V(sh_setGroupsMask),
        V(sh_getGroupsMask) };
    static const char *names[8] = { "shape.plane", "shape.sphere", "shape.box", "shape.capsule",
                                    "shape.wheel", "shape.convex", "shape.mesh", "shape.heightfield" };
    static const int sizes[8] = { 35 + 5, 35 + 5, 35 + 5, 35 + 8, 35 + 1, 35 + 4, 35 + 9, 35 + 1 };
    for (int k = 0; k < 8; k++) {
        vt_shape[k] = B2Stubs(names[k], sizes[k]);
        for (int i = 1; i < 35; i++) if (base[i]) vt_shape[k][i] = base[i];
    }
    /* plane (2.6 order: setPlane, saveToDesc, getPlane) */
    vt_shape[NX_SHAPE_PLANE][36] = V(any_saveToDesc);
    vt_shape[NX_SHAPE_SPHERE][35] = V(sph_setRadius); vt_shape[NX_SHAPE_SPHERE][36] = V(sph_getRadius);
    vt_shape[NX_SHAPE_SPHERE][37] = V(sph_getWorld);  vt_shape[NX_SHAPE_SPHERE][38] = V(any_saveToDesc);
    vt_shape[NX_SHAPE_BOX][35] = V(box_setDim); vt_shape[NX_SHAPE_BOX][36] = V(box_getDim);
    vt_shape[NX_SHAPE_BOX][37] = V(box_getOBB); vt_shape[NX_SHAPE_BOX][38] = V(any_saveToDesc);
    vt_shape[NX_SHAPE_CAPSULE][35] = V(cap_setDims); vt_shape[NX_SHAPE_CAPSULE][36] = V(cap_setRadius);
    vt_shape[NX_SHAPE_CAPSULE][37] = V(cap_getRadius); vt_shape[NX_SHAPE_CAPSULE][38] = V(cap_setHeight);
    vt_shape[NX_SHAPE_CAPSULE][39] = V(cap_getHeight); vt_shape[NX_SHAPE_CAPSULE][40] = V(cap_getWorld);
    vt_shape[NX_SHAPE_CAPSULE][41] = V(any_saveToDesc);
    vt_shape[NX_SHAPE_CONVEX][35] = V(any_saveToDesc);
    vt_shape[NX_SHAPE_CONVEX][36] = V(cvx_getMesh); vt_shape[NX_SHAPE_CONVEX][37] = V(cvx_getMesh);
    vt_shape[NX_SHAPE_MESH][35] = V(any_saveToDesc);
    vt_shape[NX_SHAPE_MESH][36] = V(trm_getMesh); vt_shape[NX_SHAPE_MESH][37] = V(trm_getMesh);

    /* actor: 2.5 = 2.6 for slots 0..81; 2.6 82/83 (sleep energy) do not exist in 2.5 */
    vt_actor = B2Stubs("actor", 91);
    void *ac[91] = { 0, V(ac_getScene), V(ac_saveToDesc), V(ac_setName), V(ac_getName), V(ac_setGlobalPose),
        V(ac_setGlobalPos), V(ac_setGlobalOri), V(ac_setGlobalOriQ), V(ac_getGlobalPose), V(ac_getGlobalPos),
        V(ac_getGlobalOri), V(ac_getGlobalOriQ), V(ac_moveGlobalPose), V(ac_moveGlobalPos), V(ac_moveGlobalOri),
        V(ac_moveGlobalOriQ), V(ac_createShape), V(ac_releaseShape), V(ac_getNbShapes), V(ac_getShapes),
        V(ac_setGroup), V(ac_getGroup), V(ac_raiseActorFlag), V(ac_clearActorFlag), V(ac_readActorFlag),
        V(ac_isDynamic), V(ac_setCMOLP), V(ac_setCMOLPos), V(ac_setCMOLOri), V(ac_setCMOGP), V(ac_setCMOGPos),
        V(ac_setCMOGOri), V(ac_setCMGP), V(ac_setCMGPos), V(ac_setCMGOri), V(ac_getCMLP), V(ac_getCMLPos),
        V(ac_getCMLOri), V(ac_getCMGP), V(ac_getCMGPos), V(ac_getCMGOri), V(ac_setMass), V(ac_getMass),
        V(ac_setMSIT), V(ac_getMSIT), V(ac_getGIT), V(ac_getGITI), V(ac_updateMass), V(ac_setLinDamp),
        V(ac_getLinDamp), V(ac_setAngDamp), V(ac_getAngDamp), V(ac_setLinVel), V(ac_setAngVel), V(ac_getLinVel),
        V(ac_getAngVel), V(ac_setMaxAngVel), V(ac_getMaxAngVel), V(ac_setCCD), V(ac_getCCD), V(ac_setLinMom),
        V(ac_setAngMom), V(ac_getLinMom), V(ac_getAngMom), V(ac_addForceAtPos), V(ac_addForceAtLocalPos),
        V(ac_addLocalForceAtPos), V(ac_addLocalForceAtLocalPos), V(ac_addForce), V(ac_addLocalForce),
        V(ac_addTorque), V(ac_addLocalTorque), V(ac_kinetic), V(ac_getPointVel), V(ac_getLocalPointVel),
        V(ac_isGroupSleeping), V(ac_isSleeping), V(ac_getSleepLin), V(ac_setSleepLin), V(ac_getSleepAng),
        V(ac_setSleepAng), /* 82 */ V(ac_wakeUp), V(ac_putToSleep), V(ac_raiseBodyFlag), V(ac_clearBodyFlag),
        V(ac_readBodyFlag), V(ac_saveBodyToDesc), V(ac_setSolverIter), V(ac_getSolverIter), 0 /* linearSweep */ };
    for (int i = 1; i < 91; i++) if (ac[i]) vt_actor[i] = ac[i];

    /* D6 joint: NxJoint 20 slots + NxD6Joint 6 + 1 trailing */
    vt_joint_d6 = B2Stubs("joint.d6", 27);
    void *jt[26] = { 0, V(jt_getActors), V(jt_setGlobalAnchor), V(jt_setGlobalAxis), V(jt_getGlobalAnchor),
        V(jt_getGlobalAxis), V(jt_getState), V(jt_setBreakable), V(jt_getBreakable), V(jt_setLimitPoint),
        V(jt_getLimitPoint), V(jt_addLimitPlane), V(jt_purgeLimitPlanes), V(jt_resetLimitPlaneIt),
        V(jt_hasMoreLimitPlanes), V(jt_getNextLimitPlane), V(jt_getType), V(jt_setName), V(jt_getName),
        V(jt_getScene), V(d6_load), V(d6_save), V(d6_setDrivePos), V(d6_setDriveOri), V(d6_setDriveLinVel),
        V(d6_setDriveAngVel) };
    for (int i = 1; i < 26; i++) vt_joint_d6[i] = jt[i];

    /* meshes */
    vt_trimesh = B2Stubs("triangleMesh", 17);
    vt_trimesh[1] = V(tm_getSubmeshCount); vt_trimesh[2] = V(tm_getCount); vt_trimesh[3] = V(tm_getFormat);
    vt_trimesh[4] = V(tm_getBase); vt_trimesh[5] = V(tm_getStride); vt_trimesh[15] = V(tm_getRefCount);
    vt_convex = B2Stubs("convexMesh", 10);
    vt_convex[1] = V(cm_getSubmeshCount); vt_convex[2] = V(cm_getCount); vt_convex[3] = V(cm_getFormat);
    vt_convex[4] = V(cm_getBase); vt_convex[5] = V(cm_getStride); vt_convex[7] = V(cm_getRefCount);
    vt_clothmesh = B2Stubs("clothMesh", 4);
    vt_clothmesh[1] = V(clm_saveToDesc); vt_clothmesh[2] = V(clm_getRefCount);

    /* cloth: 2.6 order up to slot 28 (verified: attachVertexToGlobalPosition = 27) */
    vt_cloth = B2Stubs("cloth", 72);
    void *cl[29] = { 0, V(cl_saveToDesc), V(cl_getClothMesh), V(cl_setBend), V(cl_getBend), V(cl_setStretch),
        V(cl_getStretch), V(cl_setDamp), V(cl_getDamp), V(cl_setFriction), V(cl_getFriction), V(cl_setPressure),
        V(cl_getPressure), V(cl_setTear), V(cl_getTear), V(cl_setAttTear), V(cl_getAttTear), V(cl_setThickness),
        V(cl_getThickness), V(cl_getDensity), V(cl_getSolverIter), V(cl_setSolverIter), V(cl_getWorldBounds),
        V(cl_attachToShape), V(cl_attachToColliding), V(cl_detachFromShape), V(cl_attachVertexToShape),
        V(cl_attachVertexToPos), V(cl_freeVertex) };
    for (int i = 1; i < 29; i++) vt_cloth[i] = cl[i];
    /* 2.5 has one method fewer than 2.6 between 28 and 36 (getMeshData is 36
       in 2.5, 37 in 2.6) - most likely attachToCore. So 2.5 = 2.6 - 1 here: */
    vt_cloth[31] = V(cl_setGroup);      vt_cloth[32] = V(cl_getGroup);
    vt_cloth[33] = V(cl_setGroupsMask); vt_cloth[34] = V(cl_getGroupsMask);
    vt_cloth[35] = V(cl_setMeshData);   vt_cloth[36] = V(cl_getMeshData);
    /* no set/getValidBounds in 2.5 (no validBounds in its desc): 2.5 = 2.6 - 3.
       Verified: getNumberOfParticles = 41 (debug overlay, Switchball.exe 0x42D59F) */
    vt_cloth[37] = V(cl_setPositions);  vt_cloth[38] = V(cl_getPositions);
    vt_cloth[39] = V(cl_setVelocities); vt_cloth[40] = V(cl_getVelocities);
    vt_cloth[41] = V(cl_getNbParticles);
    /* verified: isSleeping = 51 (0x42D564) -> 2.5 = 2.6 - 6 from here on */
    vt_cloth[51] = V(cl_isSleeping);    vt_cloth[52] = V(cl_getSleepLin);
    vt_cloth[53] = V(cl_setSleepLin);   vt_cloth[54] = V(cl_wakeUp);
    vt_cloth[55] = V(cl_putToSleep);    vt_cloth[56] = V(cl_setFlags);
    vt_cloth[57] = V(cl_getFlags);

    B2InitVtables_Scene();
    B2InitFluidVtables();
}
