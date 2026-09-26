#include "scene/Scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <functional>

glm::mat4 Transform::Matrix() const
{
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
}

void Transform::SetEuler(const glm::vec3& degrees)
{
    euler = degrees;
    // Same application order as Unity: Z, then X, then Y.
    glm::vec3 r = glm::radians(degrees);
    rotation = glm::normalize(glm::quat_cast(glm::eulerAngleYXZ(r.y, r.x, r.z)));
}

void Transform::SyncEulerFromRotation()
{
    float y, x, z;
    glm::extractEulerAngleYXZ(glm::mat4_cast(rotation), y, x, z);
    glm::vec3 fresh = glm::degrees(glm::vec3(x, y, z));
    // Keep the representation closest to the previous one to avoid visible jumps (e.g. 180 vs -180).
    for (int i = 0; i < 3; ++i)
    {
        while (fresh[i] - euler[i] > 180.0f) fresh[i] -= 360.0f;
        while (fresh[i] - euler[i] < -180.0f) fresh[i] += 360.0f;
        if (std::fabs(fresh[i]) < 1e-4f) fresh[i] = 0.0f; // avoid displaying "-0"
    }
    euler = fresh;
}

Entity& Scene::Create(const std::string& entityName, EntityId parent)
{
    Entity e;
    e.id = m_NextId++;
    e.name = entityName;
    e.parent = parent;
    entities.push_back(e);
    return entities.back();
}

int Scene::IndexOf(EntityId id) const
{
    for (size_t i = 0; i < entities.size(); ++i)
        if (entities[i].id == id) return static_cast<int>(i);
    return -1;
}

Entity* Scene::Find(EntityId id)
{
    int i = IndexOf(id);
    return i >= 0 ? &entities[i] : nullptr;
}

const Entity* Scene::Find(EntityId id) const
{
    int i = IndexOf(id);
    return i >= 0 ? &entities[i] : nullptr;
}

glm::mat4 Scene::WorldMatrix(EntityId id) const
{
    const Entity* e = Find(id);
    if (!e) return glm::mat4(1.0f);
    glm::mat4 local = e->transform.Matrix();
    return e->parent != kNullEntity ? WorldMatrix(e->parent) * local : local;
}

void Scene::SetWorldMatrix(EntityId id, const glm::mat4& world)
{
    Entity* e = Find(id);
    if (!e) return;
    glm::mat4 local = e->parent != kNullEntity ? glm::inverse(WorldMatrix(e->parent)) * world : world;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::vec3 scale, translation;
    glm::quat rotation;
    if (!glm::decompose(local, scale, rotation, translation, skew, perspective)) return;
    e->transform.position = translation;
    e->transform.rotation = glm::normalize(rotation);
    e->transform.scale = scale;
    e->transform.SyncEulerFromRotation();
}

bool Scene::IsActiveInHierarchy(EntityId id) const
{
    for (const Entity* e = Find(id); e; e = Find(e->parent))
        if (!e->active) return false;
    return true;
}

bool Scene::IsAncestor(EntityId ancestor, EntityId id) const
{
    const Entity* e = Find(id);
    while (e && e->parent != kNullEntity)
    {
        if (e->parent == ancestor) return true;
        e = Find(e->parent);
    }
    return false;
}

std::vector<EntityId> Scene::Children(EntityId parent) const
{
    std::vector<EntityId> out;
    for (const auto& e : entities)
        if (e.parent == parent) out.push_back(e.id);
    return out;
}

void Scene::SetParent(EntityId child, EntityId parent, bool keepWorld)
{
    if (child == parent || IsAncestor(child, parent)) return; // would create a cycle
    Entity* e = Find(child);
    if (!e) return;
    glm::mat4 world = WorldMatrix(child);
    e->parent = parent;
    if (keepWorld) SetWorldMatrix(child, world);

    // Move to the end so it becomes the last child.
    int idx = IndexOf(child);
    Entity moved = entities[idx];
    entities.erase(entities.begin() + idx);
    entities.push_back(moved);
}

void Scene::MoveBefore(EntityId id, EntityId before)
{
    int from = IndexOf(id);
    if (from < 0 || id == before) return;
    Entity moved = entities[from];
    entities.erase(entities.begin() + from);
    int to = IndexOf(before);
    if (to < 0) entities.push_back(moved);
    else entities.insert(entities.begin() + to, moved);
}

void Scene::Destroy(EntityId id)
{
    for (EntityId child : Children(id))
        Destroy(child);
    int idx = IndexOf(id);
    if (idx >= 0) entities.erase(entities.begin() + idx);
}

EntityId Scene::CloneRecursive(EntityId src, EntityId newParent, int insertAt)
{
    const Entity* s = Find(src);
    if (!s) return kNullEntity;
    Entity copy = *s;
    copy.id = m_NextId++;
    copy.parent = newParent;
    if (insertAt >= 0 && insertAt <= static_cast<int>(entities.size()))
        entities.insert(entities.begin() + insertAt, copy);
    else
        entities.push_back(copy);
    EntityId newId = copy.id;
    for (EntityId child : Children(src))
        CloneRecursive(child, newId, -1);
    return newId;
}

void Scene::RecalculateNextId()
{
    m_NextId = 1;
    for (const Entity& e : entities) m_NextId = std::max(m_NextId, e.id + 1);
}

EntityId Scene::Duplicate(EntityId id)
{
    const Entity* e = Find(id);
    if (!e) return kNullEntity;
    // Insert right after the original's subtree position, like Unity.
    EntityId newId = CloneRecursive(id, e->parent, IndexOf(id) + 1);
    if (Entity* n = Find(newId)) n->name = UniqueName(*this, Find(id)->name);
    return newId;
}

std::string UniqueName(const Scene& scene, const std::string& base)
{
    // Strip an existing " (n)" suffix.
    std::string root = base;
    if (!root.empty() && root.back() == ')')
    {
        size_t open = root.rfind(" (");
        if (open != std::string::npos) root = root.substr(0, open);
    }
    auto exists = [&](const std::string& n) {
        return std::any_of(scene.entities.begin(), scene.entities.end(), [&](const Entity& e) { return e.name == n; });
    };
    if (!exists(root)) return root;
    for (int i = 1;; ++i)
    {
        std::string candidate = root + " (" + std::to_string(i) + ")";
        if (!exists(candidate)) return candidate;
    }
}

ScriptField* ScriptComponent::FindField(const std::string& fieldName)
{
    for (ScriptField& f : fields)
        if (f.name == fieldName) return &f;
    return nullptr;
}

void Scene::CreateDefault()
{
    {
        Entity& cam = Create("Main Camera");
        cam.transform.position = { 0.0f, 2.0f, 9.0f };
        cam.transform.SetEuler({ -8.0f, 0.0f, 0.0f });
        cam.camera.enabled = true;
    }
    {
        Entity& light = Create("Directional Light");
        light.transform.position = { 0.0f, 3.0f, 0.0f };
        light.transform.SetEuler({ -35.0f, -30.0f, 0.0f });
        light.light.enabled = true;
    }
    {
        Entity& ground = Create("Ground");
        ground.meshRenderer.enabled = true;
        ground.meshRenderer.mesh = "Plane";
        ground.meshRenderer.color = { 0.45f, 0.45f, 0.45f };
        ground.meshRenderer.smoothness = 0.2f;
        ground.transform.scale = { 2.0f, 1.0f, 2.0f };
    }
    {
        Entity& cube = Create("Cube");
        cube.meshRenderer.enabled = true;
        cube.meshRenderer.mesh = "Cube";
        cube.meshRenderer.color = { 0.85f, 0.3f, 0.25f };
        cube.transform.position = { -2.5f, 0.5f, 0.0f };
        cube.transform.SetEuler({ 0.0f, 25.0f, 0.0f });
        ScriptComponent rotator;
        rotator.className = "Rotator";
        rotator.fields.push_back({ "degreesPerSecond", "Vector3", "0 45 0" });
        cube.scripts.push_back(rotator);
    }
    {
        Entity& sphere = Create("Sphere");
        sphere.meshRenderer.enabled = true;
        sphere.meshRenderer.mesh = "Sphere";
        sphere.meshRenderer.color = { 0.95f, 0.8f, 0.4f };
        sphere.meshRenderer.metallic = 1.0f;
        sphere.meshRenderer.smoothness = 0.85f;
        sphere.transform.position = { 0.0f, 0.75f, 0.0f };
        sphere.transform.scale = glm::vec3(1.5f);
    }
    {
        Entity& capsule = Create("Capsule");
        capsule.meshRenderer.enabled = true;
        capsule.meshRenderer.mesh = "Capsule";
        capsule.meshRenderer.color = { 0.3f, 0.55f, 0.9f };
        capsule.transform.position = { 2.5f, 1.0f, 0.0f };
        EntityId capsuleId = capsule.id;

        Entity& hat = Create("Hat", capsuleId);
        hat.meshRenderer.enabled = true;
        hat.meshRenderer.mesh = "Cylinder";
        hat.meshRenderer.color = { 0.15f, 0.15f, 0.18f };
        hat.transform.position = { 0.0f, 1.05f, 0.0f };
        hat.transform.scale = { 0.7f, 0.1f, 0.7f };
    }
    {
        Entity& lamp = Create("Point Light");
        lamp.light.enabled = true;
        lamp.light.type = LightType::Point;
        lamp.light.color = { 1.0f, 0.6f, 0.3f };
        lamp.light.intensity = 3.0f;
        lamp.light.range = 6.0f;
        lamp.transform.position = { 1.2f, 1.2f, 1.6f };
    }
    {
        Entity& volume = Create("Global Volume");
        volume.volume.enabled = true;
        PostProcessSettings& s = volume.volume.settings;
        s.bloom = true;
        s.bloomIntensity = 0.35f;
        s.bloomThreshold = 1.0f;
        s.vignette = true;
        s.vignetteIntensity = 0.2f;
        s.tonemapping = true;
        s.tonemapper = 1;
    }
    {
        Entity& probe = Create("Reflection Probe");
        probe.transform.position = { 0.0f, 1.0f, 0.0f };
        probe.reflectionProbe.enabled = true;
        probe.reflectionProbe.size = { 14.0f, 6.0f, 14.0f };
    }
    for (Entity& e : entities) AddDefaultCollider(e);
}

PostProcessSettings Scene::ResolvePostProcess(const glm::vec3& cameraPos) const
{
    // Neutral values; each volume that overrides a group moves it towards its own values.
    PostProcessSettings r;
    r.bloomIntensity = 0.0f;
    r.vignetteIntensity = 0.0f;
    r.tonemapper = 1;
    std::vector<const Entity*> volumes;
    for (const Entity& e : entities)
        if (e.volume.enabled && IsActiveInHierarchy(e.id)) volumes.push_back(&e);
    std::stable_sort(volumes.begin(), volumes.end(), [](const Entity* a, const Entity* b) { return a->volume.priority < b->volume.priority; });

    for (const Entity* e : volumes)
    {
        const VolumeComponent& v = e->volume;
        float f = std::clamp(v.weight, 0.0f, 1.0f);
        if (!v.isGlobal)
        {
            const glm::mat4 world = WorldMatrix(e->id);
            const glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
            const glm::vec3 half = v.size * scale * 0.5f;
            const glm::vec3 d = glm::max(glm::abs(cameraPos - glm::vec3(world[3])) - half, glm::vec3(0.0f));
            const float dist = glm::length(d);
            f *= dist <= 0.0f ? 1.0f : v.blendDistance > 0.0f ? std::clamp(1.0f - dist / v.blendDistance, 0.0f, 1.0f) : 0.0f;
        }
        if (f <= 0.0f) continue;
        const PostProcessSettings& s = v.settings;
        auto mix = [f](auto a, auto b) { return a + (b - a) * f; };
        if (s.bloom)
        {
            r.bloom = true;
            r.bloomIntensity = mix(r.bloomIntensity, s.bloomIntensity);
            r.bloomThreshold = mix(r.bloomThreshold, s.bloomThreshold);
            r.bloomScatter = mix(r.bloomScatter, s.bloomScatter);
            r.bloomTint = mix(r.bloomTint, s.bloomTint);
        }
        if (s.colorAdjustments)
        {
            r.colorAdjustments = true;
            r.postExposure = mix(r.postExposure, s.postExposure);
            r.contrast = mix(r.contrast, s.contrast);
            r.saturation = mix(r.saturation, s.saturation);
            r.colorFilter = mix(r.colorFilter, s.colorFilter);
        }
        if (s.whiteBalance)
        {
            r.whiteBalance = true;
            r.temperature = mix(r.temperature, s.temperature);
            r.tint = mix(r.tint, s.tint);
        }
        if (s.vignette)
        {
            r.vignette = true;
            r.vignetteIntensity = mix(r.vignetteIntensity, s.vignetteIntensity);
            r.vignetteSmoothness = mix(r.vignetteSmoothness, s.vignetteSmoothness);
            r.vignetteColor = mix(r.vignetteColor, s.vignetteColor);
        }
        if (s.tonemapping && f >= 0.5f)
        {
            r.tonemapping = true;
            r.tonemapper = s.tonemapper;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// Serialization: a small line-based text format.
// ---------------------------------------------------------------------------
namespace
{
    std::ostream& operator<<(std::ostream& os, const glm::vec3& v) { return os << v.x << ' ' << v.y << ' ' << v.z; }
    std::istream& operator>>(std::istream& is, glm::vec3& v) { return is >> v.x >> v.y >> v.z; }

    // Fields added in later versions are optional at the end of a line, so older files still load.
    template <typename T>
    void Optional(std::istream& is, T& field)
    {
        T value{};
        if (is >> value) field = value;
    }
}

void AddDefaultCollider(Entity& e)
{
    if (!e.meshRenderer.enabled) return;
    ColliderComponent& c = e.collider;
    c = ColliderComponent{};
    c.enabled = true;
    const std::string& m = e.meshRenderer.mesh;
    if (m == "Sphere") c.shape = ColliderShape::Sphere;
    else if (m == "Capsule" || m == "Cylinder") c.shape = ColliderShape::Capsule;
    else if (m == "Cube") c.shape = ColliderShape::Box;
    else c.shape = ColliderShape::Mesh;
}

std::vector<EntityProperty> SerializeEntity(const Entity& e, bool includeObject)
{
    std::vector<EntityProperty> props;
    auto add = [&](const std::string& key, const std::ostringstream& line) { props.push_back({ key, line.str() }); };
    if (includeObject)
    {
        std::ostringstream o;
        o << "object " << e.active << ' ' << std::quoted(e.name);
        add("object", o);
    }
    {
        const Transform& t = e.transform;
        std::ostringstream o;
        o << "transform " << t.position << ' ' << t.rotation.x << ' ' << t.rotation.y << ' ' << t.rotation.z << ' '
          << t.rotation.w << ' ' << t.scale << ' ' << t.euler;
        add("transform", o);
    }
    {
        const auto& m = e.meshRenderer;
        std::ostringstream o;
        o << "mesh " << m.enabled << ' ' << std::quoted(m.mesh) << ' ' << m.color << ' ' << m.metallic << ' '
          << m.smoothness << ' ' << std::quoted(m.material) << ' ' << m.castShadows << ' ' << m.shadowsOnly;
        add("mesh", o);
    }
    {
        const auto& l = e.light;
        std::ostringstream o;
        o << "light " << l.enabled << ' ' << l.color << ' ' << l.intensity << ' ' << l.castShadows << ' '
          << l.shadowStrength << ' ' << static_cast<int>(l.type) << ' ' << l.range << ' ' << l.spotAngle << ' '
          << l.innerSpotAngle;
        add("light", o);
    }
    {
        const auto& c = e.camera;
        std::ostringstream o;
        o << "camera " << c.enabled << ' ' << c.fov << ' ' << c.nearClip << ' ' << c.farClip << ' ' << c.orthographic << ' ' << c.orthoSize;
        add("camera", o);
    }
    {
        const auto& r = e.rigidbody;
        std::ostringstream o;
        o << "rigidbody " << r.enabled << ' ' << r.mass << ' ' << r.drag << ' ' << r.angularDrag << ' ' << r.useGravity << ' ' << r.isKinematic;
        add("rigidbody", o);
    }
    {
        const auto& c = e.collider;
        std::ostringstream o;
        o << "collider " << c.enabled << ' ' << static_cast<int>(c.shape) << ' ' << c.center << ' ' << c.size << ' ' << c.radius << ' '
          << c.height << ' ' << c.isTrigger << ' ' << c.friction << ' ' << c.bounciness;
        add("collider", o);
    }
    {
        const auto& c = e.characterController;
        std::ostringstream o;
        o << "charactercontroller " << c.enabled << ' ' << c.height << ' ' << c.radius << ' ' << c.center << ' ' << c.slopeLimit << ' ' << c.stepOffset;
        add("charactercontroller", o);
    }
    {
        const auto& a = e.animator;
        std::ostringstream o;
        o << "animator " << a.enabled << ' ' << std::quoted(a.controller) << ' ' << a.applyRootMotion << ' ' << std::quoted(a.lookBones) << ' ' << a.handIk << ' ' << std::quoted(a.rig);
        add("animator", o);
    }
    {
        const auto& b = e.boneSocket;
        std::ostringstream o;
        o << "socket " << b.enabled << ' ' << std::quoted(b.bone) << ' ' << b.position << ' ' << b.euler << ' ' << b.followRotation;
        add("socket", o);
    }
    {
        const auto& d = e.dynamicBones;
        std::ostringstream o;
        o << "dynbones " << d.enabled << ' ' << d.chains.size();
        for (const auto& c : d.chains)
            o << ' ' << std::quoted(c.root) << ' ' << c.damping << ' ' << c.elasticity << ' ' << c.stiffness << ' ' << c.inertia << ' ' << c.gravity;
        add("dynbones", o);
    }
    {
        const auto& p = e.reflectionProbe;
        std::ostringstream o;
        o << "probe " << p.enabled << ' ' << p.size << ' ' << p.boxProjection << ' ' << p.intensity;
        add("probe", o);
    }
    {
        const auto& v = e.volume;
        const PostProcessSettings& s = v.settings;
        std::ostringstream o;
        o << "volume " << v.enabled << ' ' << v.isGlobal << ' ' << v.size << ' ' << v.blendDistance << ' ' << v.weight << ' ' << v.priority << ' '
          << s.bloom << ' ' << s.bloomIntensity << ' ' << s.bloomThreshold << ' ' << s.bloomScatter << ' ' << s.bloomTint << ' '
          << s.colorAdjustments << ' ' << s.postExposure << ' ' << s.contrast << ' ' << s.saturation << ' ' << s.colorFilter << ' '
          << s.whiteBalance << ' ' << s.temperature << ' ' << s.tint << ' '
          << s.vignette << ' ' << s.vignetteIntensity << ' ' << s.vignetteSmoothness << ' ' << s.vignetteColor << ' '
          << s.tonemapping << ' ' << s.tonemapper;
        add("volume", o);
    }
    for (size_t i = 0; i < e.scripts.size(); ++i)
    {
        const ScriptComponent& sc = e.scripts[i];
        std::ostringstream o;
        o << "script " << sc.enabled << ' ' << std::quoted(sc.className);
        add("script#" + std::to_string(i), o);
        for (const ScriptField& f : sc.fields)
        {
            std::ostringstream fo;
            fo << "field " << std::quoted(f.name) << ' ' << std::quoted(f.type) << ' ' << std::quoted(f.value);
            add("field#" + std::to_string(i) + "#" + f.name, fo);
        }
    }
    return props;
}

bool ParseEntityLine(Entity& e, const std::string& line)
{
    std::istringstream in(line);
    std::string key;
    if (!(in >> key)) return true;
    if (key == "object")
    {
        in >> e.active >> std::quoted(e.name);
    }
    else if (key == "transform")
    {
        Transform& t = e.transform;
        in >> t.position >> t.rotation.x >> t.rotation.y >> t.rotation.z >> t.rotation.w >> t.scale >> t.euler;
    }
    else if (key == "mesh")
    {
        auto& m = e.meshRenderer;
        in >> m.enabled >> std::ws;
        if (in.peek() == '"')
        {
            in >> std::quoted(m.mesh) >> m.color >> m.metallic >> m.smoothness;
            if (in.fail()) return false;
            in >> std::ws;
            if (in.peek() == '"') in >> std::quoted(m.material);
            Optional(in, m.castShadows);
            Optional(in, m.shadowsOnly);
            return true;
        }
        // Version 1: primitive enum index.
        int type = 0;
        in >> type >> m.color >> m.metallic >> m.smoothness;
        m.mesh = PrimitiveName(static_cast<PrimitiveType>(std::clamp(type, 0, static_cast<int>(PrimitiveType::Count) - 1)));
    }
    else if (key == "light")
    {
        auto& l = e.light;
        in >> l.enabled >> l.color >> l.intensity;
        if (in.fail()) return false;
        Optional(in, l.castShadows);
        Optional(in, l.shadowStrength);
        int type = 0;
        if (in >> type) l.type = static_cast<LightType>(std::clamp(type, 0, 2));
        Optional(in, l.range);
        Optional(in, l.spotAngle);
        Optional(in, l.innerSpotAngle);
        return true;
    }
    else if (key == "camera")
    {
        auto& c = e.camera;
        in >> c.enabled >> c.fov >> c.nearClip >> c.farClip >> c.orthographic >> c.orthoSize;
    }
    else if (key == "rigidbody")
    {
        auto& r = e.rigidbody;
        in >> r.enabled >> r.mass >> r.drag >> r.angularDrag >> r.useGravity >> r.isKinematic;
    }
    else if (key == "collider")
    {
        auto& c = e.collider;
        int shape = 0;
        in >> c.enabled >> shape >> c.center >> c.size >> c.radius >> c.height >> c.isTrigger >> c.friction >> c.bounciness;
        c.shape = static_cast<ColliderShape>(std::clamp(shape, 0, 3));
    }
    else if (key == "charactercontroller")
    {
        auto& c = e.characterController;
        in >> c.enabled >> c.height >> c.radius >> c.center >> c.slopeLimit >> c.stepOffset;
    }
    else if (key == "animator")
    {
        auto& a = e.animator;
        in >> a.enabled >> std::quoted(a.controller) >> a.applyRootMotion >> std::ws;
        if (in.peek() == '"')
        {
            in >> std::quoted(a.lookBones) >> a.handIk >> std::ws;
            if (in.peek() == '"') in >> std::quoted(a.rig);
        }
        return true;
    }
    else if (key == "socket")
    {
        auto& b = e.boneSocket;
        in >> b.enabled >> std::quoted(b.bone) >> b.position >> b.euler >> b.followRotation;
    }
    else if (key == "dynbones")
    {
        auto& d = e.dynamicBones;
        size_t count = 0;
        in >> d.enabled >> count;
        d.chains.clear();
        for (size_t i = 0; i < count && in; ++i)
        {
            DynamicBoneChain c;
            in >> std::quoted(c.root) >> c.damping >> c.elasticity >> c.stiffness >> c.inertia >> c.gravity;
            d.chains.push_back(c);
        }
    }
    else if (key == "probe")
    {
        auto& p = e.reflectionProbe;
        in >> p.enabled >> p.size >> p.boxProjection >> p.intensity;
    }
    else if (key == "volume")
    {
        auto& v = e.volume;
        PostProcessSettings& s = v.settings;
        in >> v.enabled >> v.isGlobal >> v.size >> v.blendDistance >> v.weight >> v.priority
           >> s.bloom >> s.bloomIntensity >> s.bloomThreshold >> s.bloomScatter >> s.bloomTint
           >> s.colorAdjustments >> s.postExposure >> s.contrast >> s.saturation >> s.colorFilter
           >> s.whiteBalance >> s.temperature >> s.tint
           >> s.vignette >> s.vignetteIntensity >> s.vignetteSmoothness >> s.vignetteColor
           >> s.tonemapping >> s.tonemapper;
    }
    else if (key == "script")
    {
        ScriptComponent sc;
        in >> sc.enabled >> std::quoted(sc.className);
        e.scripts.push_back(sc);
    }
    else if (key == "field")
    {
        if (e.scripts.empty()) return true;
        ScriptField f;
        in >> std::quoted(f.name) >> std::quoted(f.type) >> std::quoted(f.value);
        e.scripts.back().fields.push_back(f);
    }
    else if (key == "prefab")
    {
        in >> std::quoted(e.prefab) >> e.prefabId;
        std::string o;
        while (in >> std::quoted(o)) e.prefabOverrides.push_back(o);
        return true;
    }
    else if (key == "rotator")
    {
        // Version 1 built-in Rotator component: now a C# script with the same field.
        bool enabled = false;
        glm::vec3 speed(0.0f);
        in >> enabled >> speed;
        if (!in.fail() && enabled)
        {
            ScriptComponent sc;
            sc.className = "Rotator";
            std::ostringstream v;
            v << speed.x << ' ' << speed.y << ' ' << speed.z;
            sc.fields.push_back({ "degreesPerSecond", "Vector3", v.str() });
            e.scripts.push_back(sc);
        }
        return true;
    }
    else
    {
        return true; // unknown key: skip line
    }
    return !in.fail();
}

std::vector<std::string> TokenizeLine(const std::string& line)
{
    std::vector<std::string> tokens;
    std::istringstream in(line);
    std::string t;
    while (in >> std::ws, in.good())
    {
        if (in.peek() == '"') { in >> std::quoted(t); t = "\"" + t; } // marker so an empty string is still a token
        else in >> t;
        tokens.push_back(t);
    }
    return tokens;
}

void WriteEntities(std::ostream& out, const std::vector<Entity>& entities)
{
    for (const Entity& e : entities)
    {
        out << "entity " << e.id << ' ' << e.parent << ' ' << e.active << ' ' << std::quoted(e.name) << "\n";
        for (const EntityProperty& p : SerializeEntity(e, false))
            out << (p.key.rfind("field#", 0) == 0 ? "    " : "  ") << p.line << "\n";
        if (!e.prefab.empty() || e.prefabId != kNullEntity)
        {
            out << "  prefab " << std::quoted(e.prefab) << ' ' << e.prefabId;
            for (const std::string& o : e.prefabOverrides) out << ' ' << std::quoted(o);
            out << "\n";
        }
    }
}

bool ReadEntities(std::istream& file, std::vector<Entity>& entities, EntityId& nextId,
                  std::function<void(const std::string&, std::istringstream&)> other)
{
    Entity* current = nullptr;
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream in(line);
        std::string key;
        if (!(in >> key)) continue;
        if (key == "entity")
        {
            entities.emplace_back();
            current = &entities.back();
            in >> current->id >> current->parent >> current->active >> std::quoted(current->name);
            if (in.fail()) return false;
            nextId = std::max(nextId, current->id + 1);
        }
        else if (current)
        {
            if (!ParseEntityLine(*current, line)) return false;
        }
        else if (other)
        {
            other(key, in);
        }
    }
    return true;
}

bool Scene::Save(const std::string& path) const
{
    std::ofstream out(path);
    if (!out) return false;
    out << "TheEngineScene 2\n";
    out << "name " << std::quoted(name) << "\n";
    out << "sky " << sky.enabled << ' ' << sky.sunSize << ' ' << sky.sunConvergence << ' ' << sky.atmosphereThickness << ' '
        << sky.skyTint << ' ' << sky.groundColor << ' ' << sky.exposure << ' ' << sky.ambientIntensity << ' '
        << sky.cloudCoverage << ' ' << sky.cloudDensity << ' ' << sky.cloudSpeed << ' ' << sky.cloudScale << ' '
        << sky.stars << ' ' << sky.fallbackColor << ' ' << sky.shadowDistance << ' '
        << sky.ssao << ' ' << sky.ssaoRadius << ' ' << sky.ssaoIntensity << ' ' << sky.reflectionIntensity << "\n";
    WriteEntities(out, entities);
    return static_cast<bool>(out);
}

bool Scene::Load(const std::string& path)
{
    std::ifstream file(path);
    if (!file) return false;
    std::string header;
    int version = 0;
    file >> header >> version;
    if (header != "TheEngineScene") return false;
    std::string rest;
    std::getline(file, rest);

    Scene loaded;
    bool ok = true;
    const bool read = ReadEntities(file, loaded.entities, loaded.m_NextId, [&](const std::string& key, std::istringstream& in) {
        if (key == "name") in >> std::quoted(loaded.name);
        else if (key == "sky")
        {
            SkySettings& s = loaded.sky;
            in >> s.enabled >> s.sunSize >> s.sunConvergence >> s.atmosphereThickness >> s.skyTint >> s.groundColor
               >> s.exposure >> s.ambientIntensity >> s.cloudCoverage >> s.cloudDensity >> s.cloudSpeed >> s.cloudScale
               >> s.stars >> s.fallbackColor;
            if (in.fail()) { ok = false; return; }
            Optional(in, s.shadowDistance);
            Optional(in, s.ssao);
            Optional(in, s.ssaoRadius);
            Optional(in, s.ssaoIntensity);
            Optional(in, s.reflectionIntensity);
        }
    });
    if (!read || !ok) return false;
    *this = std::move(loaded);
    return true;
}
