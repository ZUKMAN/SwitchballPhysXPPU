/*
 * SwitchballPhysXPPU - "hardware" mode: the game believes a PhysX PPU is installed,
 * its hardware scenes run as 2.8.4 software scenes and its fluids run on the
 * 2.8.4 software fluid solver (software fluids exist since 2.6.2).
 *
 * 2.5 NxFluid slots (recovered for the PhysX 2.6.4 bridge, verified in game):
 *   0 dtor, 1 createEmitter, 2 releaseEmitter, 3 getNbEmitters, 4 getEmitters,
 *   5..8 screen surface meshes, 9 addParticles, 10 setParticlesWriteData,
 *   11 getParticlesWriteData (hidden result), 32 getScene.
 * 2.5 NxFluidEmitter = 2.6 order (setFlag 28 / getFlag 29 verified); in 2.8
 * five methods were inserted before setFlag, so emitters are wrapped too.
 */
#include "b2_common.h"

void **vt_fluid, **vt_emitter;

/* ------------------------------------------------------------------------ */
/* particle data / descriptors                                               */
/* ------------------------------------------------------------------------ */

static void PD25To28(const PD25 *i, NxParticleData &o)
{
    o.setToDefault();
    if (!i) return;
    /* 2.8 has no maxParticles here: buffers must hold NxFluidDesc::maxParticles */
    o.numParticlesPtr = i->numParticlesPtr;
    o.bufferPos = i->bufferPos;             o.bufferVel = i->bufferVel;
    o.bufferLife = i->bufferLife;           o.bufferDensity = i->bufferDensity;
    o.bufferPosByteStride = i->posStride;   o.bufferVelByteStride = i->velStride;
    o.bufferLifeByteStride = i->lifeStride; o.bufferDensityByteStride = i->densityStride;
    o.name = i->name;
}

/* NxFluidEmitterDesc: same layout in 2.5 and 2.6 (0x70 bytes) */
static void EmitterDesc25To28(const unsigned char *d, NxFluidEmitterDesc &o)
{
    memcpy(&o.relPose, d + 0x00, 48);
    o.frameShape             = RS(*(void *const *)(d + 0x30));
    o.type                   = *(const NxU32 *)(d + 0x34);
    o.maxParticles           = *(const NxU32 *)(d + 0x38);
    o.shape                  = *(const NxU32 *)(d + 0x3C);
    o.dimensionX             = *(const float *)(d + 0x40);
    o.dimensionY             = *(const float *)(d + 0x44);
    memcpy(&o.randomPos, d + 0x48, 12);
    o.randomAngle            = *(const float *)(d + 0x54);
    o.fluidVelocityMagnitude = *(const float *)(d + 0x58);
    o.rate                   = *(const float *)(d + 0x5C);
    o.particleLifetime       = *(const float *)(d + 0x60);
    o.flags                  = *(const NxU32 *)(d + 0x64);
    o.userData               = NULL;
    o.name                   = *(const char *const *)(d + 0x6C);
}

static void EmitterDesc28To25(const NxFluidEmitterDesc &o, unsigned char *d, void *userData)
{
    memcpy(d + 0x00, &o.relPose, 48);
    *(void **)(d + 0x30) = o.frameShape ? WS(o.frameShape) : 0;
    *(NxU32 *)(d + 0x34) = o.type;          *(NxU32 *)(d + 0x38) = o.maxParticles;
    *(NxU32 *)(d + 0x3C) = o.shape;         *(float *)(d + 0x40) = o.dimensionX;
    *(float *)(d + 0x44) = o.dimensionY;    memcpy(d + 0x48, &o.randomPos, 12);
    *(float *)(d + 0x54) = o.randomAngle;   *(float *)(d + 0x58) = o.fluidVelocityMagnitude;
    *(float *)(d + 0x5C) = o.rate;          *(float *)(d + 0x60) = o.particleLifetime;
    *(NxU32 *)(d + 0x64) = o.flags;         *(void **)(d + 0x68) = userData;
    *(const char **)(d + 0x6C) = o.name;
}

/* ------------------------------------------------------------------------ */
/* emitter wrapper                                                           */
/* ------------------------------------------------------------------------ */

#define EM(t) (((WEmitter *)(t))->e)
static void  FC em_dtor(void *, EDX, NxU32) {}
static void *FC em_getFluid(void *t, EDX)                          { return ((WEmitter *)t)->fluid; }
static void  FC em_setGlobalPose(void *t, EDX, const NxMat34 *m)   { EM(t)->setGlobalPose(*m); }
static void  FC em_setGlobalPos(void *t, EDX, const NxVec3 *v)     { EM(t)->setGlobalPosition(*v); }
static void  FC em_setGlobalOri(void *t, EDX, const NxMat33 *m)    { EM(t)->setGlobalOrientation(*m); }
static NxMat34 *FC em_getGlobalPose(void *t, EDX, NxMat34 *r)      { *r = EM(t)->getGlobalPoseVal(); return r; }
static NxVec3  *FC em_getGlobalPos(void *t, EDX, NxVec3 *r)        { *r = EM(t)->getGlobalPositionVal(); return r; }
static NxMat33 *FC em_getGlobalOri(void *t, EDX, NxMat33 *r)       { *r = EM(t)->getGlobalOrientationVal(); return r; }
static void  FC em_setLocalPose(void *t, EDX, const NxMat34 *m)    { EM(t)->setLocalPose(*m); }
static void  FC em_setLocalPos(void *t, EDX, const NxVec3 *v)      { EM(t)->setLocalPosition(*v); }
static void  FC em_setLocalOri(void *t, EDX, const NxMat33 *m)     { EM(t)->setLocalOrientation(*m); }
static NxMat34 *FC em_getLocalPose(void *t, EDX, NxMat34 *r)       { *r = EM(t)->getLocalPoseVal(); return r; }
static NxVec3  *FC em_getLocalPos(void *t, EDX, NxVec3 *r)         { *r = EM(t)->getLocalPositionVal(); return r; }
static NxMat33 *FC em_getLocalOri(void *t, EDX, NxMat33 *r)        { *r = EM(t)->getLocalOrientationVal(); return r; }
static void  FC em_setFrameShape(void *t, EDX, void *s)            { EM(t)->setFrameShape(RS(s)); }
static void *FC em_getFrameShape(void *t, EDX)                     { return WS(EM(t)->getFrameShape()); }
static float FC em_getDimX(void *t, EDX)                           { return EM(t)->getDimensionX(); }
static float FC em_getDimY(void *t, EDX)                           { return EM(t)->getDimensionY(); }
static void  FC em_setRandomPos(void *t, EDX, NxVec3 v)            { EM(t)->setRandomPos(v); }
static NxVec3 *FC em_getRandomPos(void *t, EDX, NxVec3 *r)         { *r = EM(t)->getRandomPos(); return r; }
static void  FC em_setRandomAngle(void *t, EDX, NxReal a)          { EM(t)->setRandomAngle(a); }
static float FC em_getRandomAngle(void *t, EDX)                    { return EM(t)->getRandomAngle(); }
static void  FC em_setVel(void *t, EDX, NxReal v)                  { EM(t)->setFluidVelocityMagnitude(v); }
static float FC em_getVel(void *t, EDX)                            { return EM(t)->getFluidVelocityMagnitude(); }
static void  FC em_setRate(void *t, EDX, NxReal v)                 { EM(t)->setRate(v); }
static float FC em_getRate(void *t, EDX)                           { return EM(t)->getRate(); }
static void  FC em_setLife(void *t, EDX, NxReal v)                 { EM(t)->setParticleLifetime(v); }
static float FC em_getLife(void *t, EDX)                           { return EM(t)->getParticleLifetime(); }
static void  FC em_setFlag(void *t, EDX, NxU32 f, NxU32 v)         { EM(t)->setFlag((NxFluidEmitterFlag)f, (v & 0xFF) != 0); }
static NxU32 FC em_getFlag(void *t, EDX, NxU32 f)                  { return EM(t)->getFlag((NxFluidEmitterFlag)f); }
static NxU32 FC em_getShape(void *t, EDX, NxU32 s)                 { return EM(t)->getShape((NxEmitterShape)s); }
static NxU32 FC em_getType(void *t, EDX, NxU32 s)                  { return EM(t)->getType((NxEmitterType)s); }
static NxU32 FC em_load(void *t, EDX, const unsigned char *d)
{ NxFluidEmitterDesc o; EmitterDesc25To28(d, o); return EM(t)->loadFromDesc(o); }
static NxU32 FC em_save(void *t, EDX, unsigned char *d)
{ NxFluidEmitterDesc o; bool ok = EM(t)->saveToDesc(o); EmitterDesc28To25(o, d, ((WEmitter *)t)->userData); return ok; }
static void  FC em_setName(void *t, EDX, const char *n)            { EM(t)->setName(n); }
static const char *FC em_getName(void *t, EDX)                     { return EM(t)->getName(); }

/* ------------------------------------------------------------------------ */
/* fluid wrapper                                                             */
/* ------------------------------------------------------------------------ */

#define FLU(t) (((WFluid *)(t))->f)
static void FC fl_dtor(void *, EDX, NxU32) {}

static void *FC fl_createEmitter(void *t, EDX, const unsigned char *d)
{
    WFluid *self = (WFluid *)t;
    NxFluidEmitterDesc o; EmitterDesc25To28(d, o);
    NxFluidEmitter *e = self->f->createEmitter(o);
    B2Log("fluid %p createEmitter -> %p (type %u shape %u dim %.2fx%.2f vel %.2f rate %.1f life %.2f flags 0x%X)",
          self, e, o.type, o.shape, o.dimensionX, o.dimensionY, o.fluidVelocityMagnitude, o.rate,
          o.particleLifetime, o.flags);
    if (!e) return 0;
    WEmitter *w = new WEmitter;
    w->vtbl = vt_emitter; w->userData = *(void *const *)(d + 0x68); w->e = e; w->fluid = self;
    e->userData = w;
    self->emitters->push_back(w);
    return w;
}
static void FC fl_releaseEmitter(void *t, EDX, WEmitter *e)
{
    WFluid *self = (WFluid *)t;
    if (!e) return;
    for (size_t i = 0; i < self->emitters->size(); i++)
        if ((*self->emitters)[i] == e) { self->emitters->erase(self->emitters->begin() + i); break; }
    self->f->releaseEmitter(*e->e);
    delete e;
}
static NxU32 FC fl_getNbEmitters(void *t, EDX)          { return (NxU32)((WFluid *)t)->emitters->size(); }
static void *FC fl_getEmitters(void *t, EDX)
{ WFluid *self = (WFluid *)t; return self->emitters->empty() ? 0 : &(*self->emitters)[0]; }
static void *FC fl_createScreenMesh(void *t, EDX, const void *d)
{ (void)t; (void)d; B2LogOnce("screenmesh", "fluid.createScreenSurfaceMesh: not supported - NULL"); return 0; }
static void  FC fl_releaseScreenMesh(void *, EDX, void *) {}
static NxU32 FC fl_getNbScreenMeshes(void *, EDX)       { return 0; }
static void *FC fl_getScreenMeshes(void *, EDX)         { return 0; }
static void  FC fl_addParticles(void *t, EDX, const PD25 *d)
{ NxParticleData o; PD25To28(d, o); FLU(t)->addParticles(o); }
static void  FC fl_setParticlesWriteData(void *t, EDX, const PD25 *d)
{ NxParticleData o; PD25To28(d, o); ((WFluid *)t)->wd = *d; FLU(t)->setParticlesWriteData(o); }
static PD25 *FC fl_getParticlesWriteData(void *t, EDX, PD25 *r)  { *r = ((WFluid *)t)->wd; return r; }
static void *FC fl_getScene(void *t, EDX)                        { return ((WFluid *)t)->scene; }

/* ------------------------------------------------------------------------ */
/* scene entry points (called from b2_scene.cpp)                             */
/* ------------------------------------------------------------------------ */

void *B2CreateFluid(WScene *sc, const unsigned char *d)
{
    NxFluidDesc fd;                                         /* 2.8.4 defaults */
    fd.maxParticles                    = *(const NxU32 *)(d + 0x2C);
    fd.restParticlesPerMeter           = *(const float *)(d + 0x30);
    fd.restDensity                     = *(const float *)(d + 0x34);
    fd.kernelRadiusMultiplier          = *(const float *)(d + 0x38);
    fd.motionLimitMultiplier           = *(const float *)(d + 0x3C);
    fd.packetSizeMultiplier            = *(const NxU32 *)(d + 0x40);
    fd.stiffness                       = *(const float *)(d + 0x44);
    fd.viscosity                       = *(const float *)(d + 0x48);
    fd.damping                         = *(const float *)(d + 0x4C);
    memcpy(&fd.externalAcceleration, d + 0x50, 12);
    /* 2.5 static/dynamic "collision restitution / adhesion" = 2.8 restitution /
       dynamic friction for static / dynamic shapes */
    fd.restitutionForStaticShapes      = *(const float *)(d + 0x5C);
    fd.dynamicFrictionForStaticShapes  = *(const float *)(d + 0x60);
    fd.restitutionForDynamicShapes     = *(const float *)(d + 0x64);
    fd.dynamicFrictionForDynamicShapes = *(const float *)(d + 0x68);
    fd.collisionResponseCoefficient    = *(const float *)(d + 0x6C);
    fd.simulationMethod                = *(const NxU32 *)(d + 0x70);
    fd.collisionMethod                 = *(const NxU32 *)(d + 0x74);
    fd.collisionGroup                  = *(const NxU16 *)(d + 0x78);
    memcpy(&fd.groupsMask, d + 0x7C, 16);
    PD25To28((const PD25 *)(d + 0x00), fd.initialParticleData);
    PD25To28((const PD25 *)(d + 0x8C), fd.particlesWriteData);
    fd.flags                           = *(const NxU32 *)(d + 0xB8) & ~(NxU32)NX_FF_HARDWARE;
    fd.userData                        = NULL;
    {   /* the game's write buffer size limits the fluid (2.8 writes up to maxParticles) */
        NxU32 wmax = ((const PD25 *)(d + 0x8C))->maxParticles;
        if (wmax && fd.maxParticles > wmax) fd.maxParticles = wmax;
    }
    if (fd.motionLimitMultiplier > fd.packetSizeMultiplier * fd.kernelRadiusMultiplier)
        fd.motionLimitMultiplier = fd.packetSizeMultiplier * fd.kernelRadiusMultiplier;
    if (fd.initialParticleData.numParticlesPtr && *fd.initialParticleData.numParticlesPtr == 0)
        fd.initialParticleData.setToDefault();
    if (!fd.isValid()) B2Log("fluid desc NOT valid for 2.8.4 (creating anyway)");

    NxFluid *f = sc->s->createFluid(fd);
    B2Log("createFluid -> %p: max %u rppm %.1f kernel %.2f motion %.2f packet %u stiff %.1f visc %.1f "
          "method %u coll %u flags 0x%X write(max %u pos %p stride %u)",
          f, fd.maxParticles, fd.restParticlesPerMeter, fd.kernelRadiusMultiplier, fd.motionLimitMultiplier,
          fd.packetSizeMultiplier, fd.stiffness, fd.viscosity, fd.simulationMethod, fd.collisionMethod,
          fd.flags, ((const PD25 *)(d + 0x8C))->maxParticles, fd.particlesWriteData.bufferPos,
          fd.particlesWriteData.bufferPosByteStride);
    if (!f) return 0;
    WFluid *w = new WFluid;
    w->vtbl = vt_fluid; w->userData = *(void *const *)(d + 0xBC); w->f = f; w->scene = sc;
    w->wd = *(const PD25 *)(d + 0x8C);
    w->emitters = new std::vector<WEmitter *>;
    f->userData = w;
    return w;
}

void B2ReleaseFluid(WScene *sc, WFluid *w)
{
    if (!w) return;
    sc->s->releaseFluid(*w->f);
    for (size_t i = 0; i < w->emitters->size(); i++) delete (*w->emitters)[i];
    delete w->emitters;
    delete w;
}

#define V(f) ((void *)(f))

void B2InitFluidVtables()
{
    vt_fluid = B2Stubs("fluid", 64);
    vt_fluid[0] = V(fl_dtor);              vt_fluid[1] = V(fl_createEmitter);
    vt_fluid[2] = V(fl_releaseEmitter);    vt_fluid[3] = V(fl_getNbEmitters);
    vt_fluid[4] = V(fl_getEmitters);       vt_fluid[5] = V(fl_createScreenMesh);
    vt_fluid[6] = V(fl_releaseScreenMesh); vt_fluid[7] = V(fl_getNbScreenMeshes);
    vt_fluid[8] = V(fl_getScreenMeshes);   vt_fluid[9] = V(fl_addParticles);
    vt_fluid[10] = V(fl_setParticlesWriteData); vt_fluid[11] = V(fl_getParticlesWriteData);
    vt_fluid[32] = V(fl_getScene);

    vt_emitter = B2Stubs("fluidEmitter", 40);
    void *em[36] = { V(em_dtor), V(em_getFluid), V(em_setGlobalPose), V(em_setGlobalPos), V(em_setGlobalOri),
        V(em_getGlobalPose), V(em_getGlobalPos), V(em_getGlobalOri), V(em_setLocalPose), V(em_setLocalPos),
        V(em_setLocalOri), V(em_getLocalPose), V(em_getLocalPos), V(em_getLocalOri), V(em_setFrameShape),
        V(em_getFrameShape), V(em_getDimX), V(em_getDimY), V(em_setRandomPos), V(em_getRandomPos),
        V(em_setRandomAngle), V(em_getRandomAngle), V(em_setVel), V(em_getVel), V(em_setRate), V(em_getRate),
        V(em_setLife), V(em_getLife), V(em_setFlag), V(em_getFlag), V(em_getShape), V(em_getType),
        V(em_load), V(em_save), V(em_setName), V(em_getName) };
    for (int i = 0; i < 36; i++) vt_emitter[i] = em[i];
}
