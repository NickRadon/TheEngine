#pragma once

#include "render/Mesh.h"
#include "render/VulkanContext.h"
#include "scene/Material.h"

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct Texture
{
    GpuImage image;
    uint32_t width = 0, height = 0;
    ImTextureID thumbnail = 0; // created lazily for the asset browser
    bool valid = false;
};

struct GpuMaterial
{
    MaterialAsset data;
    VkDescriptorSet set = VK_NULL_HANDLE;
    bool hasNormalMap = false;
};

// Imported model (glTF or FBX). Every glTF primitive / FBX mesh material part becomes one mesh ("path#index").
struct ModelNode
{
    std::string name;
    int parent = -1;
    glm::vec3 position{ 0.0f };
    glm::quat rotation{ 1, 0, 0, 0 };
    glm::vec3 scale{ 1.0f };
    std::vector<int> meshes;             // indices into ModelAsset::meshMaterials / mesh refs
};

struct ModelAsset
{
    std::vector<ModelNode> nodes;         // parents always precede children
    std::vector<std::string> meshMaterials; // .mat path per mesh ("" = default)
    int meshCount = 0;
    std::shared_ptr<const Skeleton> skeleton; // FBX rigs (skinned meshes); bones are not scene objects
    bool hasAnimations = false;
    bool valid = false;
};

// FBX import (anim/FbxImport.cpp): meshes (skinned or static), materials and the skeleton.
bool ImportFbxModel(const std::string& path, std::vector<MeshData>& meshes, ModelAsset& model);

// Loads and caches GPU resources referenced by path (textures, materials, meshes, models) and reloads
// them when the files change on disk.
class ResourceCache
{
public:
    bool Init(VulkanContext* vk);
    void Shutdown();

    VkDescriptorSetLayout MaterialLayout() const { return m_MaterialLayout; }

    // Mesh reference: built-in primitive name ("Cube") or "Assets/model.glb#3".
    const Mesh* GetMesh(const std::string& ref);
    const GpuMaterial& GetMaterial(const std::string& path); // "" or missing -> default material
    const GpuMaterial& DefaultMaterial() const { return *m_Default; }
    Texture* GetTexture(const std::string& path, bool srgb);
    const ModelAsset* GetModel(const std::string& path);
    ImTextureID Thumbnail(const std::string& texturePath);

    // Drop cached entries whose files changed (called periodically by the editor).
    void CheckForChanges();
    void Invalidate(const std::string& path);
    // Update a cached material's factors in place (no texture changes) after the editor saved it.
    void UpdateMaterialData(const std::string& path, const MaterialAsset& data);

    static bool IsTextureFile(const std::string& path);
    static bool IsModelFile(const std::string& path);

private:
    struct Entry
    {
        std::filesystem::file_time_type stamp{};
    };

    Texture LoadTexture(const std::string& path, bool srgb);
    Texture CreateTexture(const uint8_t* rgba, uint32_t width, uint32_t height, bool srgb);
    void DestroyTexture(Texture& t);
    std::unique_ptr<GpuMaterial> BuildMaterial(const MaterialAsset& data);
    void WriteMaterialSet(GpuMaterial& m);
    bool LoadModel(const std::string& path, ModelAsset& model, std::vector<Mesh>& meshes);
    Mesh Upload(MeshData data);
    static std::filesystem::file_time_type Stamp(const std::string& path);

    VulkanContext* m_Vk = nullptr;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_MaterialLayout = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
    float m_MaxAnisotropy = 1.0f;

    Texture m_White, m_FlatNormal;
    std::unique_ptr<GpuMaterial> m_Default;

    std::unordered_map<std::string, Mesh> m_Builtins;
    struct TextureEntry : Entry { Texture tex; };
    struct MaterialEntry : Entry { std::unique_ptr<GpuMaterial> mat; };
    struct ModelEntry : Entry { ModelAsset model; std::vector<Mesh> meshes; };
    std::unordered_map<std::string, TextureEntry> m_Textures; // key = path + ("|srgb" or "|linear")
    std::unordered_map<std::string, MaterialEntry> m_Materials;
    std::unordered_map<std::string, ModelEntry> m_Models;

    // Resources released only after the GPU is idle (they may still be referenced by in-flight frames).
    std::vector<Texture> m_PendingTextures;
    std::vector<VkDescriptorSet> m_PendingSets;
    std::vector<Mesh> m_PendingMeshes;
    void FlushPending();
};
