// FBX import through ufbx: meshes (split per material), skin weights, skeleton, materials and animation clips.
// Everything is converted to the engine's model space: Y up, meters, character facing -Z.
#include "anim/Animation.h"
#include "core/Log.h"
#include "render/Resources.h"
#include "scene/Material.h"

#include <ufbx.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace
{
    ufbx_scene* LoadScene(const std::string& path, bool geometry)
    {
        ufbx_load_opts opts = {};
        opts.target_axes.right = UFBX_COORDINATE_AXIS_NEGATIVE_X;
        opts.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
        opts.target_axes.front = UFBX_COORDINATE_AXIS_NEGATIVE_Z; // characters face the engine's forward (-Z)
        opts.target_unit_meters = 1.0f;
        opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
        opts.generate_missing_normals = true;
        opts.ignore_geometry = !geometry;
        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
        if (!scene)
        {
            char message[512];
            ufbx_format_error(message, sizeof(message), &error);
            LOG_ERROR("Failed to load %s: %s", path.c_str(), message);
        }
        return scene;
    }

    glm::mat4 ToGlm(const ufbx_matrix& m)
    {
        glm::mat4 r(1.0f);
        for (int c = 0; c < 4; ++c)
            r[c] = glm::vec4(static_cast<float>(m.cols[c].x), static_cast<float>(m.cols[c].y), static_cast<float>(m.cols[c].z), c == 3 ? 1.0f : 0.0f);
        return r;
    }

    BoneTransform Decompose(const glm::mat4& m)
    {
        BoneTransform x;
        glm::vec3 skew;
        glm::vec4 persp;
        glm::decompose(m, x.s, x.r, x.t, skew, persp);
        x.r = glm::normalize(x.r);
        return x;
    }

    std::string SafeName(std::string s, const std::string& fallback)
    {
        for (char& c : s)
            if (std::string("\\/:*?\"<>|").find(c) != std::string::npos) c = '_';
        return s.empty() ? fallback : s;
    }

    std::string Relative(const fs::path& p)
    {
        std::error_code ec;
        fs::path rel = fs::relative(p, fs::current_path(), ec);
        return (ec || rel.empty() ? p : rel).generic_string();
    }

    // Skeleton = bone nodes, nodes used by skin clusters, and their ancestors (in parent-first order).
    std::shared_ptr<Skeleton> BuildSkeleton(ufbx_scene* scene)
    {
        std::set<const ufbx_node*> wanted;
        auto addChain = [&](const ufbx_node* n) {
            for (; n && !n->is_root; n = n->parent) wanted.insert(n);
        };
        for (size_t i = 0; i < scene->nodes.count; ++i)
            if (scene->nodes.data[i]->bone) addChain(scene->nodes.data[i]);
        for (size_t i = 0; i < scene->skin_clusters.count; ++i) addChain(scene->skin_clusters.data[i]->bone_node);
        // Mesh nodes are not bones.
        for (size_t i = 0; i < scene->nodes.count; ++i)
        {
            const ufbx_node* n = scene->nodes.data[i];
            if (n->mesh && !n->bone)
            {
                bool usedByCluster = false;
                for (size_t c = 0; c < scene->skin_clusters.count; ++c) usedByCluster |= scene->skin_clusters.data[c]->bone_node == n;
                if (!usedByCluster) wanted.erase(n);
            }
        }
        if (wanted.empty()) return nullptr;

        auto skeleton = std::make_shared<Skeleton>();
        std::map<const ufbx_node*, int> indexOf;
        std::function<void(const ufbx_node*, int)> visit = [&](const ufbx_node* n, int parent) {
            int self = parent;
            if (wanted.count(n))
            {
                self = static_cast<int>(skeleton->names.size());
                indexOf[n] = self;
                skeleton->names.push_back(n->name.data);
                skeleton->parents.push_back(parent);
                const glm::mat4 world = ToGlm(n->node_to_world);
                glm::mat4 local = world;
                for (const ufbx_node* p = n->parent; p && !p->is_root; p = p->parent)
                    if (wanted.count(p))
                    {
                        local = glm::inverse(ToGlm(p->node_to_world)) * world;
                        break;
                    }
                skeleton->rest.push_back(Decompose(local));
                skeleton->index[n->name.data] = self;
            }
            for (size_t c = 0; c < n->children.count; ++c) visit(n->children.data[c], self);
        };
        visit(scene->root_node, -1);

        skeleton->root = skeleton->Find("root");
        if (skeleton->root < 0)
            for (size_t i = 0; i < skeleton->parents.size(); ++i)
                if (skeleton->parents[i] < 0) { skeleton->root = static_cast<int>(i); break; }
        for (const char* hips : { "pelvis", "Hips", "hips", "mixamorig:Hips", "Pelvis" })
            if ((skeleton->pelvis = skeleton->Find(hips)) >= 0) break;
        return skeleton;
    }
}

bool ImportFbxModel(const std::string& path, std::vector<MeshData>& meshes, ModelAsset& model)
{
    ufbx_scene* scene = LoadScene(path, true);
    if (!scene) return false;

    std::shared_ptr<Skeleton> skeleton = BuildSkeleton(scene);
    model.skeleton = skeleton;
    model.hasAnimations = scene->anim_stacks.count > 0;

    const fs::path modelPath(path);
    const fs::path modelDir = modelPath.parent_path();
    const fs::path materialDir = modelDir / (modelPath.stem().string() + "_Materials");
    std::error_code ec;

    // Materials: generated next to the model the first time (existing files are kept so edits survive).
    std::map<const ufbx_material*, std::string> materialPaths;
    auto materialPath = [&](const ufbx_material* mat) -> std::string {
        if (!mat) return {};
        auto it = materialPaths.find(mat);
        if (it != materialPaths.end()) return it->second;
        const fs::path out = materialDir / (SafeName(mat->name.data, "Material" + std::to_string(materialPaths.size())) + ".mat");
        const std::string rel = Relative(out);
        materialPaths[mat] = rel;
        if (fs::exists(out, ec)) return rel;
        MaterialAsset m;
        const ufbx_material_map& base = mat->pbr.base_color.has_value ? mat->pbr.base_color : mat->fbx.diffuse_color;
        if (base.has_value) m.albedo = glm::vec3(static_cast<float>(base.value_vec3.x), static_cast<float>(base.value_vec3.y),
                                                 static_cast<float>(base.value_vec3.z));
        auto texture = [&](const ufbx_material_map& map) -> std::string {
            if (!map.texture || !map.texture->filename.length) return {};
            fs::path file = map.texture->filename.data;
            if (!fs::exists(file, ec)) file = modelDir / fs::path(map.texture->relative_filename.data);
            if (!fs::exists(file, ec)) file = modelDir / fs::path(map.texture->filename.data).filename();
            return fs::exists(file, ec) ? Relative(file) : std::string();
        };
        m.albedoMap = texture(base);
        m.normalMap = texture(mat->pbr.normal_map.texture ? mat->pbr.normal_map : mat->fbx.normal_map);
        if (!m.albedoMap.empty()) m.albedo = glm::vec3(1.0f);
        if (m.albedo == glm::vec3(0.0f)) m.albedo = glm::vec3(0.8f);
        m.metallic = mat->pbr.metalness.has_value ? static_cast<float>(mat->pbr.metalness.value_real) : 0.0f;
        m.smoothness = mat->pbr.roughness.has_value ? 1.0f - static_cast<float>(mat->pbr.roughness.value_real) : 0.4f;
        m.metallic = std::clamp(m.metallic, 0.0f, 1.0f);
        m.smoothness = std::clamp(m.smoothness, 0.0f, 1.0f);
        fs::create_directories(materialDir, ec);
        m.Save(out.string());
        return rel;
    };

    // Model nodes: a root (the file) with one child per engine mesh.
    ModelNode root;
    root.name = modelPath.stem().string();
    model.nodes.push_back(root);

    std::vector<uint32_t> triangle(64);
    for (size_t ni = 0; ni < scene->nodes.count; ++ni)
    {
        const ufbx_node* node = scene->nodes.data[ni];
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh || mesh->num_triangles == 0) continue;
        triangle.resize(mesh->max_face_triangles * 3);

        const ufbx_skin_deformer* skin = mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : nullptr;
        const bool skinned = skin && skeleton && skin->clusters.count > 0;
        const glm::mat4 geometryToModel = ToGlm(node->geometry_to_world);
        const glm::mat3 normalToModel = glm::transpose(glm::inverse(glm::mat3(geometryToModel)));

        std::vector<int> jointBones;
        std::vector<glm::mat4> inverseBind;
        if (skinned)
            for (size_t c = 0; c < skin->clusters.count; ++c)
            {
                const ufbx_skin_cluster* cluster = skin->clusters.data[c];
                jointBones.push_back(cluster->bone_node ? skeleton->Find(cluster->bone_node->name.data) : 0);
                inverseBind.push_back(ToGlm(cluster->geometry_to_bone));
            }

        // One engine mesh per material part.
        const size_t parts = std::max<size_t>(mesh->material_parts.count, 1);
        for (size_t part = 0; part < parts; ++part)
        {
            MeshData md;
            std::vector<uint32_t> faces;
            if (mesh->material_parts.count)
            {
                const ufbx_mesh_part& mp = mesh->material_parts.data[part];
                faces.assign(mp.face_indices.data, mp.face_indices.data + mp.face_indices.count);
            }
            else
            {
                for (uint32_t f = 0; f < mesh->faces.count; ++f) faces.push_back(f);
            }
            if (faces.empty()) continue;

            std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t> unique; // (position, normal, uv) -> vertex
            for (uint32_t faceIndex : faces)
            {
                const ufbx_face face = mesh->faces.data[faceIndex];
                const uint32_t count = ufbx_triangulate_face(triangle.data(), triangle.size(), mesh, face);
                for (uint32_t k = 0; k < count * 3; ++k)
                {
                    const uint32_t index = triangle[k];
                    const uint32_t vi = mesh->vertex_indices.data[index];
                    const uint32_t nIdx = mesh->vertex_normal.exists ? mesh->vertex_normal.indices.data[index] : 0;
                    const uint32_t uIdx = mesh->vertex_uv.exists ? mesh->vertex_uv.indices.data[index] : 0;
                    const auto key = std::make_tuple(vi, nIdx, uIdx);
                    auto it = unique.find(key);
                    if (it != unique.end())
                    {
                        md.indices.push_back(it->second);
                        continue;
                    }
                    Vertex v{};
                    const ufbx_vec3 p = mesh->vertices.data[vi];
                    v.position = glm::vec3(p.x, p.y, p.z);
                    if (mesh->vertex_normal.exists)
                    {
                        const ufbx_vec3 n = mesh->vertex_normal.values.data[nIdx];
                        v.normal = glm::vec3(n.x, n.y, n.z);
                    }
                    if (mesh->vertex_uv.exists)
                    {
                        const ufbx_vec2 uv = mesh->vertex_uv.values.data[uIdx];
                        v.uv = glm::vec2(uv.x, 1.0f - uv.y);
                    }
                    if (!skinned)
                    {
                        v.position = glm::vec3(geometryToModel * glm::vec4(v.position, 1.0f));
                        v.normal = normalToModel * v.normal;
                    }
                    if (glm::dot(v.normal, v.normal) > 0.0f) v.normal = glm::normalize(v.normal);
                    const uint32_t newIndex = static_cast<uint32_t>(md.vertices.size());
                    md.vertices.push_back(v);
                    if (skinned)
                    {
                        // Keep the 4 strongest influences, renormalized.
                        SkinVertex sv;
                        const ufbx_skin_vertex& weights = skin->vertices.data[vi];
                        std::vector<std::pair<float, uint32_t>> list;
                        for (uint32_t w = 0; w < weights.num_weights; ++w)
                        {
                            const ufbx_skin_weight& sw = skin->weights.data[weights.weight_begin + w];
                            list.push_back({ static_cast<float>(sw.weight), sw.cluster_index });
                        }
                        std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.first > b.first; });
                        float total = 0.0f;
                        for (size_t w = 0; w < 4; ++w)
                        {
                            sv.joints[w] = w < list.size() ? static_cast<uint16_t>(list[w].second) : 0;
                            sv.weights[w] = w < list.size() ? list[w].first : 0.0f;
                            total += sv.weights[w];
                        }
                        if (total > 0.0f)
                            for (float& w : sv.weights) w /= total;
                        else
                            sv.weights[0] = 1.0f;
                        md.skin.push_back(sv);
                    }
                    unique.emplace(key, newIndex);
                    md.indices.push_back(newIndex);
                }
            }
            if (skinned)
            {
                md.jointBones = jointBones;
                md.inverseBind = inverseBind;
                md.skeleton = skeleton;
            }
            const ufbx_material* material = mesh->materials.count ? mesh->materials.data[std::min(part, mesh->materials.count - 1)] : nullptr;
            model.meshMaterials.push_back(materialPath(material));
            ModelNode child;
            child.name = std::string(node->name.length ? node->name.data : "Mesh") + (parts > 1 ? "_" + std::to_string(part) : "");
            child.parent = 0;
            child.meshes.push_back(static_cast<int>(meshes.size()));
            model.nodes.push_back(child);
            meshes.push_back(std::move(md));
        }
    }
    ufbx_free_scene(scene);
    return !meshes.empty();
}

bool LoadFbxClip(const std::string& path, const std::string& take, AnimationClip& clip)
{
    ufbx_scene* scene = LoadScene(path, false);
    if (!scene) return false;
    const ufbx_anim_stack* stack = nullptr;
    for (size_t i = 0; i < scene->anim_stacks.count; ++i)
        if (take.empty() || take == scene->anim_stacks.data[i]->name.data)
        {
            stack = scene->anim_stacks.data[i];
            break;
        }
    std::shared_ptr<Skeleton> skeleton = stack ? BuildSkeleton(scene) : nullptr;
    if (!stack || !skeleton)
    {
        LOG_ERROR("%s has no animation%s", path.c_str(), take.empty() ? "" : (" '" + take + "'").c_str());
        ufbx_free_scene(scene);
        return false;
    }

    clip.name = take.empty() ? fs::path(path).stem().string() : take;
    clip.fps = scene->settings.frames_per_second > 0 ? static_cast<float>(scene->settings.frames_per_second) : 30.0f;
    clip.duration = static_cast<float>(stack->time_end - stack->time_begin);
    clip.frameCount = std::max(2, static_cast<int>(std::round(clip.duration * clip.fps)) + 1);
    const size_t bones = skeleton->names.size();
    clip.tracks.resize(bones);
    for (size_t b = 0; b < bones; ++b)
    {
        clip.tracks[b].bone = skeleton->names[b];
        clip.tracks[b].rest = skeleton->rest[b];
        clip.tracks[b].frames.resize(clip.frameCount);
        clip.trackIndex[skeleton->names[b]] = static_cast<int>(b);
    }

    // Bake local transforms per frame by evaluating the whole scene (includes all FBX pivots/conversions).
    for (int f = 0; f < clip.frameCount; ++f)
    {
        const double time = stack->time_begin + std::min<double>(f / static_cast<double>(clip.fps), clip.duration);
        ufbx_scene* evaluated = ufbx_evaluate_scene(scene, stack->anim, time, nullptr, nullptr);
        if (!evaluated) continue;
        std::vector<glm::mat4> world(bones);
        for (size_t b = 0; b < bones; ++b)
        {
            const ufbx_node* n = ufbx_find_node(evaluated, skeleton->names[b].c_str());
            world[b] = n ? ToGlm(n->node_to_world) : glm::mat4(1.0f);
            const int parent = skeleton->parents[b];
            clip.tracks[b].frames[f] = Decompose(parent >= 0 ? glm::inverse(world[parent]) * world[b] : world[b]);
        }
        ufbx_free_scene(evaluated);
    }

    // Root motion curves (planar position and heading of the root bone, relative to its rest orientation).
    clip.rootTrack = skeleton->root;
    if (clip.rootTrack >= 0)
    {
        const auto& track = clip.tracks[clip.rootTrack];
        const glm::quat restInv = glm::inverse(track.rest.r);
        for (const BoneTransform& x : track.frames)
        {
            clip.rootXZ.push_back(glm::vec2(x.t.x, x.t.z));
            const glm::vec3 forward = (x.r * restInv) * glm::vec3(0.0f, 0.0f, -1.0f);
            clip.rootYaw.push_back(std::atan2(-forward.x, -forward.z));
        }
        // Unwrap the heading so interpolation never jumps by 2*pi.
        for (size_t i = 1; i < clip.rootYaw.size(); ++i)
        {
            float d = clip.rootYaw[i] - clip.rootYaw[i - 1];
            while (d > glm::pi<float>()) { clip.rootYaw[i] -= glm::two_pi<float>(); d -= glm::two_pi<float>(); }
            while (d < -glm::pi<float>()) { clip.rootYaw[i] += glm::two_pi<float>(); d += glm::two_pi<float>(); }
        }
        const float travel = glm::length(clip.rootXZ.back() - clip.rootXZ.front());
        const float turn = std::fabs(clip.rootYaw.back() - clip.rootYaw.front());
        clip.hasRootMotion = travel > 0.01f || turn > 0.01f;
        clip.averageSpeed = clip.duration > 0.0f ? travel / clip.duration : 0.0f;
    }
    ufbx_free_scene(scene);
    return true;
}

std::vector<std::string> ClipLibrary::TakeNames(const std::string& path)
{
    std::vector<std::string> names;
    ufbx_load_opts opts = {};
    opts.ignore_geometry = true;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, nullptr);
    if (!scene) return names;
    for (size_t i = 0; i < scene->anim_stacks.count; ++i) names.push_back(scene->anim_stacks.data[i]->name.data);
    ufbx_free_scene(scene);
    return names;
}
