#include "physics/Physics.h"

#include "core/Log.h"
#include "render/Mesh.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cstdarg>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <tuple>
#include <unordered_map>

namespace
{
    using namespace JPH;

    namespace Layers
    {
        constexpr ObjectLayer NonMoving = 0;
        constexpr ObjectLayer Moving = 1;
        constexpr ObjectLayer Count = 2;
    }

    class BroadPhaseLayers final : public BroadPhaseLayerInterface
    {
    public:
        uint GetNumBroadPhaseLayers() const override { return 2; }
        BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override { return BroadPhaseLayer(static_cast<BroadPhaseLayer::Type>(layer)); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(BroadPhaseLayer layer) const override { return static_cast<BroadPhaseLayer::Type>(layer) == 0 ? "NonMoving" : "Moving"; }
#endif
    };

    class ObjectVsBroadPhase final : public ObjectVsBroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(ObjectLayer layer, BroadPhaseLayer bp) const override
        {
            return layer == Layers::Moving || static_cast<BroadPhaseLayer::Type>(bp) == Layers::Moving;
        }
    };

    class ObjectPairs final : public ObjectLayerPairFilter
    {
    public:
        bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override { return a == Layers::Moving || b == Layers::Moving; }
    };

    Vec3 ToJ(const glm::vec3& v) { return Vec3(v.x, v.y, v.z); }
    glm::vec3 ToG(Vec3Arg v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
    Quat ToJ(const glm::quat& q) { return Quat(q.x, q.y, q.z, q.w).Normalized(); }
    glm::quat ToG(QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

    void TraceImpl(const char* fmt, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        LOG_INFO("[Jolt] %s", buffer);
    }

    // Collects enter/exit events per body pair (a pair can touch through several sub-shapes).
    class Contacts final : public ContactListener
    {
    public:
        void OnContactAdded(const Body& b1, const Body& b2, const ContactManifold& manifold, ContactSettings&) override
        {
            std::lock_guard lock(mutex);
            const SubKey sub{ b1.GetID().GetIndexAndSequenceNumber(), manifold.mSubShapeID1.GetValue(),
                              b2.GetID().GetIndexAndSequenceNumber(), manifold.mSubShapeID2.GetValue() };
            if (!subShapes.insert(sub).second) return;
            Pair& p = pairs[PairKey(b1.GetID(), b2.GetID())];
            if (p.count++ > 0) return;
            p.a = static_cast<EntityId>(b1.GetUserData());
            p.b = static_cast<EntityId>(b2.GetUserData());
            p.trigger = b1.IsSensor() || b2.IsSensor();
            CollisionEvent e;
            e.type = p.trigger ? CollisionEventType::TriggerEnter : CollisionEventType::CollisionEnter;
            e.a = p.a;
            e.b = p.b;
            if (!manifold.mRelativeContactPointsOn1.empty())
            {
                const RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
                e.point = { static_cast<float>(point.GetX()), static_cast<float>(point.GetY()), static_cast<float>(point.GetZ()) };
            }
            e.normal = ToG(manifold.mWorldSpaceNormal);
            e.relativeVelocity = ToG(b2.GetLinearVelocity() - b1.GetLinearVelocity());
            events.push_back(e);
        }

        void OnContactRemoved(const SubShapeIDPair& pair) override
        {
            std::lock_guard lock(mutex);
            const SubKey sub{ pair.GetBody1ID().GetIndexAndSequenceNumber(), pair.GetSubShapeID1().GetValue(),
                              pair.GetBody2ID().GetIndexAndSequenceNumber(), pair.GetSubShapeID2().GetValue() };
            if (subShapes.erase(sub) == 0) return;
            auto it = pairs.find(PairKey(pair.GetBody1ID(), pair.GetBody2ID()));
            if (it == pairs.end() || --it->second.count > 0) return;
            CollisionEvent e;
            e.type = it->second.trigger ? CollisionEventType::TriggerExit : CollisionEventType::CollisionExit;
            e.a = it->second.a;
            e.b = it->second.b;
            events.push_back(e);
            pairs.erase(it);
        }

        // A removed body leaves silently (Unity doesn't send exit messages for destroyed objects either).
        void Forget(BodyID id)
        {
            std::lock_guard lock(mutex);
            const uint32 v = id.GetIndexAndSequenceNumber();
            for (auto it = subShapes.begin(); it != subShapes.end();)
                it = (std::get<0>(*it) == v || std::get<2>(*it) == v) ? subShapes.erase(it) : std::next(it);
            for (auto it = pairs.begin(); it != pairs.end();)
                it = (it->first.first == v || it->first.second == v) ? pairs.erase(it) : std::next(it);
        }

        std::vector<CollisionEvent> Take()
        {
            std::lock_guard lock(mutex);
            return std::move(events);
        }

    private:
        using SubKey = std::tuple<uint32, uint32, uint32, uint32>;
        struct Pair
        {
            int count = 0;
            EntityId a = kNullEntity, b = kNullEntity;
            bool trigger = false;
        };
        static std::pair<uint32, uint32> PairKey(BodyID a, BodyID b)
        {
            const uint32 x = a.GetIndexAndSequenceNumber(), y = b.GetIndexAndSequenceNumber();
            return { std::min(x, y), std::max(x, y) };
        }

        std::mutex mutex;
        std::set<SubKey> subShapes;
        std::map<std::pair<uint32, uint32>, Pair> pairs;
        std::vector<CollisionEvent> events;
    };

    std::unique_ptr<TempAllocatorImpl> g_Temp;
    std::unique_ptr<JobSystemThreadPool> g_Jobs;
}

struct PhysicsWorld::Impl
{
    struct BodyRecord
    {
        BodyID id;
        std::string signature;
        glm::vec3 position{ 0.0f };
        glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 scale{ 1.0f };
        bool dynamic = false, kinematic = false;
        bool seen = false;
    };

    BroadPhaseLayers bpLayers;
    ObjectVsBroadPhase objectVsBp;
    ObjectPairs objectPairs;
    Contacts contacts;
    std::unique_ptr<PhysicsSystem> system;
    Scene* scene = nullptr;
    MeshProvider meshes;
    std::unordered_map<EntityId, BodyRecord> bodies;
    struct CharacterRecord
    {
        Ref<CharacterVirtual> character;
        std::string signature;
        glm::vec3 position{ 0.0f }; // entity world position after the last Move (teleports are detected)
        glm::vec3 velocity{ 0.0f };
        bool grounded = false;
    };
    std::unordered_map<EntityId, CharacterRecord> characters;
    float accumulator = 0.0f;
    float fixedDt = 0.02f;

    BodyInterface& BI() { return system->GetBodyInterface(); }

    static void Decompose(const glm::mat4& m, glm::vec3& pos, glm::quat& rot, glm::vec3& scale)
    {
        glm::vec3 skew;
        glm::vec4 persp;
        glm::decompose(m, scale, rot, pos, skew, persp);
        rot = glm::normalize(rot);
    }

    std::string Signature(const Entity& e, const glm::vec3& scale) const
    {
        const RigidbodyComponent& r = e.rigidbody;
        const ColliderComponent& c = e.collider;
        std::ostringstream s;
        s.precision(4);
        s << r.enabled << r.isKinematic << ' ' << r.mass << ' ' << r.drag << ' ' << r.angularDrag << ' ' << r.useGravity << '|'
          << c.enabled << static_cast<int>(c.shape) << ' ' << c.center.x << ' ' << c.center.y << ' ' << c.center.z << ' '
          << c.size.x << ' ' << c.size.y << ' ' << c.size.z << ' ' << c.radius << ' ' << c.height << ' ' << c.isTrigger << ' '
          << c.friction << ' ' << c.bounciness << '|' << scale.x << ' ' << scale.y << ' ' << scale.z << '|'
          << (c.shape == ColliderShape::Mesh ? e.meshRenderer.mesh : std::string());
        return s.str();
    }

    ShapeRefC BuildShape(const Entity& e, const glm::vec3& worldScale, bool dynamic)
    {
        const ColliderComponent& c = e.collider;
        const glm::vec3 scale = glm::abs(worldScale);
        if (!c.enabled) return new EmptyShape();

        ShapeSettings::ShapeResult result;
        switch (c.shape)
        {
        case ColliderShape::Box:
        {
            const glm::vec3 half = glm::max(glm::abs(c.size) * scale * 0.5f, glm::vec3(1e-3f));
            const float minHalf = std::min({ half.x, half.y, half.z });
            result = BoxShapeSettings(ToJ(half), std::min(cDefaultConvexRadius, minHalf * 0.5f)).Create();
            break;
        }
        case ColliderShape::Sphere:
            result = SphereShapeSettings(std::max(c.radius * std::max({ scale.x, scale.y, scale.z }), 1e-3f)).Create();
            break;
        case ColliderShape::Capsule:
        {
            const float radius = std::max(c.radius * std::max(scale.x, scale.z), 1e-3f);
            const float halfCylinder = c.height * scale.y * 0.5f - radius;
            if (halfCylinder <= 1e-3f) result = SphereShapeSettings(radius).Create();
            else result = CapsuleShapeSettings(halfCylinder, radius).Create();
            break;
        }
        case ColliderShape::Mesh:
        {
            const MeshData* mesh = meshes ? meshes(e.meshRenderer.mesh) : nullptr;
            if (!mesh || mesh->vertices.empty())
            {
                LOG_WARN("Mesh Collider on '%s' has no mesh; using an empty shape", e.name.c_str());
                return new EmptyShape();
            }
            if (dynamic)
            {
                // Like Unity, a mesh collider on a non-kinematic rigidbody must be convex.
                Array<Vec3> points;
                points.reserve(mesh->vertices.size());
                for (const Vertex& v : mesh->vertices) points.push_back(ToJ(v.position * scale));
                result = ConvexHullShapeSettings(points).Create();
                if (result.HasError())
                {
                    const glm::vec3 half = glm::max((mesh->boundsMax - mesh->boundsMin) * scale * 0.5f, glm::vec3(0.01f));
                    result = RotatedTranslatedShapeSettings(ToJ((mesh->boundsMax + mesh->boundsMin) * 0.5f * scale), Quat::sIdentity(),
                                                            new BoxShapeSettings(ToJ(half), 0.0f)).Create();
                }
            }
            else
            {
                VertexList vertices;
                vertices.reserve(mesh->vertices.size());
                for (const Vertex& v : mesh->vertices)
                {
                    const glm::vec3 p = v.position * scale;
                    vertices.push_back(Float3(p.x, p.y, p.z));
                }
                IndexedTriangleList triangles;
                triangles.reserve(mesh->indices.size() / 3);
                for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3)
                    triangles.push_back(IndexedTriangle(mesh->indices[i], mesh->indices[i + 1], mesh->indices[i + 2], 0));
                result = MeshShapeSettings(vertices, triangles).Create();
            }
            break;
        }
        }
        if (result.HasError())
        {
            LOG_WARN("Collider on '%s': %s", e.name.c_str(), result.GetError().c_str());
            return new EmptyShape();
        }
        ShapeRefC shape = result.Get();
        if (glm::dot(c.center, c.center) > 1e-10f)
            shape = RotatedTranslatedShapeSettings(ToJ(c.center * worldScale), Quat::sIdentity(), shape).Create().Get();
        return shape;
    }

    void RemoveBody(EntityId id)
    {
        auto it = bodies.find(id);
        if (it == bodies.end()) return;
        contacts.Forget(it->second.id);
        BI().RemoveBody(it->second.id);
        BI().DestroyBody(it->second.id);
        bodies.erase(it);
    }

    // Creates, rebuilds, teleports or removes the body for one entity so it matches the scene.
    BodyRecord* Sync(const Entity& e)
    {
        const bool wanted = (e.collider.enabled || e.rigidbody.enabled) && scene->IsActiveInHierarchy(e.id);
        if (!wanted)
        {
            RemoveBody(e.id);
            return nullptr;
        }

        glm::vec3 pos, scale;
        glm::quat rot;
        Decompose(scene->WorldMatrix(e.id), pos, rot, scale);
        const std::string signature = Signature(e, scale);

        auto it = bodies.find(e.id);
        if (it != bodies.end() && it->second.signature == signature)
        {
            BodyRecord& rec = it->second;
            const bool moved = glm::distance(pos, rec.position) > 1e-4f || std::abs(glm::dot(rot, rec.rotation)) < 0.99999f;
            if (rec.kinematic)
                BI().MoveKinematic(rec.id, RVec3(ToJ(pos)), ToJ(rot), fixedDt);
            else if (moved)
                BI().SetPositionAndRotation(rec.id, RVec3(ToJ(pos)), ToJ(rot), EActivation::Activate);
            rec.position = pos;
            rec.rotation = rot;
            return &rec;
        }

        // (Re)build. A rebuilt dynamic body keeps its velocity.
        Vec3 linear = Vec3::sZero(), angular = Vec3::sZero();
        if (it != bodies.end())
        {
            linear = BI().GetLinearVelocity(it->second.id);
            angular = BI().GetAngularVelocity(it->second.id);
            RemoveBody(e.id);
        }

        const RigidbodyComponent& r = e.rigidbody;
        const bool dynamic = r.enabled && !r.isKinematic;
        const bool kinematic = r.enabled && r.isKinematic;
        ShapeRefC shape = BuildShape(e, scale, dynamic);
        const EMotionType motion = dynamic ? EMotionType::Dynamic : kinematic ? EMotionType::Kinematic : EMotionType::Static;
        BodyCreationSettings s(shape, RVec3(ToJ(pos)), ToJ(rot), motion, motion == EMotionType::Static ? Layers::NonMoving : Layers::Moving);
        s.mUserData = e.id;
        s.mIsSensor = e.collider.enabled && e.collider.isTrigger;
        s.mCollideKinematicVsNonDynamic = s.mIsSensor;
        s.mFriction = e.collider.friction;
        s.mRestitution = e.collider.bounciness;
        s.mLinearDamping = r.drag;
        s.mAngularDamping = r.angularDrag;
        s.mGravityFactor = r.useGravity ? 1.0f : 0.0f;
        if (motion != EMotionType::Static)
        {
            s.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
            s.mMassPropertiesOverride.mMass = std::max(r.mass, 1e-4f);
            if (shape->GetSubType() == EShapeSubType::Empty)
            {
                // A rigidbody without colliders still simulates (unit inertia), it just doesn't collide.
                s.mOverrideMassProperties = EOverrideMassProperties::MassAndInertiaProvided;
                s.mMassPropertiesOverride.mInertia = Mat44::sScale(std::max(r.mass, 1e-4f) * 0.4f);
                s.mMassPropertiesOverride.mInertia.SetColumn4(3, Vec4(0, 0, 0, 1));
            }
        }
        s.mLinearVelocity = linear;
        s.mAngularVelocity = angular;
        Body* body = BI().CreateBody(s);
        if (!body)
        {
            LOG_ERROR("Physics: out of bodies");
            return nullptr;
        }
        BI().AddBody(body->GetID(), motion == EMotionType::Static ? EActivation::DontActivate : EActivation::Activate);

        BodyRecord rec;
        rec.id = body->GetID();
        rec.signature = signature;
        rec.position = pos;
        rec.rotation = rot;
        rec.scale = scale;
        rec.dynamic = dynamic;
        rec.kinematic = kinematic;
        return &(bodies[e.id] = rec);
    }

    void SyncAll()
    {
        for (auto& [id, rec] : bodies) rec.seen = false;
        for (const Entity& e : scene->entities)
            if (BodyRecord* rec = Sync(e)) rec->seen = true;
        std::vector<EntityId> stale;
        for (auto& [id, rec] : bodies)
            if (!rec.seen) stale.push_back(id);
        for (EntityId id : stale) RemoveBody(id);
    }

    void WriteBack()
    {
        for (auto& [id, rec] : bodies)
        {
            if (!rec.dynamic || !BI().IsActive(rec.id)) continue;
            RVec3 p;
            Quat q;
            BI().GetPositionAndRotation(rec.id, p, q);
            rec.position = { static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ()) };
            rec.rotation = glm::normalize(ToG(q));
            const glm::mat4 world = glm::translate(glm::mat4(1.0f), rec.position) * glm::mat4_cast(rec.rotation) *
                                    glm::scale(glm::mat4(1.0f), rec.scale);
            scene->SetWorldMatrix(id, world);
        }
    }

    CharacterRecord* Character(EntityId id)
    {
        if (!scene || !system) return nullptr;
        const Entity* e = scene->Find(id);
        if (!e || !e->characterController.enabled)
        {
            characters.erase(id);
            return nullptr;
        }
        glm::vec3 pos, scale;
        glm::quat rot;
        Decompose(scene->WorldMatrix(id), pos, rot, scale);
        const CharacterControllerComponent& cc = e->characterController;
        char sig[128];
        std::snprintf(sig, sizeof(sig), "%.4f %.4f %.4f %.4f %.4f %.3f %.3f", cc.height, cc.radius, cc.center.x, cc.center.y, cc.center.z,
                      cc.slopeLimit, scale.y);
        auto it = characters.find(id);
        if (it == characters.end() || it->second.signature != sig)
        {
            // Capsule standing on the object's origin (offset by center), like Unity's CharacterController.
            const float radius = std::max(cc.radius * std::max(scale.x, scale.z), 0.01f);
            const float halfCylinder = std::max(cc.height * scale.y * 0.5f - radius, 0.01f);
            Ref<CharacterVirtualSettings> settings = new CharacterVirtualSettings();
            settings->mShape = RotatedTranslatedShapeSettings(ToJ(cc.center * scale), Quat::sIdentity(), new CapsuleShape(halfCylinder, radius)).Create().Get();
            settings->mMaxSlopeAngle = glm::radians(std::clamp(cc.slopeLimit, 1.0f, 89.0f));
            const float bottom = cc.center.y * scale.y - cc.height * scale.y * 0.5f;
            settings->mSupportingVolume = Plane(Vec3::sAxisY(), -(bottom + radius)); // contacts below the lower sphere's center support
            settings->mCharacterPadding = 0.02f;
            CharacterRecord rec;
            rec.character = new CharacterVirtual(settings, RVec3(ToJ(pos)), Quat::sIdentity(), 0, system.get());
            rec.signature = sig;
            rec.position = pos;
            it = characters.insert_or_assign(id, rec).first;
        }
        CharacterRecord& rec = it->second;
        if (glm::distance(rec.position, pos) > 1e-4f) // moved by the editor or a script: teleport
        {
            rec.character->SetPosition(RVec3(ToJ(pos)));
            rec.position = pos;
        }
        return &rec;
    }

    BodyRecord* Ensure(EntityId id)
    {
        if (!scene || !system) return nullptr;
        const Entity* e = scene->Find(id);
        return e ? Sync(*e) : nullptr;
    }
};

void PhysicsWorld::GlobalInit()
{
    RegisterDefaultAllocator();
    Trace = TraceImpl;
    Factory::sInstance = new Factory();
    RegisterTypes();
    g_Temp = std::make_unique<TempAllocatorImpl>(16 * 1024 * 1024);
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    g_Jobs = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, std::min(threads, 8));
}

void PhysicsWorld::GlobalShutdown()
{
    g_Jobs.reset();
    g_Temp.reset();
    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
}

PhysicsWorld::PhysicsWorld() : m(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() { End(); }

void PhysicsWorld::Begin(Scene* scene, MeshProvider meshes)
{
    End();
    m->scene = scene;
    m->meshes = std::move(meshes);
    m->system = std::make_unique<PhysicsSystem>();
    m->system->Init(65536, 0, 65536, 16384, m->bpLayers, m->objectVsBp, m->objectPairs);
    m->system->SetContactListener(&m->contacts);
    m->system->SetGravity(ToJ(gravity));
    m->accumulator = 0.0f;
    m->fixedDt = fixedDeltaTime;
    m->SyncAll();
    m->system->OptimizeBroadPhase();
}

void PhysicsWorld::End()
{
    if (!m->system) return;
    m->characters.clear();
    std::vector<EntityId> ids;
    for (auto& [id, rec] : m->bodies) ids.push_back(id);
    for (EntityId id : ids) m->RemoveBody(id);
    m->contacts.Take();
    m->system.reset();
    m->scene = nullptr;
}

bool PhysicsWorld::Running() const { return m->system != nullptr; }
int PhysicsWorld::BodyCount() const { return static_cast<int>(m->bodies.size()); }

void PhysicsWorld::Update(float dt, const std::function<void()>& fixedUpdate,
                          const std::function<void(const std::vector<CollisionEvent>&)>& onEvents)
{
    if (!m->system) return;
    m->fixedDt = std::max(fixedDeltaTime, 1e-3f);
    m->accumulator += std::min(dt, 0.25f);
    int steps = 0;
    while (m->accumulator >= m->fixedDt && steps < 8)
    {
        if (fixedUpdate) fixedUpdate();
        m->system->SetGravity(ToJ(gravity));
        m->SyncAll();
        const EPhysicsUpdateError error = m->system->Update(m->fixedDt, 1, g_Temp.get(), g_Jobs.get());
        if (error != EPhysicsUpdateError::None) LOG_WARN("Physics update error %u", static_cast<unsigned>(error));
        m->WriteBack();
        std::vector<CollisionEvent> events = m->contacts.Take();
        if (onEvents && !events.empty()) onEvents(events);
        m->accumulator -= m->fixedDt;
        ++steps;
    }
    m->accumulator = std::min(m->accumulator, m->fixedDt); // don't spiral when frames are slow
}

bool PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit) const
{
    if (!m->system || glm::dot(direction, direction) < 1e-12f) return false;
    const float distance = std::min(maxDistance, 1e5f);
    const glm::vec3 dir = glm::normalize(direction);
    RRayCast ray{ RVec3(ToJ(origin)), ToJ(dir * distance) };
    RayCastResult result;
    if (!m->system->GetNarrowPhaseQuery().CastRay(ray, result)) return false;
    const RVec3 point = ray.GetPointOnRay(result.mFraction);
    hit.point = { static_cast<float>(point.GetX()), static_cast<float>(point.GetY()), static_cast<float>(point.GetZ()) };
    hit.distance = result.mFraction * distance;
    BodyLockRead lock(m->system->GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded())
    {
        const Body& body = lock.GetBody();
        hit.entity = static_cast<EntityId>(body.GetUserData());
        hit.normal = ToG(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
    }
    return true;
}

int PhysicsWorld::CharacterMove(EntityId id, const glm::vec3& motion, float dt)
{
    auto* rec = m->Character(id);
    if (!rec || dt <= 0.0f) return 0;
    const Entity* e = m->scene->Find(id);
    CharacterVirtual& c = *rec->character;
    c.SetLinearVelocity(ToJ(motion / dt));
    CharacterVirtual::ExtendedUpdateSettings update;
    update.mStickToFloorStepDown = Vec3(0.0f, -std::max(e->characterController.stepOffset, 0.05f), 0.0f);
    update.mWalkStairsStepUp = Vec3(0.0f, std::max(e->characterController.stepOffset, 0.0f), 0.0f);
    // A character only collides with the world (not with itself); sensors are ignored like Unity's triggers.
    BroadPhaseLayerFilter bpFilter;
    ObjectLayerFilter layerFilter;
    class NoSensors final : public BodyFilter
    {
    public:
        bool ShouldCollideLocked(const Body& body) const override { return !body.IsSensor(); }
    } bodyFilter;
    ShapeFilter shapeFilter;
    const glm::vec3 before = rec->position;
    c.ExtendedUpdate(dt, ToJ(gravity), update, bpFilter, layerFilter, bodyFilter, shapeFilter, *g_Temp);

    const RVec3 p = c.GetPosition();
    const glm::vec3 after(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ()));
    rec->velocity = (after - before) / dt;
    rec->grounded = c.GetGroundState() == CharacterBase::EGroundState::OnGround;
    // Write the new position back (keeping rotation / scale).
    glm::mat4 world = m->scene->WorldMatrix(id);
    world[3] = glm::vec4(after, 1.0f);
    m->scene->SetWorldMatrix(id, world);
    rec->position = after;

    int flags = rec->grounded ? 4 : 0;
    for (const CharacterContact& contact : c.GetActiveContacts())
    {
        if (!contact.mHadCollision) continue;
        const float ny = contact.mSurfaceNormal.GetY();
        if (ny < -0.5f) flags |= 2;
        else if (ny < 0.5f) flags |= 1;
    }
    return flags;
}

bool PhysicsWorld::CharacterGrounded(EntityId id)
{
    auto* rec = m->Character(id);
    return rec && rec->grounded;
}

glm::vec3 PhysicsWorld::CharacterVelocity(EntityId id)
{
    auto* rec = m->Character(id);
    return rec ? rec->velocity : glm::vec3(0.0f);
}

glm::vec3 PhysicsWorld::GetVelocity(EntityId id)
{
    auto* rec = m->Ensure(id);
    return rec ? ToG(m->BI().GetLinearVelocity(rec->id)) : glm::vec3(0.0f);
}

void PhysicsWorld::SetVelocity(EntityId id, const glm::vec3& v)
{
    if (auto* rec = m->Ensure(id); rec && rec->dynamic) m->BI().SetLinearVelocity(rec->id, ToJ(v));
}

glm::vec3 PhysicsWorld::GetAngularVelocity(EntityId id)
{
    auto* rec = m->Ensure(id);
    return rec ? ToG(m->BI().GetAngularVelocity(rec->id)) : glm::vec3(0.0f);
}

void PhysicsWorld::SetAngularVelocity(EntityId id, const glm::vec3& v)
{
    if (auto* rec = m->Ensure(id); rec && rec->dynamic) m->BI().SetAngularVelocity(rec->id, ToJ(v));
}

void PhysicsWorld::AddForce(EntityId id, const glm::vec3& force, ForceMode mode)
{
    auto* rec = m->Ensure(id);
    if (!rec || !rec->dynamic) return;
    const Entity* e = m->scene->Find(id);
    const float mass = e ? std::max(e->rigidbody.mass, 1e-4f) : 1.0f;
    switch (mode)
    {
    case ForceMode::Force: m->BI().AddForce(rec->id, ToJ(force)); break;
    case ForceMode::Acceleration: m->BI().AddForce(rec->id, ToJ(force * mass)); break;
    case ForceMode::Impulse: m->BI().AddImpulse(rec->id, ToJ(force)); break;
    case ForceMode::VelocityChange: m->BI().AddLinearVelocity(rec->id, ToJ(force)); break;
    }
}

void PhysicsWorld::AddTorque(EntityId id, const glm::vec3& torque, ForceMode mode)
{
    auto* rec = m->Ensure(id);
    if (!rec || !rec->dynamic) return;
    switch (mode)
    {
    case ForceMode::Force:
    case ForceMode::Acceleration: m->BI().AddTorque(rec->id, ToJ(torque)); break;
    case ForceMode::Impulse: m->BI().AddAngularImpulse(rec->id, ToJ(torque)); break;
    case ForceMode::VelocityChange:
        m->BI().SetAngularVelocity(rec->id, m->BI().GetAngularVelocity(rec->id) + ToJ(torque));
        break;
    }
}
