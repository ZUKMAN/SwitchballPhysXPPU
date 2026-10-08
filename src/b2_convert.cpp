/*
 * SwitchballPhysXPPU - descriptor conversion between the 2.5 layouts used by the game
 * and the 2.8.4 SDK classes (filled field by field, by name).
 */
#include "b2_common.h"

/* ---- shapes ---------------------------------------------------------------- */

template <class T> static T *Own(std::vector<void *> &owned, T *p) { owned.push_back(p); return p; }

static void ShapeBase25To28(const ShapeDesc25 *d, NxShapeDesc &o)
{
    o.localPose     = d->localPose;
    o.shapeFlags    = d->shapeFlags;
    o.group         = d->group;
    o.materialIndex = d->materialIndex;
    o.ccdSkeleton   = NULL;                 /* not used by the game */
    o.density       = d->density;
    o.mass          = d->mass;
    o.skinWidth     = d->skinWidth;
    o.userData      = NULL;                 /* set to our wrapper after creation */
    o.name          = d->name;
    o.groupsMask    = d->groupsMask;
}

/* returns a new 2.8 descriptor (freed by the caller through 'owned') */
NxShapeDesc *Shape25To28(const ShapeDesc25 *d, std::vector<void *> &owned)
{
    NxShapeDesc *o = 0;
    switch (d->type) {
    case NX_SHAPE_PLANE: {
        NxPlaneShapeDesc *p = Own(owned, new NxPlaneShapeDesc);
        p->normal = NxVec3(d->u.plane.normal); p->d = d->u.plane.d; o = p; break; }
    case NX_SHAPE_SPHERE: {
        NxSphereShapeDesc *p = Own(owned, new NxSphereShapeDesc);
        p->radius = d->u.sphere.radius; o = p; break; }
    case NX_SHAPE_BOX: {
        NxBoxShapeDesc *p = Own(owned, new NxBoxShapeDesc);
        p->dimensions = NxVec3(d->u.box.dimensions); o = p; break; }
    case NX_SHAPE_CAPSULE: {
        NxCapsuleShapeDesc *p = Own(owned, new NxCapsuleShapeDesc);
        p->radius = d->u.capsule.radius; p->height = d->u.capsule.height;
        p->flags = d->u.capsule.flags; o = p; break; }
    case NX_SHAPE_CONVEX: {
        NxConvexShapeDesc *p = Own(owned, new NxConvexShapeDesc);
        p->meshData  = d->u.convex.meshData ? ((WConvex *)d->u.convex.meshData)->m : 0;
        p->meshFlags = d->u.convex.meshFlags; o = p; break; }
    case NX_SHAPE_MESH: {
        NxTriangleMeshShapeDesc *p = Own(owned, new NxTriangleMeshShapeDesc);
        p->meshData  = d->u.mesh.meshData ? ((WTriMesh *)d->u.mesh.meshData)->m : 0;
        p->meshFlags = d->u.mesh.meshFlags;
        p->meshPagingMode = (NxMeshPagingMode)d->u.mesh.meshPagingMode;
        o = p; break; }
    default:
        B2Log("UNSUPPORTED shape type %u in a descriptor - skipped", d->type);
        return 0;
    }
    ShapeBase25To28(d, *o);
    return o;
}

/* 2.8 shape -> 2.5 descriptor of the game (its vptr stays untouched) */
void Shape28To25(NxShape *s, ShapeDesc25 *out)
{
    WShape *w = WS(s);
    out->type          = s->getType();
    out->localPose     = s->getLocalPose();
    out->group         = s->getGroup();
    out->materialIndex = s->getMaterial();
    out->ccdSkeleton   = NULL;
    out->skinWidth     = s->getSkinWidth();
    out->userData      = w ? w->userData : 0;
    out->name          = s->getName();
    out->groupsMask    = s->getGroupsMask();
    {
        NxU32 f = 0;
        for (int b = 0; b < 32; b++)
            if (s->getFlag((NxShapeFlag)(1u << b))) f |= (1u << b);
        out->shapeFlags = f;
    }
    switch (s->getType()) {
    case NX_SHAPE_PLANE: {
        NxPlaneShapeDesc d; s->isPlane()->saveToDesc(d);
        d.normal.get(out->u.plane.normal); out->u.plane.d = d.d;
        out->density = d.density; out->mass = d.mass; break; }
    case NX_SHAPE_SPHERE: {
        NxSphereShapeDesc d; s->isSphere()->saveToDesc(d);
        out->u.sphere.radius = d.radius; out->density = d.density; out->mass = d.mass; break; }
    case NX_SHAPE_BOX: {
        NxBoxShapeDesc d; s->isBox()->saveToDesc(d);
        d.dimensions.get(out->u.box.dimensions); out->density = d.density; out->mass = d.mass; break; }
    case NX_SHAPE_CAPSULE: {
        NxCapsuleShapeDesc d; s->isCapsule()->saveToDesc(d);
        out->u.capsule.radius = d.radius; out->u.capsule.height = d.height;
        out->u.capsule.flags = d.flags; out->density = d.density; out->mass = d.mass; break; }
    case NX_SHAPE_CONVEX: {
        NxConvexShapeDesc d; s->isConvexMesh()->saveToDesc(d);
        out->u.convex.meshData = ConvexWrapperOf(d.meshData);
        out->u.convex.meshFlags = d.meshFlags; out->density = d.density; out->mass = d.mass; break; }
    case NX_SHAPE_MESH: {
        NxTriangleMeshShapeDesc d; s->isTriangleMesh()->saveToDesc(d);
        out->u.mesh.meshData = TriMeshWrapperOf(d.meshData);
        out->u.mesh.meshFlags = d.meshFlags; out->u.mesh.meshPagingMode = d.meshPagingMode;
        out->density = d.density; out->mass = d.mass; break; }
    default: break;
    }
}

/* ---- body / material ------------------------------------------------------- */

void Body25To28(const BodyDesc25 *i, NxBodyDesc &o)
{
    o.massLocalPose        = i->massLocalPose;
    o.massSpaceInertia     = i->massSpaceInertia;
    o.mass                 = i->mass;
    o.linearVelocity       = i->linearVelocity;
    o.angularVelocity      = i->angularVelocity;
    o.wakeUpCounter        = i->wakeUpCounter;
    o.linearDamping        = i->linearDamping;
    o.angularDamping       = i->angularDamping;
    o.maxAngularVelocity   = i->maxAngularVelocity;
    o.CCDMotionThreshold   = i->CCDMotionThreshold;
    o.flags                = i->flags & ~(NxU32)(1 << 9);   /* 2.6 NX_BF_POSE_SLEEP_TEST: gone in 2.8 */
    o.sleepLinearVelocity  = i->sleepLinearVelocity;
    o.sleepAngularVelocity = i->sleepAngularVelocity;
    o.solverIterationCount = i->solverIterationCount;
    o.sleepEnergyThreshold = i->sleepEnergyThreshold;
    o.sleepDamping         = i->sleepDamping;
}

void Body28To25(const NxBodyDesc &i, BodyDesc25 *o)
{
    o->massLocalPose = i.massLocalPose; o->massSpaceInertia = i.massSpaceInertia; o->mass = i.mass;
    o->linearVelocity = i.linearVelocity; o->angularVelocity = i.angularVelocity;
    o->wakeUpCounter = i.wakeUpCounter; o->linearDamping = i.linearDamping;
    o->angularDamping = i.angularDamping; o->maxAngularVelocity = i.maxAngularVelocity;
    o->CCDMotionThreshold = i.CCDMotionThreshold; o->flags = i.flags;
    o->sleepLinearVelocity = i.sleepLinearVelocity; o->sleepAngularVelocity = i.sleepAngularVelocity;
    o->solverIterationCount = i.solverIterationCount; o->sleepEnergyThreshold = i.sleepEnergyThreshold;
    o->sleepDamping = i.sleepDamping;
}

void Material25To28(const MaterialDesc25 *i, NxMaterialDesc &o)
{
    o.dynamicFriction = i->dynamicFriction; o.staticFriction = i->staticFriction;
    o.restitution = i->restitution; o.dynamicFrictionV = i->dynamicFrictionV;
    o.staticFrictionV = i->staticFrictionV; o.dirOfAnisotropy = i->dirOfAnisotropy;
    o.flags = i->flags;
    o.frictionCombineMode = (NxCombineMode)i->frictionCombineMode;
    o.restitutionCombineMode = (NxCombineMode)i->restitutionCombineMode;
    if (i->spring) B2LogOnce("matspring", "material desc with a spring - ignored (removed in 2.8)");
}

void Material28To25(const NxMaterialDesc &i, MaterialDesc25 *o)
{
    o->dynamicFriction = i.dynamicFriction; o->staticFriction = i.staticFriction;
    o->restitution = i.restitution; o->dynamicFrictionV = i.dynamicFrictionV;
    o->staticFrictionV = i.staticFrictionV; o->dirOfAnisotropy = i.dirOfAnisotropy;
    o->flags = i.flags; o->frictionCombineMode = i.frictionCombineMode;
    o->restitutionCombineMode = i.restitutionCombineMode; o->spring = 0;
}

/* ---- D6 joint ------------------------------------------------------------------ */

static void Soft25To28(const LimitSoft25 &i, NxJointLimitSoftDesc &o)
{ o.value = i.value; o.restitution = i.restitution; o.spring = i.spring; o.damping = i.damping; }
static void Soft28To25(const NxJointLimitSoftDesc &i, LimitSoft25 &o)
{ o.value = i.value; o.restitution = i.restitution; o.spring = i.spring; o.damping = i.damping; }
static void Drive25To28(const Drive25 &i, NxJointDriveDesc &o)
{ o.driveType = i.driveType; o.spring = i.spring; o.damping = i.damping; o.forceLimit = i.forceLimit; }
static void Drive28To25(NxJointDriveDesc i, Drive25 &o)   /* NxBitField has a non-const conversion */
{ o.driveType = (NxU32)i.driveType; o.spring = i.spring; o.damping = i.damping; o.forceLimit = i.forceLimit; }

bool D625To28(const D6JointDesc25 *i, NxD6JointDesc &o)
{
    for (int k = 0; k < 2; k++) {
        o.actor[k]       = RA(i->actor[k]);
        o.localNormal[k] = i->localNormal[k];
        o.localAxis[k]   = i->localAxis[k];
        o.localAnchor[k] = i->localAnchor[k];
    }
    o.maxForce = i->maxForce; o.maxTorque = i->maxTorque;
    o.userData = NULL; o.name = i->name; o.jointFlags = i->jointFlags;
    o.xMotion = (NxD6JointMotion)i->xMotion; o.yMotion = (NxD6JointMotion)i->yMotion;
    o.zMotion = (NxD6JointMotion)i->zMotion; o.swing1Motion = (NxD6JointMotion)i->swing1Motion;
    o.swing2Motion = (NxD6JointMotion)i->swing2Motion; o.twistMotion = (NxD6JointMotion)i->twistMotion;
    Soft25To28(i->linearLimit, o.linearLimit); Soft25To28(i->swing1Limit, o.swing1Limit);
    Soft25To28(i->swing2Limit, o.swing2Limit);
    Soft25To28(i->twistLimit.low, o.twistLimit.low); Soft25To28(i->twistLimit.high, o.twistLimit.high);
    Drive25To28(i->xDrive, o.xDrive); Drive25To28(i->yDrive, o.yDrive); Drive25To28(i->zDrive, o.zDrive);
    Drive25To28(i->swingDrive, o.swingDrive); Drive25To28(i->twistDrive, o.twistDrive);
    Drive25To28(i->slerpDrive, o.slerpDrive);
    o.drivePosition = i->drivePosition; o.driveOrientation = i->driveOrientation;
    o.driveLinearVelocity = i->driveLinearVelocity; o.driveAngularVelocity = i->driveAngularVelocity;
    o.projectionMode = (NxJointProjectionMode)i->projectionMode;
    o.projectionDistance = i->projectionDistance; o.projectionAngle = i->projectionAngle;
    o.gearRatio = i->gearRatio; o.flags = i->flags;
    return o.isValid();
}

void D628To25(const NxD6JointDesc &i, D6JointDesc25 *o, NxJoint *j)
{
    for (int k = 0; k < 2; k++) {
        o->actor[k] = WA(i.actor[k]);
        o->localNormal[k] = i.localNormal[k]; o->localAxis[k] = i.localAxis[k];
        o->localAnchor[k] = i.localAnchor[k];
    }
    o->type = NX_JOINT_D6;
    o->maxForce = i.maxForce; o->maxTorque = i.maxTorque;
    o->userData = j && WJ(j) ? WJ(j)->userData : 0; o->name = i.name; o->jointFlags = i.jointFlags;
    o->xMotion = i.xMotion; o->yMotion = i.yMotion; o->zMotion = i.zMotion;
    o->swing1Motion = i.swing1Motion; o->swing2Motion = i.swing2Motion; o->twistMotion = i.twistMotion;
    Soft28To25(i.linearLimit, o->linearLimit); Soft28To25(i.swing1Limit, o->swing1Limit);
    Soft28To25(i.swing2Limit, o->swing2Limit);
    Soft28To25(i.twistLimit.low, o->twistLimit.low); Soft28To25(i.twistLimit.high, o->twistLimit.high);
    Drive28To25(i.xDrive, o->xDrive); Drive28To25(i.yDrive, o->yDrive); Drive28To25(i.zDrive, o->zDrive);
    Drive28To25(i.swingDrive, o->swingDrive); Drive28To25(i.twistDrive, o->twistDrive);
    Drive28To25(i.slerpDrive, o->slerpDrive);
    o->drivePosition = i.drivePosition; o->driveOrientation = i.driveOrientation;
    o->driveLinearVelocity = i.driveLinearVelocity; o->driveAngularVelocity = i.driveAngularVelocity;
    o->projectionMode = i.projectionMode; o->projectionDistance = i.projectionDistance;
    o->projectionAngle = i.projectionAngle; o->gearRatio = i.gearRatio; o->flags = i.flags;
}

/* ---- cloth mesh data ------------------------------------------------------------ */

void MeshData25To28(const MeshData25 *i, NxMeshData &o)
{
    o.verticesPosBegin = i->verticesPosBegin; o.verticesNormalBegin = i->verticesNormalBegin;
    o.verticesPosByteStride = i->verticesPosByteStride; o.verticesNormalByteStride = i->verticesNormalByteStride;
    o.maxVertices = i->maxVertices; o.numVerticesPtr = i->numVerticesPtr;
    o.indicesBegin = i->indicesBegin; o.indicesByteStride = i->indicesByteStride;
    o.maxIndices = i->maxIndices; o.numIndicesPtr = i->numIndicesPtr;
    o.parentIndicesBegin = i->parentIndicesBegin; o.parentIndicesByteStride = i->parentIndicesByteStride;
    o.maxParentIndices = i->maxParentIndices; o.numParentIndicesPtr = i->numParentIndicesPtr;
    o.dirtyBufferFlagsPtr = NULL;
    o.flags = i->flags; o.name = i->name;
}

void MeshData28To25(const NxMeshData &i, MeshData25 *o)
{
    o->verticesPosBegin = i.verticesPosBegin; o->verticesNormalBegin = i.verticesNormalBegin;
    o->verticesPosByteStride = i.verticesPosByteStride; o->verticesNormalByteStride = i.verticesNormalByteStride;
    o->maxVertices = i.maxVertices; o->numVerticesPtr = i.numVerticesPtr;
    o->indicesBegin = i.indicesBegin; o->indicesByteStride = i.indicesByteStride;
    o->maxIndices = i.maxIndices; o->numIndicesPtr = i.numIndicesPtr;
    o->parentIndicesBegin = i.parentIndicesBegin; o->parentIndicesByteStride = i.parentIndicesByteStride;
    o->maxParentIndices = i.maxParentIndices; o->numParentIndicesPtr = i.numParentIndicesPtr;
    o->flags = i.flags; o->name = i.name;
}

/* ---- NxParameter: 2.5 (= 2.6.4 numbering) -> 2.8.4 --------------------------------- */

NxParameter Param25To28(NxU32 p, bool *ok)
{
    /* 0..14 have the same meaning and value in both versions */
    *ok = true;
    if (p <= 14) return (NxParameter)p;
    *ok = false;                     /* visualisation parameters only - not mapped */
    return (NxParameter)0;
}
