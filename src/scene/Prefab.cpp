#include "scene/Prefab.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
    using TokenMap = std::map<std::string, std::vector<std::string>>;

    struct Props
    {
        std::vector<std::string> order;
        TokenMap tokens;
    };

    Props Tokenize(const Entity& e)
    {
        Props p;
        for (const EntityProperty& prop : SerializeEntity(e, true))
        {
            p.order.push_back(prop.key);
            p.tokens[prop.key] = TokenizeLine(prop.line);
        }
        return p;
    }

    std::string Join(const std::vector<std::string>& tokens)
    {
        std::ostringstream out;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            if (i) out << ' ';
            if (!tokens[i].empty() && tokens[i][0] == '"') out << std::quoted(tokens[i].substr(1));
            else out << tokens[i];
        }
        return out.str();
    }

    // Root position, rotation (quaternion + euler) and name always belong to the instance.
    const std::vector<std::string>& ImplicitRootOverrides()
    {
        static const std::vector<std::string> keys = {
            "object:2", "transform:1", "transform:2", "transform:3", "transform:4", "transform:5", "transform:6", "transform:7",
            "transform:11", "transform:12", "transform:13",
        };
        return keys;
    }

    bool IsImplicit(const std::string& o) { return std::find(ImplicitRootOverrides().begin(), ImplicitRootOverrides().end(), o) != ImplicitRootOverrides().end(); }

    // Sort key that keeps each script's fields right after it.
    std::pair<int, int> Rank(const std::string& key)
    {
        static const char* fixed[] = { "object", "transform", "mesh", "light", "camera", "rigidbody", "collider", "probe", "volume" };
        for (int i = 0; i < 9; ++i)
            if (key == fixed[i]) return { i, 0 };
        if (key.rfind("script#", 0) == 0) return { 100 + 2 * std::atoi(key.c_str() + 7), 0 };
        if (key.rfind("field#", 0) == 0) return { 100 + 2 * std::atoi(key.c_str() + 6), 1 };
        return { 1000, 0 };
    }

    // instance := source, except for the properties listed in overrides.
    void Merge(Entity& instance, const Entity& source, const std::vector<std::string>& overrides)
    {
        const std::set<std::string> ov(overrides.begin(), overrides.end());
        const Props I = Tokenize(instance), S = Tokenize(source);
        std::vector<std::pair<std::string, std::string>> lines;
        for (const std::string& key : S.order)
        {
            if (ov.count(key + ":-")) continue;
            auto inst = I.tokens.find(key);
            if (ov.count(key + ":*") && inst != I.tokens.end())
            {
                lines.push_back({ key, Join(inst->second) });
                continue;
            }
            std::vector<std::string> tokens = S.tokens.at(key);
            if (inst != I.tokens.end())
                for (size_t i = 1; i < tokens.size() && i < inst->second.size(); ++i)
                    if (ov.count(key + ":" + std::to_string(i))) tokens[i] = inst->second[i];
            lines.push_back({ key, Join(tokens) });
        }
        for (const std::string& key : I.order)
            if (!S.tokens.count(key) && ov.count(key + ":*")) lines.push_back({ key, Join(I.tokens.at(key)) });
        std::stable_sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) { return Rank(a.first) < Rank(b.first); });

        Entity merged;
        merged.id = instance.id;
        merged.parent = instance.parent;
        merged.prefab = instance.prefab;
        merged.prefabId = instance.prefabId;
        merged.prefabOverrides = instance.prefabOverrides;
        for (const auto& [key, line] : lines) ParseEntityLine(merged, line);
        instance = std::move(merged);
    }

    const Entity* FindLocal(const Prefab::Contents& contents, EntityId local)
    {
        for (const Entity& e : contents)
            if (e.id == local) return &e;
        return nullptr;
    }

    std::vector<EntityId> Subtree(const Scene& scene, EntityId root)
    {
        std::vector<EntityId> ids{ root };
        for (const Entity& e : scene.entities)
            if (scene.IsAncestor(root, e.id)) ids.push_back(e.id);
        return ids;
    }
}

namespace Prefab
{
    bool IsPrefabFile(const std::string& path)
    {
        std::string ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".prefab";
    }

    bool Load(const std::string& path, Contents& out)
    {
        std::ifstream file(path);
        if (!file) return false;
        std::string header;
        int version = 0;
        file >> header >> version;
        if (header != "TheEnginePrefab") return false;
        std::string rest;
        std::getline(file, rest);
        Contents contents;
        EntityId next = 1;
        if (!ReadEntities(file, contents, next) || contents.empty()) return false;
        out = std::move(contents);
        return true;
    }

    bool SaveContents(const std::string& path, const Contents& contents)
    {
        std::ofstream out(path);
        if (!out) return false;
        out << "TheEnginePrefab 1\n";
        WriteEntities(out, contents);
        return static_cast<bool>(out);
    }

    bool CreateFromEntity(Scene& scene, EntityId root, const std::string& path)
    {
        if (!scene.Find(root)) return false;
        const std::vector<EntityId> ids = Subtree(scene, root);
        std::map<EntityId, EntityId> local;
        for (size_t i = 0; i < ids.size(); ++i) local[ids[i]] = static_cast<EntityId>(i + 1);

        Contents contents;
        for (EntityId id : ids)
        {
            Entity e = *scene.Find(id);
            e.id = local[id];
            e.parent = id == root ? kNullEntity : local[e.parent];
            e.prefab.clear();
            e.prefabId = kNullEntity;
            e.prefabOverrides.clear();
            contents.push_back(e);
        }
        if (!SaveContents(path, contents)) return false;

        for (EntityId id : ids)
        {
            Entity* e = scene.Find(id);
            e->prefabId = local[id];
            e->prefab = id == root ? path : std::string();
            e->prefabOverrides = id == root ? ImplicitRootOverrides() : std::vector<std::string>{};
        }
        return true;
    }

    EntityId Instantiate(Scene& scene, const Contents& contents, const std::string& path, EntityId parent)
    {
        if (contents.empty()) return kNullEntity;
        std::map<EntityId, EntityId> ids;
        for (const Entity& p : contents)
        {
            Entity copy = p;
            const EntityId id = scene.Create(p.name).id;
            copy.id = id;
            copy.parent = kNullEntity;
            copy.prefabId = p.id;
            copy.prefab.clear();
            copy.prefabOverrides.clear();
            *scene.Find(id) = copy;
            ids[p.id] = id;
        }
        const EntityId root = ids[contents[0].id];
        for (const Entity& p : contents)
            scene.Find(ids[p.id])->parent = p.id == contents[0].id ? parent : ids[p.parent];
        Entity* r = scene.Find(root);
        r->prefab = path;
        r->prefabOverrides = ImplicitRootOverrides();
        return root;
    }

    EntityId InstanceRoot(const Scene& scene, EntityId id)
    {
        for (const Entity* e = scene.Find(id); e; e = scene.Find(e->parent))
        {
            if (!e->prefab.empty()) return e->id;
            if (e->prefabId == kNullEntity) return kNullEntity;
        }
        return kNullEntity;
    }

    std::vector<EntityId> InstanceMembers(const Scene& scene, EntityId root)
    {
        std::vector<EntityId> members;
        for (EntityId id : Subtree(scene, root))
            if (InstanceRoot(scene, id) == root) members.push_back(id);
        return members;
    }

    std::vector<std::string> ComputeOverrides(const Entity& instance, const Entity& source, bool isRoot)
    {
        std::vector<std::string> out;
        const Props I = Tokenize(instance), S = Tokenize(source);
        for (const std::string& key : I.order)
        {
            auto s = S.tokens.find(key);
            if (s == S.tokens.end())
            {
                out.push_back(key + ":*");
                continue;
            }
            const auto& a = I.tokens.at(key);
            const auto& b = s->second;
            if (a.size() != b.size() && key.rfind("field#", 0) != 0)
            {
                out.push_back(key + ":*");
                continue;
            }
            for (size_t i = 1; i < std::max(a.size(), b.size()); ++i)
                if (i >= a.size() || i >= b.size() || a[i] != b[i]) out.push_back(key + ":" + std::to_string(i));
        }
        for (const std::string& key : S.order)
            if (!I.tokens.count(key)) out.push_back(key + ":-");
        if (isRoot)
            for (const std::string& o : ImplicitRootOverrides())
                if (std::find(out.begin(), out.end(), o) == out.end()) out.push_back(o);
        return out;
    }

    void RefreshOverrides(Scene& scene, EntityId root, const Contents& contents)
    {
        for (EntityId id : InstanceMembers(scene, root))
        {
            Entity* e = scene.Find(id);
            if (const Entity* p = FindLocal(contents, e->prefabId))
                e->prefabOverrides = ComputeOverrides(*e, *p, id == root);
        }
    }

    void SyncInstance(Scene& scene, EntityId root, const Contents& contents, bool keepOverrides)
    {
        if (contents.empty() || !scene.Find(root)) return;
        std::map<EntityId, EntityId> byLocal;
        for (EntityId id : InstanceMembers(scene, root))
        {
            const Entity* e = scene.Find(id);
            if (!e) continue;
            if (id != root && !FindLocal(contents, e->prefabId))
            {
                scene.Destroy(id); // removed from the prefab
                continue;
            }
            byLocal[id == root ? contents[0].id : e->prefabId] = id;
        }

        for (const Entity& p : contents)
        {
            const bool isRoot = p.id == contents[0].id;
            auto it = byLocal.find(p.id);
            if (it != byLocal.end())
            {
                Entity* e = scene.Find(it->second);
                if (!keepOverrides) e->prefabOverrides = isRoot ? ImplicitRootOverrides() : std::vector<std::string>{};
                Merge(*e, p, e->prefabOverrides);
                e->prefabId = p.id;
                continue;
            }
            // Added to the prefab since this instance was made.
            Entity copy = p;
            const EntityId id = scene.Create(p.name).id;
            copy.id = id;
            copy.parent = kNullEntity;
            copy.prefabId = p.id;
            copy.prefab.clear();
            copy.prefabOverrides.clear();
            *scene.Find(id) = copy;
            byLocal[p.id] = id;
        }
        for (const Entity& p : contents)
        {
            if (p.id == contents[0].id) continue;
            Entity* e = scene.Find(byLocal[p.id]);
            if (e && byLocal.count(p.parent)) e->parent = byLocal[p.parent];
        }
    }

    bool ApplyInstance(Scene& scene, EntityId root)
    {
        Entity* r = scene.Find(root);
        if (!r || r->prefab.empty()) return false;
        const std::string path = r->prefab;
        const std::vector<EntityId> ids = Subtree(scene, root);

        // Children added on the instance become part of the prefab.
        EntityId next = 1;
        for (EntityId id : ids) next = std::max(next, scene.Find(id)->prefabId + 1);
        for (EntityId id : ids)
        {
            Entity* e = scene.Find(id);
            if (id != root && (e->prefabId == kNullEntity || !e->prefab.empty()))
            {
                e->prefab.clear(); // nested instances are unpacked into this prefab
                e->prefabId = next++;
            }
        }
        if (scene.Find(root)->prefabId == kNullEntity) scene.Find(root)->prefabId = next++;

        Contents contents;
        for (EntityId id : ids)
        {
            Entity e = *scene.Find(id);
            e.id = e.prefabId;
            e.parent = id == root ? kNullEntity : scene.Find(e.parent)->prefabId;
            e.prefab.clear();
            e.prefabId = kNullEntity;
            e.prefabOverrides.clear();
            contents.push_back(e);
        }
        if (!SaveContents(path, contents)) return false;
        for (EntityId id : ids)
        {
            Entity* e = scene.Find(id);
            e->prefabOverrides = id == root ? ImplicitRootOverrides() : std::vector<std::string>{};
        }
        return true;
    }

    std::vector<std::string> DescribeOverrides(const Scene& scene, EntityId root)
    {
        std::vector<std::string> out;
        for (EntityId id : InstanceMembers(scene, root))
        {
            const Entity* e = scene.Find(id);
            std::set<std::string> components;
            for (const std::string& o : e->prefabOverrides)
            {
                if (id == root && IsImplicit(o)) continue;
                const std::string key = o.substr(0, o.rfind(':'));
                const std::string what = o.substr(o.rfind(':') + 1);
                std::string name;
                if (key == "object") name = "GameObject";
                else if (key == "transform") name = "Transform";
                else if (key == "mesh") name = "Mesh Renderer";
                else if (key == "light") name = "Light";
                else if (key == "camera") name = "Camera";
                else if (key == "rigidbody") name = "Rigidbody";
                else if (key == "collider") name = "Collider";
                else if (key == "probe") name = "Reflection Probe";
                else if (key == "volume") name = "Volume";
                else if (key.rfind("script#", 0) == 0 || key.rfind("field#", 0) == 0)
                {
                    const size_t index = static_cast<size_t>(std::atoi(key.c_str() + key.find('#') + 1));
                    name = index < e->scripts.size() ? e->scripts[index].className + " (Script)" : "Script";
                }
                else name = key;
                if (what == "*" && key.rfind("script#", 0) == 0) name += " added";
                if (what == "-") name += " removed";
                components.insert(name);
            }
            for (const std::string& c : components) out.push_back(e->name + ": " + c);
        }
        return out;
    }
}
