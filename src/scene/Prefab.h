#pragma once

#include "scene/Scene.h"

#include <string>
#include <vector>

// Prefabs (Unity-style): a .prefab file stores an entity subtree with prefab-local ids (the root is the first
// entity, parent 0). Each entity of an instance remembers its prefab-local id; the instance root also stores the
// prefab path.
//
// Overrides are tracked per property token ("mesh:2" = the 2nd token of the mesh line, "script#0:*" = a whole
// script added on the instance). When a prefab changes, every instance takes the new prefab values except for its
// overrides, which are found by comparing the instance with the previous version of the prefab. The root's
// position, rotation and name are always overrides, as in Unity.
namespace Prefab
{
    using Contents = std::vector<Entity>;

    bool IsPrefabFile(const std::string& path);
    bool Load(const std::string& path, Contents& out);
    bool SaveContents(const std::string& path, const Contents& contents);

    // Writes the subtree at root to path and turns it into an instance of the new prefab.
    bool CreateFromEntity(Scene& scene, EntityId root, const std::string& path);

    // Adds a new instance to the scene and returns its root.
    EntityId Instantiate(Scene& scene, const Contents& contents, const std::string& path, EntityId parent);

    // Instance root for any entity of an instance (kNullEntity if it isn't part of one).
    EntityId InstanceRoot(const Scene& scene, EntityId id);
    // All entities of the instance whose root is given (children added on the instance are excluded).
    std::vector<EntityId> InstanceMembers(const Scene& scene, EntityId root);

    // Properties of `instance` that differ from `source` (plus the implicit root overrides).
    std::vector<std::string> ComputeOverrides(const Entity& instance, const Entity& source, bool isRoot);

    // Recomputes the stored overrides of every entity of the instance against the given prefab contents.
    void RefreshOverrides(Scene& scene, EntityId root, const Contents& contents);

    // Makes the instance match the prefab, keeping each entity's stored overrides (revert = with none). Entities
    // added to the prefab are created, removed ones are deleted, children added on the instance are kept.
    void SyncInstance(Scene& scene, EntityId root, const Contents& contents, bool keepOverrides = true);

    // Writes the instance (including its overrides) back to the prefab file ("Apply All").
    bool ApplyInstance(Scene& scene, EntityId root);

    // Human readable override list for the Inspector ("Transform (Scale)", "Mesh Renderer", ...).
    std::vector<std::string> DescribeOverrides(const Scene& scene, EntityId root);
}
