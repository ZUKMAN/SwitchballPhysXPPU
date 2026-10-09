/*
 * SwitchballPhysXPPU - scene, SDK, cooking, utility library, contact reports and the
 * exported PhysXLoader functions. PhysX 2.8.4 is loaded directly from
 * <game>\PhysX284\PhysXCore.dll (no 2.8.4 PhysXLoader, no registry).
 */
#include "b2_common.h"
#include <map>
#include <string>

NxPhysicsSDK       *g_sdk;
NxCookingInterface *g_cook;
NxUtilLib          *g_util;
WSdk                g_wsdk;
int                 g_clothTwoWay = 1;
int                 g_hwMode = 1;
int                 g_jointKeepSleep = 1, g_logJoints = 0, g_trackActors = 0, g_jointProjection = -1;
static int          g_trackMoving = 0;          /* diagnostics: every moving body, every frame */
static float        g_trackBox[6]; static int g_trackBoxOn;
float               g_frictionScale = 1.0f; int g_coneFriction = 0;
float               g_staticFrictionScale = 1.0f;   /* extra factor for static friction only */
static float        g_skinWidth = -1.0f;            /* -1 = as the game sets it (0.001) */
static int          g_bodySolverIter = 0;           /* 0 = as the game sets it */
static float        g_adaptiveForce = -1.0f, g_jointExtrapolation = 1.0f, g_ropeLoadMassScale = 1.0f;
static float        g_ropeSettleSeconds = 10.0f, g_settleAdaptive = 0.0f, g_defaultAdaptive = 1.0f;
static double       g_settleUntil;            /* GetTickCount() time until which the settle mode runs */
static bool         g_settling;
static float        g_clothResponseScale = 0.5f, g_clothIterScale = 3.0f, g_clothStretchMin = 0.0f;

void *NewMeshWrapper(void *real, int kind);
void  DeleteMeshWrapper(void *real);

/* ------------------------------------------------------------------------ */
/* Log                                                                       */
/* ------------------------------------------------------------------------ */

static char g_dir[MAX_PATH];
static FILE *g_log;
static CRITICAL_SECTION g_cs;
static std::map<std::string, int> *g_once;

void B2Log(const char *fmt, ...)
{
    if (!g_log) return;
    EnterCriticalSection(&g_cs);
    va_list ap; va_start(ap, fmt);
    fprintf(g_log, "[%10lu] ", (unsigned long)GetTickCount());
    vfprintf(g_log, fmt, ap); fputc('\n', g_log); fflush(g_log);
    va_end(ap);
    LeaveCriticalSection(&g_cs);
}

void B2LogOnce(const char *key, const char *fmt, ...)
{
    EnterCriticalSection(&g_cs);
    if (!g_once) g_once = new std::map<std::string, int>;
    bool first = ((*g_once)[key]++ == 0);
    LeaveCriticalSection(&g_cs);
    if (!first || !g_log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt); _vsnprintf(buf, sizeof(buf) - 1, fmt, ap); va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    B2Log("%s", buf);
}

class B2Output : public NxUserOutputStream {
public:
    std::map<std::string, unsigned> seen;
    void reportError(NxErrorCode c, const char *m, const char *f, int l)
    {
        /* first 3 of each message, then every 10000th - 2.8 can repeat one message a million times */
        EnterCriticalSection(&g_cs);
        unsigned n = ++seen[std::string(m ? m : "") + (f ? f : "")];
        LeaveCriticalSection(&g_cs);
        if (n <= 3 || n % 10000 == 0)
            B2Log("PhysX 2.8.4 error %d: %s (%s:%d)%s", (int)c, m ? m : "", f ? f : "", l,
                  n > 3 ? "  (repeated)" : "");
    }
    NxAssertResponse reportAssertViolation(const char *m, const char *f, int l)
    { B2Log("PhysX 2.8.4 assert: %s (%s:%d)", m ? m : "", f ? f : "", l); return NX_AR_CONTINUE; }
    void print(const char *m) { B2Log("PhysX 2.8.4: %s", m ? m : ""); }
} g_output;

/* ------------------------------------------------------------------------ */
/* Contact reports: 2.8.4 pair -> 2.5 pair, shape pointers in the stream      */
/* replaced by our wrappers                                                  */
/* ------------------------------------------------------------------------ */

void ContactAdapter::onContactNotify(NxContactPair &p, NxU32 events)
{
    ContactPair25 q;
    /* 2.8 also reports pairs whose actor was just released (puzzle reset);
       2.5 did not - the game would dereference a dead actor */
    if (p.isDeletedActor[0] || p.isDeletedActor[1]) return;
    q.actors[0] = WA(p.actors[0]);
    q.actors[1] = WA(p.actors[1]);
    if (!q.actors[0] || !q.actors[1]) return;
    q.sumNormalForce = p.sumNormalForce;
    q.sumFrictionForce = p.sumFrictionForce;
    q.stream = 0;
    if (p.stream) {
        /* walk the stream (format of NxContactStreamIterator, identical in
           2.5/2.6/2.8) to find its length and the shape pointer words */
        const NxU32 *s = p.stream;
        NxU32 nPairs = *s++, len;
        const NxU32 *e = s;
        for (NxU32 i = 0; i < nPairs; i++) {
            e += 2;
            NxU32 tw = *e++, nPatches = tw & 0xFFFF, flags = tw >> 16;
            for (NxU32 k = 0; k < nPatches; k++) {
                e += 3;
                NxU32 nPoints = *e++;
                for (NxU32 m = 0; m < nPoints; m++) {
                    e += 3;
                    NxU32 bin = *e++;
                    if (flags & NX_SF_POINT_CONTACT_FORCE) e++;
                    if (flags & NX_SF_HAS_FEATURES_PER_POINT) e += (bin & 0x80000000) ? 2 : 1;
                }
            }
        }
        len = (NxU32)(e - p.stream);
        buf.assign(p.stream, p.stream + len);
        NxU32 *o = &buf[1];
        for (NxU32 i = 0; i < nPairs; i++) {
            NxU32 tw = o[2], flags = tw >> 16;
            o[0] = (NxU32)(size_t)((flags & NX_SF_DELETED_SHAPE_0) ? 0 : WS((NxShape *)(size_t)o[0]));
            o[1] = (NxU32)(size_t)((flags & NX_SF_DELETED_SHAPE_1) ? 0 : WS((NxShape *)(size_t)o[1]));
            o[2] = tw & ~((NxU32)(NX_SF_DELETED_SHAPE_0 | NX_SF_DELETED_SHAPE_1) << 16);
            NxU32 nPatches = tw & 0xFFFF;
            o += 3;
            for (NxU32 k = 0; k < nPatches; k++) {
                o += 3;
                NxU32 nPoints = *o++;
                for (NxU32 m = 0; m < nPoints; m++) {
                    o += 3;
                    NxU32 bin = *o++;
                    if (flags & NX_SF_POINT_CONTACT_FORCE) o++;
                    if (flags & NX_SF_HAS_FEATURES_PER_POINT) o += (bin & 0x80000000) ? 2 : 1;
                }
            }
        }
        q.stream = &buf[0];
    }
    typedef void (FC *PFN)(void *self, EDX, ContactPair25 *pair, NxU32 events);
    ((PFN)(*(void ***)game)[0])(game, 0, &q, events);
}

/* ------------------------------------------------------------------------ */
/* Scene                                                                     */
/* ------------------------------------------------------------------------ */

#define SC(t) (((WScene *)(t))->s)

static NxU32 FC sc_getSimType(void *t, EDX)                    { return ((WScene *)t)->hw ? NX_SIMULATION_HW : NX_SIMULATION_SW; }
static void  FC sc_setGravity(void *t, EDX, const NxVec3 *g)   { SC(t)->setGravity(*g); }
static void  FC sc_getGravity(void *t, EDX, NxVec3 *g)         { SC(t)->getGravity(*g); }

static void EnsurePMap(NxTriangleMesh *m)
{
    if (!m || m->hasPMap() || !g_cook) return;
    NxPMap pm;
    if (g_cook->NxCreatePMap(pm, *m, 64)) {
        m->loadPMap(pm);
        g_cook->NxReleasePMap(pm);
        B2Log("PMap created for dynamic triangle mesh %p", m);
    } else B2Log("PMap creation FAILED for mesh %p", m);
}

static void *FC sc_createActor(void *t, EDX, const ActorDesc25 *d)
{
    WScene *self = (WScene *)t;
    std::vector<void *> owned;
    NxActorDesc ad;
    NxBodyDesc bd;
    ad.globalPose = d->globalPose;
    ad.density = d->density;
    ad.flags = d->flags;
    ad.group = d->group;
    ad.name = d->name;
    ad.userData = NULL;
    if (d->body) { Body25To28(d->body, bd); ad.body = &bd; }
    if (d->shapesBegin)
        for (ShapeDesc25 **p = d->shapesBegin; p < d->shapesEnd; p++) {
            NxShapeDesc *sd = Shape25To28(*p, owned);
            if (!sd) continue;
            /* 2.5 dynamic triangle meshes (moving platforms, the waterwheel) collide
               with other meshes only through a PMap - 2.8 too ("Trying to collide two
               pmap-less, nonconvex, nonheightfield meshes") */
            if (d->body && !(d->body->flags & NX_BF_KINEMATIC) && sd->getType() == NX_SHAPE_MESH)
                EnsurePMap(((NxTriangleMeshShapeDesc *)sd)->meshData);
            /* 2.5: a cloth with NX_CLF_COLLISION_TWOWAY pushed every body; 2.8 also
               needs NX_SF_CLOTH_TWOWAY on the body's shapes (all of them alike) */
            if (d->body && g_clothTwoWay) sd->shapeFlags |= NX_SF_CLOTH_TWOWAY;
            /* 2.5: NX_FF_COLLISION_TWOWAY on the fluid was enough; 2.8 also wants it on the shapes */
            if (d->body && g_hwMode) sd->shapeFlags |= NX_SF_FLUID_TWOWAY;
            ad.shapes.pushBack(sd);
        }
    if (g_coneFriction) ad.flags |= NX_AF_FORCE_CONE_FRICTION;
    /* more solver iterations for moving bodies: calmer contacts for multi-shape boxes */
    if (d->body && g_bodySolverIter > 0 && bd.solverIterationCount < (NxU32)g_bodySolverIter)
        bd.solverIterationCount = (NxU32)g_bodySolverIter;
    NxActor *a = self->s->createActor(ad);
    if (!a) {
        B2Log("scene.createActor FAILED: %u shapes, body %p, density %f, desc valid %d",
              (unsigned)ad.shapes.size(), d->body, d->density, ad.isValid());
        if (d->body) B2Log("   body: mass %f inertia %f %f %f, flags 0x%X, valid %d",
                           bd.mass, bd.massSpaceInertia.x, bd.massSpaceInertia.y, bd.massSpaceInertia.z,
                           bd.flags, bd.isValid());
        for (NxU32 i = 0; i < ad.shapes.size(); i++)
            B2Log("   shape %u: type %d valid %d", i, (int)ad.shapes[i]->getType(), ad.shapes[i]->isValid());
    }
    for (size_t i = 0; i < owned.size(); i++) delete (NxShapeDesc *)owned[i];
    return a ? NewActorWrapper(a, self, d) : 0;
}

static void FC sc_releaseActor(void *t, EDX, WActor *a)
{
    if (!a) return;
    SC(t)->releaseActor(*a->a);
    DeleteActorWrapper(a);
}

static void *FC sc_createJoint(void *t, EDX, const D6JointDesc25 *d)
{
    if (d->type != NX_JOINT_D6) {
        B2LogOnce("jointtype", "UNSUPPORTED joint type %u (only D6 is implemented)", d->type);
        return 0;
    }
    NxD6JointDesc o;
    if (!D625To28(d, o)) B2Log("D6 joint desc not valid for 2.8.4 (creating anyway)");
    if (g_jointProjection >= 0) o.projectionMode = (NxJointProjectionMode)g_jointProjection;
    if (g_jointExtrapolation != 1.0f) o.solverExtrapolationFactor = g_jointExtrapolation;
    if (g_ropeSettleSeconds > 0) {
        /* a heavy compound body (crate) on a light rope link: let the scene settle for a
           while with NX_ADAPTIVE_FORCE off - 2.5 had no adaptive force, and with it 2.8
           drags such crates off ledges (level 2-4). After the window the crates sleep
           and the default is restored (cloth/ball interaction needs it). */
        for (int k = 0; k < 2; k++) {
            NxActor *link = o.actor[k], *load = o.actor[1 - k];
            if (!link || !load || !link->isDynamic() || !load->isDynamic()) continue;
            if (link->getMass() > 0.15f || load->getMass() < 0.5f || load->getNbShapes() < 10) continue;
            g_settleUntil = GetTickCount() + g_ropeSettleSeconds * 1000.0;
            if (!g_settling) {
                g_settling = true;
                g_sdk->setParameter(NX_ADAPTIVE_FORCE, g_settleAdaptive);
                B2Log("rope load %p found: NX_ADAPTIVE_FORCE %.2f for %.0f s", load->userData, g_settleAdaptive, g_ropeSettleSeconds);
            }
        }
    }
    if (g_ropeLoadMassScale != 1.0f) {
        /* a heavy compound body (crate) hanging on a light chain link: scale its mass once */
        static std::vector<NxActor *> done;
        for (int k = 0; k < 2; k++) {
            NxActor *link = o.actor[k], *load = o.actor[1 - k];
            if (!link || !load || !link->isDynamic() || !load->isDynamic()) continue;
            if (link->getMass() > 0.15f || load->getMass() < 0.5f || load->getNbShapes() < 10) continue;
            bool seen = false;
            for (size_t i = 0; i < done.size(); i++) if (done[i] == load) seen = true;
            if (seen) continue;
            done.push_back(load);
            load->setMass(load->getMass() * g_ropeLoadMassScale);
            load->setMassSpaceInertiaTensor(load->getMassSpaceInertiaTensor() * g_ropeLoadMassScale);
            B2Log("rope load %p: mass scaled x%.2f -> %.2f", load->userData, g_ropeLoadMassScale, load->getMass());
        }
    }
    /* initial joint error: distance between the two anchors in world space */
    NxVec3 wa0 = o.localAnchor[0], wa1 = o.localAnchor[1];
    if (o.actor[0]) wa0 = o.actor[0]->getGlobalPose() * o.localAnchor[0];
    if (o.actor[1]) wa1 = o.actor[1]->getGlobalPose() * o.localAnchor[1];
    NxVec3 wx0 = o.actor[0] ? o.actor[0]->getGlobalOrientation() * o.localAxis[0] : o.localAxis[0];
    NxVec3 wx1 = o.actor[1] ? o.actor[1]->getGlobalOrientation() * o.localAxis[1] : o.localAxis[1];
    NxVec3 wn0 = o.actor[0] ? o.actor[0]->getGlobalOrientation() * o.localNormal[0] : o.localNormal[0];
    NxVec3 wn1 = o.actor[1] ? o.actor[1]->getGlobalOrientation() * o.localNormal[1] : o.localNormal[1];
    bool sleep0 = o.actor[0] && o.actor[0]->isDynamic() && o.actor[0]->isSleeping();
    bool sleep1 = o.actor[1] && o.actor[1]->isDynamic() && o.actor[1]->isSleeping();
    NxJoint *j = SC(t)->createJoint(o);
    if (j && g_jointKeepSleep) {
        if (sleep0 && !o.actor[0]->isSleeping()) o.actor[0]->putToSleep();
        if (sleep1 && !o.actor[1]->isSleeping()) o.actor[1]->putToSleep();
    }
    if (g_logJoints) {
        static int n;
        if (n++ < 300) {
            B2Log("joint#%d D6 %p: actors %p(%s%s, m=%.2f) %p(%s%s, m=%.2f) flags 0x%X d6flags 0x%X proj %u/%.3f/%.3f",
                  n, j, d->actor[0], o.actor[0] ? (o.actor[0]->isDynamic() ? "dyn" : "static") : "world",
                  sleep0 ? " asleep" : "", o.actor[0] && o.actor[0]->isDynamic() ? o.actor[0]->getMass() : 0.0f,
                  d->actor[1], o.actor[1] ? (o.actor[1]->isDynamic() ? "dyn" : "static") : "world",
                  sleep1 ? " asleep" : "", o.actor[1] && o.actor[1]->isDynamic() ? o.actor[1]->getMass() : 0.0f,
                  d->jointFlags, d->flags, d->projectionMode, d->projectionDistance, d->projectionAngle);
            B2Log("   motions x%u y%u z%u s1%u s2%u tw%u  linLimit %.3f swing %.3f/%.3f twist %.3f..%.3f  maxF %g maxT %g",
                  d->xMotion, d->yMotion, d->zMotion, d->swing1Motion, d->swing2Motion, d->twistMotion,
                  d->linearLimit.value, d->swing1Limit.value, d->swing2Limit.value,
                  d->twistLimit.low.value, d->twistLimit.high.value, d->maxForce, d->maxTorque);
            B2Log("   world anchor0 %.3f %.3f %.3f anchor1 %.3f %.3f %.3f  ERROR %.3f  axis dot %.3f normal dot %.3f",
                  wa0.x, wa0.y, wa0.z, wa1.x, wa1.y, wa1.z, (wa0 - wa1).magnitude(), wx0.dot(wx1), wn0.dot(wn1));
            B2Log("   anchor0 %.3f %.3f %.3f axis0 %.3f %.3f %.3f normal0 %.3f %.3f %.3f",
                  d->localAnchor[0].x, d->localAnchor[0].y, d->localAnchor[0].z,
                  d->localAxis[0].x, d->localAxis[0].y, d->localAxis[0].z,
                  d->localNormal[0].x, d->localNormal[0].y, d->localNormal[0].z);
        }
    }
    if (!j) { B2Log("scene.createJoint(D6) FAILED"); return 0; }
    WJoint *w = new WJoint;
    w->vtbl = vt_joint_d6; w->userData = d->userData; w->appData = 0; w->j = j; w->scene = (WScene *)t;
    j->userData = w;
    return w;
}

static void FC sc_releaseJoint(void *t, EDX, WJoint *j)
{
    if (!j) return;
    SC(t)->releaseJoint(*j->j);
    delete j;
}

static void *FC sc_createMaterial(void *t, EDX, const MaterialDesc25 *d)
{
    NxMaterialDesc o; Material25To28(d, o);
    o.staticFriction *= g_frictionScale * g_staticFrictionScale; o.dynamicFriction *= g_frictionScale;
    o.staticFrictionV *= g_frictionScale * g_staticFrictionScale; o.dynamicFrictionV *= g_frictionScale;
    NxMaterial *m = SC(t)->createMaterial(o);
    B2Log("createMaterial -> index %u: static %.3f dynamic %.3f restitution %.3f (V %.3f/%.3f) flags 0x%X combine friction %u restitution %u%s",
          m ? m->getMaterialIndex() : 0xFFFF, d->staticFriction, d->dynamicFriction, d->restitution,
          d->staticFrictionV, d->dynamicFrictionV, d->flags, d->frictionCombineMode, d->restitutionCombineMode,
          (g_frictionScale != 1.0f || g_staticFrictionScale != 1.0f) ? "  (friction scaled)" : "");
    return MaterialWrapper((WScene *)t, m);
}

static void FC sc_releaseMaterial(void *t, EDX, WMaterial *m)
{
    if (!m) return;
    SC(t)->releaseMaterial(*m->m);
    delete m;
}

static void  FC sc_setActorPairFlags(void *t, EDX, void *a, void *b, NxU32 f)
{ SC(t)->setActorPairFlags(*RA(a), *RA(b), f); }
static NxU32 FC sc_getActorPairFlags(void *t, EDX, void *a, void *b)
{ return SC(t)->getActorPairFlags(*RA(a), *RA(b)); }
static void  FC sc_setShapePairFlags(void *t, EDX, void *a, void *b, NxU32 f)
{ SC(t)->setShapePairFlags(*RS(a), *RS(b), f); }
static NxU32 FC sc_getShapePairFlags(void *t, EDX, void *a, void *b)
{ return SC(t)->getShapePairFlags(*RS(a), *RS(b)); }
static void  FC sc_setGroupCollisionFlag(void *t, EDX, NxU32 g1, NxU32 g2, NxU32 en)
{ SC(t)->setGroupCollisionFlag((NxCollisionGroup)g1, (NxCollisionGroup)g2, (en & 0xFF) != 0); }
static NxU32 FC sc_getGroupCollisionFlag(void *t, EDX, NxU32 g1, NxU32 g2)
{ return SC(t)->getGroupCollisionFlag((NxCollisionGroup)g1, (NxCollisionGroup)g2); }
static void  FC sc_setActorGroupPairFlags(void *t, EDX, NxU32 g1, NxU32 g2, NxU32 f)
{ SC(t)->setActorGroupPairFlags((NxActorGroup)g1, (NxActorGroup)g2, f); }
static NxU32 FC sc_getActorGroupPairFlags(void *t, EDX, NxU32 g1, NxU32 g2)
{ return SC(t)->getActorGroupPairFlags((NxActorGroup)g1, (NxActorGroup)g2); }

static void *FC sc_getMaterialFromIndex(void *t, EDX, NxU32 idx)
{ return MaterialWrapper((WScene *)t, SC(t)->getMaterialFromIndex((NxMaterialIndex)idx)); }
static void  FC sc_flushStream(void *t, EDX)                     { SC(t)->flushStream(); }
static void  FC sc_setTiming(void *t, EDX, NxReal ts, NxU32 it, NxU32 m)
{ SC(t)->setTiming(ts, it, (NxTimeStepMethod)m); }
static const void *FC sc_getDebugRenderable(void *t, EDX)        { return SC(t)->getDebugRenderable(); }
static void *FC sc_getPhysicsSDK(void *t, EDX)                   { (void)t; return &g_wsdk; }
static void *FC sc_createFluid(void *t, EDX, const void *d)
{
    if (!g_hwMode) { B2LogOnce("fluid", "createFluid: HardwareMode=0 - no fluids (NULL)"); return 0; }
    return B2CreateFluid((WScene *)t, (const unsigned char *)d);
}
static void  FC sc_releaseFluid(void *t, EDX, WFluid *f)         { if (g_hwMode) B2ReleaseFluid((WScene *)t, f); }

static void *FC sc_createCloth(void *t, EDX, const ClothDesc25 *d)
{
    NxClothDesc o;
    o.clothMesh = d->clothMesh ? ((WClothMesh *)d->clothMesh)->m : 0;
    o.globalPose = d->globalPose; o.thickness = d->thickness; o.density = d->density;
    o.bendingStiffness = d->bendingStiffness; o.stretchingStiffness = d->stretchingStiffness;
    o.dampingCoefficient = d->dampingCoefficient; o.friction = d->friction; o.pressure = d->pressure;
    o.tearFactor = d->tearFactor; o.collisionResponseCoefficient = d->collisionResponseCoefficient;
    o.attachmentResponseCoefficient = d->attachmentResponseCoefficient;
    o.attachmentTearFactor = d->attachmentTearFactor; o.solverIterations = d->solverIterations;
    o.externalAcceleration = d->externalAcceleration; o.wakeUpCounter = d->wakeUpCounter;
    o.sleepLinearVelocity = d->sleepLinearVelocity; MeshData25To28(&d->meshData, o.meshData);
    o.collisionGroup = d->collisionGroup; o.groupsMask = d->groupsMask;
    o.flags = d->flags & ~(NxU32)NX_CLF_HARDWARE; o.userData = NULL; o.name = d->name;
    /* tuning of the cloth -> body push (2.8 two-way is stronger than 2.5's) */
    o.collisionResponseCoefficient *= g_clothResponseScale;
    if (o.stretchingStiffness < g_clothStretchMin) o.stretchingStiffness = g_clothStretchMin;
    if (g_clothIterScale != 1.0f) {
        NxU32 it = (NxU32)(o.solverIterations * g_clothIterScale + 0.5f);
        o.solverIterations = it < 1 ? 1 : it;
    }
    B2Log("createCloth: flags 0x%X (two-way %s), thickness %.3f, collisionResponse %.2f -> %.2f, attachResponse %.2f, iterations %u -> %u, mesh %p",
          d->flags, (d->flags & NX_CLF_COLLISION_TWOWAY) ? "yes" : "no", d->thickness,
          d->collisionResponseCoefficient, o.collisionResponseCoefficient, d->attachmentResponseCoefficient,
          d->solverIterations, o.solverIterations, o.clothMesh);
    B2Log("   density %.3f stretching %.3f -> %.3f bending %.3f damping %.3f friction %.3f tearFactor %.2f",
          d->density, d->stretchingStiffness, o.stretchingStiffness, d->bendingStiffness,
          d->dampingCoefficient, d->friction, d->tearFactor);
    if (!o.isValid()) B2Log("cloth desc not valid for 2.8.4 (flags 0x%X, mesh %p)", o.flags, o.clothMesh);
    NxCloth *c = SC(t)->createCloth(o);
    if (!c) { B2Log("scene.createCloth FAILED"); return 0; }
    WCloth *w = new WCloth;
    w->vtbl = vt_cloth; w->userData = d->userData; w->c = c; w->scene = (WScene *)t; w->md = d->meshData;
    c->userData = w;
    return w;
}
static void FC sc_releaseCloth(void *t, EDX, WCloth *c) { if (c) { SC(t)->releaseCloth(*c->c); delete c; } }

static void  FC sc_simulate(void *t, EDX, NxReal dt)             { SC(t)->simulate(dt); }
static NxU32 FC sc_checkResults(void *t, EDX, NxU32 st, NxU32 block)
{ return SC(t)->checkResults((NxSimulationStatus)st, (block & 0xFF) != 0); }
/* diagnostics: trajectory of compound bodies (>= 10 shapes, e.g. the 2-4 crates)
   during the first 40 s of a scene - same format as the 2.5 inventory build */
static bool InTrackBox(const NxVec3 &p)
{
    return p.x >= g_trackBox[0] && p.y >= g_trackBox[1] && p.z >= g_trackBox[2] &&
           p.x <= g_trackBox[3] && p.y <= g_trackBox[4] && p.z <= g_trackBox[5];
}

static void TrackActors(WScene *w)
{
    static NxScene *cur; static unsigned frame;
    if (cur != w->s) { cur = w->s; frame = 0; }
    frame++;
    /* with a TrackBox: every 5th frame for 2 s, then every 30th up to 10 s;
       otherwise compound bodies every 30th frame for 40 s */
    if (g_trackBoxOn) { if (frame > 600 || (frame > 120 ? frame % 30 : frame % 5)) return; }
    else if (frame > 2400 || frame % 30) return;
    NxU32 n = w->s->getNbActors();
    NxActor **as = w->s->getActors();
    for (NxU32 i = 0; i < n; i++) {
        NxActor *a = as[i];
        if (!a->isDynamic()) continue;
        NxVec3 p = a->getGlobalPosition();
        if (g_trackBoxOn ? !InTrackBox(p) : a->getNbShapes() < 10) continue;
        NxVec3 v = a->getLinearVelocity(), av = a->getAngularVelocity();
        NxU32 f = 0;
        for (int b = 0; b < 12; b++) if (a->readBodyFlag((NxBodyFlag)(1u << b))) f |= 1u << b;
        if (frame == 5 || frame == 30) {
            NxShape *s0 = a->getShapes()[0];
            NxMaterial *mt = w->s->getMaterialFromIndex(s0->getMaterial());
            B2Log("   material of %p: index %u static %.3f dynamic %.3f restitution %.3f, skin %.4f",
                  a->userData, s0->getMaterial(), mt->getStaticFriction(), mt->getDynamicFriction(),
                  mt->getRestitution(), s0->getSkinWidth());
        }
        B2Log("track f%u actor %p (%u shapes, m %.3f): pos %.3f %.3f %.3f vel %.3f %.3f %.3f angvel %.3f %.3f %.3f %s bodyflags 0x%X",
              frame, a->userData, a->getNbShapes(), a->getMass(), p.x, p.y, p.z, v.x, v.y, v.z, av.x, av.y, av.z,
              a->isSleeping() ? "asleep" : "awake", f);
    }
}

/* diagnostics (TrackMoving=1): every frame, every moving non-kinematic body (and every
   sleep/wake change) - to see what a pushed box does: stick-slip, rocking, sleeping */
static void TrackMoving(WScene *w)
{
    static NxScene *cur; static unsigned frame, lines;
    static std::map<NxActor *, int> seen;          /* actor -> last asleep state */
    if (cur != w->s) { cur = w->s; frame = 0; seen.clear(); }
    frame++;
    if (lines > 60000) return;
    NxU32 n = w->s->getNbActors();
    NxActor **as = w->s->getActors();
    for (NxU32 i = 0; i < n; i++) {
        NxActor *a = as[i];
        if (!a->isDynamic() || a->readBodyFlag(NX_BF_KINEMATIC)) continue;
        int asleep = a->isSleeping() ? 1 : 0;
        NxVec3 v = a->getLinearVelocity(), av = a->getAngularVelocity();
        std::map<NxActor *, int>::iterator it = seen.find(a);
        bool changed = it != seen.end() && it->second != asleep;
        if (it == seen.end()) {
            seen[a] = asleep;
            NxShape *s0 = a->getShapes()[0];
            NxMaterial *mt = w->s->getMaterialFromIndex(s0->getMaterial());
            B2Log("body %p: %u shapes (first type %d), mass %.3f, linDamp %.3f angDamp %.3f, sleepLin %.3f sleepAng %.3f, "
                  "iters %u, material %u (static %.2f dynamic %.2f rest %.2f), skin %.4f",
                  a->userData, a->getNbShapes(), (int)s0->getType(), a->getMass(), a->getLinearDamping(),
                  a->getAngularDamping(), a->getSleepLinearVelocity(), a->getSleepAngularVelocity(),
                  a->getSolverIterationCount(), s0->getMaterial(), mt->getStaticFriction(),
                  mt->getDynamicFriction(), mt->getRestitution(), s0->getSkinWidth());
        } else it->second = asleep;
        if (!changed && (asleep || v.magnitudeSquared() < 0.0025f)) continue;   /* < 5 cm/s */
        NxVec3 p = a->getGlobalPosition();
        B2Log("mv f%u %p (%u sh) pos %.3f %.3f %.3f vel %.3f %.3f %.3f |%.3f| angvel %.2f %.2f %.2f%s",
              frame, a->userData, a->getNbShapes(), p.x, p.y, p.z, v.x, v.y, v.z, v.magnitude(),
              av.x, av.y, av.z, changed ? (asleep ? "  -> ASLEEP" : "  -> AWAKE") : "");
        lines++;
    }
}

static NxU32 FC sc_fetchResults(void *t, EDX, NxU32 st, NxU32 block, NxU32 *err)
{
    NxU32 r = SC(t)->fetchResults((NxSimulationStatus)st, (block & 0xFF) != 0, err);
    if (g_settling && GetTickCount() > g_settleUntil) {
        g_settling = false;
        g_sdk->setParameter(NX_ADAPTIVE_FORCE, g_defaultAdaptive);
        B2Log("rope settle window over: NX_ADAPTIVE_FORCE back to %.2f", g_defaultAdaptive);
    }
    if (g_trackActors && (r & 0xFF)) TrackActors((WScene *)t);
    if (g_trackMoving && (r & 0xFF)) TrackMoving((WScene *)t);
    return r;
}
static void  FC sc_flushCaches(void *t, EDX)                     { SC(t)->flushCaches(); }
static void  FC sc_shutdownWorkers(void *t, EDX)                 { SC(t)->shutdownWorkerThreads(); }

/* ------------------------------------------------------------------------ */
/* SDK                                                                       */
/* ------------------------------------------------------------------------ */

static void  FC sdk_release(void *t, EDX)                        { (void)t; }
static NxU32 FC sdk_setParameter(void *t, EDX, NxU32 p, NxReal v)
{
    bool ok; NxParameter q = Param25To28(p, &ok); (void)t;
    if (!ok) { B2LogOnce("param", "setParameter(%u) not mapped - ignored", p); return 1; }
    /* Switchball sets NX_SKIN_WIDTH 0.001; 2.8's contact generation can flicker with such
       a thin skin (resting/pushed boxes stutter). SkinWidth overrides it. */
    if (q == NX_SKIN_WIDTH && g_skinWidth >= 0) {
        B2LogOnce("skin", "setParameter(NX_SKIN_WIDTH %.4f) -> %.4f (ini SkinWidth)", v, g_skinWidth);
        v = g_skinWidth;
    }
    return g_sdk->setParameter(q, v);
}
static float FC sdk_getParameter(void *t, EDX, NxU32 p)
{ bool ok; NxParameter q = Param25To28(p, &ok); (void)t; return ok ? g_sdk->getParameter(q) : 0.0f; }

static void *FC sdk_createScene(void *t, EDX, const SceneDesc25 *d)
{
    (void)t;
    int hw = 0;
    if (d->simType != NX_SIMULATION_SW) {
        if (!g_hwMode) { B2Log("createScene: hardware scene requested - refused (HardwareMode=0)"); return 0; }
        hw = 1;            /* emulated PPU: run it in software, report it as hardware */
    }
    NxSceneDesc o;
    o.gravity = d->gravity;
    o.maxTimestep = d->maxTimestep; o.maxIter = d->maxIter;
    o.timeStepMethod = (NxTimeStepMethod)d->timeStepMethod;
    o.simType = NX_SIMULATION_SW;
    o.groundPlane = d->groundPlane != 0; o.boundsPlanes = d->boundsPlanes != 0;
    o.flags = d->flags;
    if (d->userNotify)        B2Log("createScene: user notify object given - NOT supported yet");
    if (d->userContactModify) B2Log("createScene: contact modify object given - NOT supported yet");
    if (d->userTriggerReport) B2Log("createScene: trigger report object given - NOT supported yet");
    ContactAdapter *ca = d->userContactReport ? new ContactAdapter(d->userContactReport) : 0;
    o.userContactReport = ca;
    NxScene *s = g_sdk->createScene(o);
    B2Log("createScene(gravity %.2f %.2f %.2f, flags 0x%X, timestep %f/%u/%u, contact report %p) -> %p",
          d->gravity.x, d->gravity.y, d->gravity.z, d->flags, d->maxTimestep, d->maxIter, d->timeStepMethod,
          d->userContactReport, s);
    if (!s) { delete ca; return 0; }
    WScene *w = new WScene;
    w->vtbl = vt_scene; w->userData = 0; w->s = s; w->contact = ca; w->materials = 0; w->hw = hw;
    if (hw) B2Log("   hardware scene emulated in software");
    s->userData = w;
    return w;
}

static void FC sdk_releaseScene(void *t, EDX, WScene *s)
{
    (void)t;
    if (!s) return;
    /* free the wrappers of everything that is still in the scene */
    NxU32 n = s->s->getNbActors();
    NxActor **as = s->s->getActors();
    std::vector<WActor *> wa;
    for (NxU32 i = 0; i < n; i++) if (WA(as[i])) wa.push_back(WA(as[i]));
    g_sdk->releaseScene(*s->s);
    for (size_t i = 0; i < wa.size(); i++) DeleteActorWrapper(wa[i]);
    delete s->contact;
    delete s;
}
static NxU32 FC sdk_getNbScenes(void *t, EDX)                    { (void)t; return g_sdk->getNbScenes(); }
static void *FC sdk_getScene(void *t, EDX, NxU32 i)
{ (void)t; NxScene *s = g_sdk->getScene(i); return s ? s->userData : 0; }
static void *FC sdk_createTriangleMesh(void *t, EDX, const NxStream *st)
{ (void)t; return NewMeshWrapper(g_sdk->createTriangleMesh(*st), 0); }
static void  FC sdk_releaseTriangleMesh(void *t, EDX, WTriMesh *m)
{ (void)t; if (m) { NxTriangleMesh *r = m->m; DeleteMeshWrapper(r); g_sdk->releaseTriangleMesh(*r); } }
static void *FC sdk_createConvexMesh(void *t, EDX, const NxStream *st)
{ (void)t; return NewMeshWrapper(g_sdk->createConvexMesh(*st), 1); }
static void  FC sdk_releaseConvexMesh(void *t, EDX, WConvex *m)
{ (void)t; if (m) { NxConvexMesh *r = m->m; DeleteMeshWrapper(r); g_sdk->releaseConvexMesh(*r); } }
static void *FC sdk_createClothMesh(void *t, EDX, NxStream *st)
{ (void)t; return NewMeshWrapper(g_sdk->createClothMesh(*st), 2); }
static void  FC sdk_releaseClothMesh(void *t, EDX, WClothMesh *m)
{ (void)t; if (m) { NxClothMesh *r = m->m; DeleteMeshWrapper(r); g_sdk->releaseClothMesh(*r); } }
static NxU32 FC sdk_getInternalVersion(void *t, EDX, NxU32 *a, NxU32 *d, NxU32 *b)
{ (void)t; return g_sdk->getInternalVersion(*a, *d, *b); }
static NxU32 FC sdk_getHWVersion(void *t, EDX)                   { (void)t; return g_hwMode ? NX_HW_VERSION_ATHENA_1_0 : NX_HW_VERSION_NONE; }
static NxU32 FC sdk_getNbPPUs(void *t, EDX)                      { (void)t; return g_hwMode ? 1 : 0; }

/* ------------------------------------------------------------------------ */
/* Cooking                                                                   */
/* ------------------------------------------------------------------------ */

static NxU32 FC ck_init(void *t, EDX, void *alloc, void *out)
{ (void)t; (void)alloc; (void)out; return g_cook->NxInitCooking(NULL, &g_output); }
static void  FC ck_close(void *t, EDX)                           { (void)t; g_cook->NxCloseCooking(); }
static NxU32 FC ck_cookTri(void *t, EDX, const TriangleMeshDesc25 *d, NxStream *st)
{
    (void)t;
    NxTriangleMeshDesc o;
    o.numVertices = d->numVertices; o.numTriangles = d->numTriangles;
    o.pointStrideBytes = d->pointStrideBytes; o.triangleStrideBytes = d->triangleStrideBytes;
    o.points = d->points; o.triangles = d->triangles;
    o.flags = d->flags & ~(NxU32)NX_MF_HARDWARE_MESH;
    o.materialIndexStride = d->materialIndexStride; o.materialIndices = d->materialIndices;
    o.heightFieldVerticalAxis = (NxHeightFieldAxis)d->heightFieldVerticalAxis;
    o.heightFieldVerticalExtent = d->heightFieldVerticalExtent;
    o.pmap = NULL; o.convexEdgeThreshold = d->convexEdgeThreshold;
    bool ok = g_cook->NxCookTriangleMesh(o, *st);
    if (!ok) B2Log("cookTriangleMesh FAILED (%u verts, %u tris, flags 0x%X)", d->numVertices, d->numTriangles, d->flags);
    return ok;
}
static NxU32 FC ck_cookConvex(void *t, EDX, const ConvexMeshDesc25 *d, NxStream *st)
{
    (void)t;
    NxConvexMeshDesc o;
    o.numVertices = d->numVertices; o.numTriangles = d->numTriangles;
    o.pointStrideBytes = d->pointStrideBytes; o.triangleStrideBytes = d->triangleStrideBytes;
    o.points = d->points; o.triangles = d->triangles; o.flags = d->flags;
    bool ok = g_cook->NxCookConvexMesh(o, *st);
    if (!ok) B2Log("cookConvexMesh FAILED (%u verts, flags 0x%X)", d->numVertices, d->flags);
    return ok;
}
static NxU32 FC ck_cookCloth(void *t, EDX, const ClothMeshDesc25 *d, NxStream *st)
{
    (void)t;
    NxClothMeshDesc o;
    o.numVertices = d->numVertices; o.numTriangles = d->numTriangles;
    o.pointStrideBytes = d->pointStrideBytes; o.triangleStrideBytes = d->triangleStrideBytes;
    o.points = d->points; o.triangles = d->triangles; o.flags = d->flags & ~(NxU32)NX_MF_HARDWARE_MESH;
    o.vertexMassStrideBytes = d->vertexMassStrideBytes; o.vertexFlagStrideBytes = d->vertexFlagStrideBytes;
    o.vertexMasses = d->vertexMasses; o.vertexFlags = d->vertexFlags;
    bool ok = g_cook->NxCookClothMesh(o, *st);
    if (!ok) B2Log("cookClothMesh FAILED (%u verts, %u tris, flags 0x%X)", d->numVertices, d->numTriangles, d->flags);
    return ok;
}

/* ------------------------------------------------------------------------ */
/* Utility library: forwarded slot by slot (2.6 and 2.8 orders are equal up  */
/* to slot 65); the joint descriptor helpers work on the 2.5 descriptor      */
/* ------------------------------------------------------------------------ */

/* run PhysX 2.8.4's own NxJointDesc_SetGlobalAnchor/Axis on a converted
   descriptor and copy the local frames back into the game's 2.5 descriptor */
static void ut_setGlobal(D6JointDesc25 *d, const NxVec3 *ws, bool axis)
{
    NxD6JointDesc o;
    for (int i = 0; i < 2; i++) {
        o.actor[i] = RA(d->actor[i]);
        o.localAnchor[i] = d->localAnchor[i]; o.localAxis[i] = d->localAxis[i]; o.localNormal[i] = d->localNormal[i];
    }
    if (axis) g_util->NxJointDesc_SetGlobalAxis(o, *ws);
    else      g_util->NxJointDesc_SetGlobalAnchor(o, *ws);
    for (int i = 0; i < 2; i++) {
        d->localAnchor[i] = o.localAnchor[i]; d->localAxis[i] = o.localAxis[i]; d->localNormal[i] = o.localNormal[i];
    }
}
static void FC ut_setGlobalAnchor(void *t, EDX, D6JointDesc25 *d, const NxVec3 *ws) { (void)t; ut_setGlobal(d, ws, false); }
static void FC ut_setGlobalAxis(void *t, EDX, D6JointDesc25 *d, const NxVec3 *ws)   { (void)t; ut_setGlobal(d, ws, true); }

static void **MakeForwarders(void *realObj, int n)
{
    /* mov ecx, realObj ; mov eax, [ecx] ; jmp [eax + i*4] */
    void **vt = B2Stubs("utilLib", 75);
    unsigned char *code = (unsigned char *)VirtualAlloc(NULL, n * 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    for (int i = 0; i < n; i++) {
        unsigned char *c = code + i * 16;
        c[0] = 0xB9; *(void **)(c + 1) = realObj;
        c[5] = 0x8B; c[6] = 0x01;
        c[7] = 0xFF; c[8] = 0xA0; *(NxU32 *)(c + 9) = i * 4;
        vt[i] = c;
    }
    FlushInstructionCache(GetCurrentProcess(), code, n * 16);
    return vt;
}

/* ------------------------------------------------------------------------ */
/* Scene / SDK / cooking vtables                                             */
/* ------------------------------------------------------------------------ */

#define V(f) ((void *)(f))

void B2InitVtables_Scene()
{
    /* scene (105 slots in 2.5): 0..13 as 2.6; 14..23 = 2.6 17..26 (no compartments);
       45..50 = 2.6 54..59; 80/81 fluid; 88/89 cloth; 93..96 = 2.6 104..107; 101 = 2.6 112 */
    vt_scene = B2Stubs("scene", 105);
    vt_scene[2]  = V(sc_getSimType);       vt_scene[4]  = V(sc_setGravity);
    vt_scene[5]  = V(sc_getGravity);       vt_scene[6]  = V(sc_createActor);
    vt_scene[7]  = V(sc_releaseActor);     vt_scene[8]  = V(sc_createJoint);
    vt_scene[9]  = V(sc_releaseJoint);     vt_scene[12] = V(sc_createMaterial);
    vt_scene[13] = V(sc_releaseMaterial);
    vt_scene[14] = V(sc_setActorPairFlags);     vt_scene[15] = V(sc_getActorPairFlags);
    vt_scene[16] = V(sc_setShapePairFlags);     vt_scene[17] = V(sc_getShapePairFlags);
    vt_scene[20] = V(sc_setGroupCollisionFlag); vt_scene[21] = V(sc_getGroupCollisionFlag);
    vt_scene[22] = V(sc_setActorGroupPairFlags);vt_scene[23] = V(sc_getActorGroupPairFlags);
    vt_scene[45] = V(sc_getMaterialFromIndex);  vt_scene[46] = V(sc_flushStream);
    vt_scene[47] = V(sc_setTiming);             vt_scene[49] = V(sc_getDebugRenderable);
    vt_scene[50] = V(sc_getPhysicsSDK);
    vt_scene[80] = V(sc_createFluid);           vt_scene[81] = V(sc_releaseFluid);
    vt_scene[88] = V(sc_createCloth);           vt_scene[89] = V(sc_releaseCloth);
    vt_scene[93] = V(sc_simulate);              vt_scene[94] = V(sc_checkResults);
    vt_scene[95] = V(sc_fetchResults);          vt_scene[96] = V(sc_flushCaches);
    vt_scene[101] = V(sc_shutdownWorkers);

    /* SDK (25 slots, 2.6 order) */
    vt_sdk = B2Stubs("sdk", 25);
    vt_sdk[1]  = V(sdk_release);          vt_sdk[2]  = V(sdk_setParameter);
    vt_sdk[3]  = V(sdk_getParameter);     vt_sdk[4]  = V(sdk_createScene);
    vt_sdk[5]  = V(sdk_releaseScene);     vt_sdk[6]  = V(sdk_getNbScenes);
    vt_sdk[7]  = V(sdk_getScene);         vt_sdk[8]  = V(sdk_createTriangleMesh);
    vt_sdk[9]  = V(sdk_releaseTriangleMesh); vt_sdk[15] = V(sdk_createConvexMesh);
    vt_sdk[16] = V(sdk_releaseConvexMesh);   vt_sdk[17] = V(sdk_createClothMesh);
    vt_sdk[18] = V(sdk_releaseClothMesh);    vt_sdk[21] = V(sdk_getInternalVersion);
    vt_sdk[23] = V(sdk_getHWVersion);        vt_sdk[24] = V(sdk_getNbPPUs);

    /* cooking (15 slots in 2.5; 3..7 as in 2.6) */
    vt_cooking = B2Stubs("cooking", 15);
    vt_cooking[3] = V(ck_init);      vt_cooking[4] = V(ck_close);
    vt_cooking[5] = V(ck_cookTri);   vt_cooking[6] = V(ck_cookConvex);
    vt_cooking[7] = V(ck_cookCloth);
}

/* ------------------------------------------------------------------------ */
/* Loading PhysX 2.8.4 and the exported loader functions                    */
/* ------------------------------------------------------------------------ */

typedef NxPhysicsSDK *(__cdecl *PFN_NpCreate)(NxU32, NxUserAllocator *, NxUserOutputStream *,
                                              const NxPhysicsSDKDesc &, NxSDKCreateError *);
typedef void (__cdecl *PFN_NpRelease)(NxPhysicsSDK *);
typedef NxUtilLib *(__cdecl *PFN_NpUtil)(void);
typedef NxCookingInterface *(__cdecl *PFN_GetCooking)(NxU32);

static HMODULE g_core, g_cookDll;
static PFN_NpRelease g_npRelease;
static WCooking g_wcook;
static WUtil    g_wutil;
static volatile LONG g_inited;

static void B2Init()
{
    if (g_inited) return;
    EnterCriticalSection(&g_cs);
    if (!g_inited) {
        char path[MAX_PATH], sub[MAX_PATH], ini[MAX_PATH];
        _snprintf(path, sizeof(path), "%sSwitchballPhysXPPU.log", g_dir); path[MAX_PATH - 1] = 0;
        g_log = fopen(path, "w");
        B2Log("==== %s started, dir=%s ====", B2_VERSION, g_dir);
        _snprintf(ini, sizeof(ini), "%sSwitchballPhysXPPU.ini", g_dir); ini[MAX_PATH - 1] = 0;
        GetPrivateProfileStringA("SwitchballPhysXPPU", "CoreFolder", "PhysX284", sub, sizeof(sub), ini);
        g_clothTwoWay    = GetPrivateProfileIntA("SwitchballPhysXPPU", "ClothTwoWay", 1, ini);
        g_hwMode         = GetPrivateProfileIntA("SwitchballPhysXPPU", "HardwareMode", 1, ini);
        B2Log("HardwareMode=%d (%s)", g_hwMode, g_hwMode ? "emulated PPU: hardware levels and fluids on" : "software only");
        g_jointKeepSleep = GetPrivateProfileIntA("SwitchballPhysXPPU", "JointKeepSleep", 1, ini);
        g_logJoints      = GetPrivateProfileIntA("SwitchballPhysXPPU", "LogJoints", 0, ini);
        g_trackActors    = GetPrivateProfileIntA("SwitchballPhysXPPU", "TrackActors", 0, ini);
        g_trackMoving    = GetPrivateProfileIntA("SwitchballPhysXPPU", "TrackMoving", 0, ini);
        {
            char b[16];
            GetPrivateProfileStringA("SwitchballPhysXPPU", "JointProjectionMode", "-1", b, sizeof(b), ini);
            g_jointProjection = atoi(b);
            char tb[128];
            GetPrivateProfileStringA("SwitchballPhysXPPU", "FrictionScale", "1.0", b, sizeof(b), ini);
            g_frictionScale = (float)atof(b);
            g_coneFriction = GetPrivateProfileIntA("SwitchballPhysXPPU", "ConeFriction", 0, ini);
            GetPrivateProfileStringA("SwitchballPhysXPPU", "StaticFrictionScale", "1.0", b, sizeof(b), ini); g_staticFrictionScale = (float)atof(b);
            GetPrivateProfileStringA("SwitchballPhysXPPU", "SkinWidth", "-1", b, sizeof(b), ini);         g_skinWidth = (float)atof(b);
            g_bodySolverIter = GetPrivateProfileIntA("SwitchballPhysXPPU", "BodySolverIterations", 0, ini);
            GetPrivateProfileStringA("SwitchballPhysXPPU", "AdaptiveForce", "-1", b, sizeof(b), ini);      g_adaptiveForce = (float)atof(b);
            GetPrivateProfileStringA("SwitchballPhysXPPU", "JointExtrapolation", "1.0", b, sizeof(b), ini); g_jointExtrapolation = (float)atof(b);
            GetPrivateProfileStringA("SwitchballPhysXPPU", "RopeLoadMassScale", "1.0", b, sizeof(b), ini);  g_ropeLoadMassScale = (float)atof(b);
            GetPrivateProfileStringA("SwitchballPhysXPPU", "RopeSettleSeconds", "10", b, sizeof(b), ini);   g_ropeSettleSeconds = (float)atof(b);
            GetPrivateProfileStringA("SwitchballPhysXPPU", "RopeSettleAdaptiveForce", "0", b, sizeof(b), ini); g_settleAdaptive = (float)atof(b);
            GetPrivateProfileStringA("SwitchballPhysXPPU", "ClothResponseScale", "0.5", b, sizeof(b), ini);  g_clothResponseScale = (float)atof(b);
            GetPrivateProfileStringA("SwitchballPhysXPPU", "ClothIterationScale", "3", b, sizeof(b), ini); g_clothIterScale = (float)atof(b);
            GetPrivateProfileStringA("SwitchballPhysXPPU", "ClothStretchingMin", "0", b, sizeof(b), ini);    g_clothStretchMin = (float)atof(b);
            GetPrivateProfileStringA("SwitchballPhysXPPU", "TrackBox", "", tb, sizeof(tb), ini);
            g_trackBoxOn = sscanf(tb, "%f %f %f %f %f %f", &g_trackBox[0], &g_trackBox[1], &g_trackBox[2],
                                  &g_trackBox[3], &g_trackBox[4], &g_trackBox[5]) == 6;
        }
        B2InitVtables();
        _snprintf(path, sizeof(path), "%s%s\\PhysXCore.dll", g_dir, sub); path[MAX_PATH - 1] = 0;
        g_core = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        B2Log("load %s -> %p (error %lu)", path, g_core, g_core ? 0 : GetLastError());
        _snprintf(path, sizeof(path), "%s%s\\PhysXCooking.dll", g_dir, sub); path[MAX_PATH - 1] = 0;
        g_cookDll = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        B2Log("load %s -> %p (error %lu)", path, g_cookDll, g_cookDll ? 0 : GetLastError());
        g_inited = 1;
    }
    LeaveCriticalSection(&g_cs);
}

extern "C" void *__cdecl B2_NxCreatePhysicsSDK(NxU32 ver, void *alloc, void *out, const void *desc, NxSDKCreateError *err)
{
    (void)alloc; (void)out; (void)desc;
    B2Init();
    if (g_sdk) { B2Log("NxCreatePhysicsSDK(0x%08X): SDK already exists", ver); return &g_wsdk; }
    PFN_NpCreate create = g_core ? (PFN_NpCreate)GetProcAddress(g_core, "NpCreatePhysicsSDK") : 0;
    g_npRelease = g_core ? (PFN_NpRelease)GetProcAddress(g_core, "NpReleasePhysicsSDK") : 0;
    if (!create) { B2Log("ERROR: PhysXCore.dll 2.8.4 not loaded"); if (err) *err = NXCE_PHYSX_NOT_FOUND; return 0; }
    NxPhysicsSDKDesc d;
    d.flags |= NX_SDKF_NO_HARDWARE;
    NxSDKCreateError e = NXCE_NO_ERROR;
    g_sdk = create(NX_PHYSICS_SDK_VERSION, NULL, &g_output, d, &e);
    B2Log("NxCreatePhysicsSDK(game 0x%08X) -> PhysX 0x%08X: %p (error %d)", ver, NX_PHYSICS_SDK_VERSION, g_sdk, (int)e);
    if (err) *err = e;
    if (!g_sdk) return 0;
    B2Log("2.8.4 defaults: NX_ADAPTIVE_FORCE %.2f, skin width %.4f, bounce threshold %.2f, dyn/sta friction scaling %.2f/%.2f",
          g_sdk->getParameter(NX_ADAPTIVE_FORCE), g_sdk->getParameter(NX_SKIN_WIDTH),
          g_sdk->getParameter(NX_BOUNCE_THRESHOLD), g_sdk->getParameter(NX_DYN_FRICT_SCALING),
          g_sdk->getParameter(NX_STA_FRICT_SCALING));
    if (g_adaptiveForce >= 0) { g_sdk->setParameter(NX_ADAPTIVE_FORCE, g_adaptiveForce); B2Log("NX_ADAPTIVE_FORCE set to %.2f", g_adaptiveForce); }
    g_defaultAdaptive = g_sdk->getParameter(NX_ADAPTIVE_FORCE);
    g_wsdk.vtbl = vt_sdk; g_wsdk.sdk = g_sdk;
    return &g_wsdk;
}

extern "C" void *__cdecl B2_NxCreatePhysicsSDKWithID(NxU32 ver, char *, char *, char *, char *,
                                                     void *alloc, void *out, const void *desc, NxSDKCreateError *err)
{ return B2_NxCreatePhysicsSDK(ver, alloc, out, desc, err); }

extern "C" void __cdecl B2_NxReleasePhysicsSDK(void *sdk)
{
    B2Log("NxReleasePhysicsSDK(%p)", sdk);
    if (sdk == &g_wsdk && g_sdk) {
        if (g_cook) { g_cook->NxCloseCooking(); }
        if (g_npRelease) g_npRelease(g_sdk);
        g_sdk = 0;
    }
}

extern "C" void *__cdecl B2_NxGetCookingLib(NxU32 ver)
{
    B2Init();
    if (!g_cook) {
        PFN_GetCooking gc = g_cookDll ? (PFN_GetCooking)GetProcAddress(g_cookDll, "NxGetCookingInterface") : 0;
        g_cook = gc ? gc(NX_PHYSICS_SDK_VERSION) : 0;
        B2Log("NxGetCookingLib(game 0x%08X) -> 2.8.4 cooking %p", ver, g_cook);
        if (!g_cook) return 0;
        g_wcook.vtbl = vt_cooking; g_wcook.c = g_cook;
    }
    return &g_wcook;
}

extern "C" void *__cdecl B2_NxGetCookingLibWithID(NxU32 ver, char *, char *, char *, char *)
{ return B2_NxGetCookingLib(ver); }

extern "C" void *__cdecl B2_NxGetUtilLib(void)
{
    B2Init();
    if (!g_util) {
        PFN_NpUtil gu = g_core ? (PFN_NpUtil)GetProcAddress(g_core, "NpGetUtilLib") : 0;
        g_util = gu ? gu() : 0;
        B2Log("NxGetUtilLib -> 2.8.4 util %p", g_util);
        if (!g_util) return 0;
        vt_util = MakeForwarders(g_util, 66);
        vt_util[48] = V(ut_setGlobalAnchor);
        vt_util[49] = V(ut_setGlobalAxis);
        g_wutil.vtbl = vt_util; g_wutil.u = g_util;
    }
    return &g_wutil;
}

extern "C" void *__cdecl B2_NxGetPhysicsSDK(void)          { return g_sdk ? &g_wsdk : 0; }
extern "C" void *__cdecl B2_NxGetFoundationSDK(void)
{ typedef void *(__cdecl *F)(void); F f = g_core ? (F)GetProcAddress(g_core, "NpGetFoundationSDK") : 0; return f ? f() : 0; }
extern "C" void *__cdecl B2_NxGetPhysicsSDKAllocator(void)
{ typedef void *(__cdecl *F)(void); F f = g_core ? (F)GetProcAddress(g_core, "NpGetPhysicsSDKAllocator") : 0; return f ? f() : 0; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE pinned;
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&g_cs);
        GetModuleFileNameA(h, g_dir, sizeof(g_dir));
        char *slash = strrchr(g_dir, '\\');
        if (slash) slash[1] = 0; else g_dir[0] = 0;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCSTR)&DllMain, &pinned);
    }
    return TRUE;
}
