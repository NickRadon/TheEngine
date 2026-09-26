#include "render/Resources.h"

#include "core/Log.h"

#include <imgui_impl_vulkan.h>
#include <cgltf.h>
#include <stb_image.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
    std::string Lower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Project-relative, forward-slash path (how assets are referenced in scenes and materials).
    std::string RelativeAssetPath(const fs::path& p)
    {
        std::error_code ec;
        fs::path rel = fs::relative(p, fs::current_path(), ec);
        return (ec || rel.empty() ? p : rel).generic_string();
    }

    std::string SafeFileName(std::string s, const std::string& fallback)
    {
        for (char& c : s)
            if (std::string("\\/:*?\"<>|").find(c) != std::string::npos) c = '_';
        return s.empty() ? fallback : s;
    }

    void MipBarrier(VkCommandBuffer cmd, VkImage image, uint32_t mip, VkImageLayout from, VkImageLayout to,
                    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
    {
        VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        b.srcStageMask = srcStage;
        b.srcAccessMask = srcAccess;
        b.dstStageMask = dstStage;
        b.dstAccessMask = dstAccess;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1 };
        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    }
}

bool ResourceCache::IsTextureFile(const std::string& path)
{
    const std::string ext = Lower(fs::path(path).extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".psd" || ext == ".gif";
}

bool ResourceCache::IsModelFile(const std::string& path)
{
    const std::string ext = Lower(fs::path(path).extension().string());
    return ext == ".gltf" || ext == ".glb" || ext == ".fbx";
}

fs::file_time_type ResourceCache::Stamp(const std::string& path)
{
    std::error_code ec;
    auto t = fs::last_write_time(path, ec);
    return ec ? fs::file_time_type{} : t;
}

bool ResourceCache::Init(VulkanContext* vk)
{
    m_Vk = vk;
    VkDevice device = vk->Device();

    VkDescriptorSetLayoutBinding bindings[3];
    for (uint32_t i = 0; i < 3; ++i)
        bindings[i] = { i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo layout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layout.bindingCount = 3;
    layout.pBindings = bindings;
    vkCreateDescriptorSetLayout(device, &layout, nullptr, &m_MaterialLayout);

    VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * 1024 };
    VkDescriptorPoolCreateInfo pool{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = 1024;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    vkCreateDescriptorPool(device, &pool, nullptr, &m_Pool);

    m_MaxAnisotropy = vk->MaxAnisotropy();
    VkSamplerCreateInfo sampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.anisotropyEnable = m_MaxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    sampler.maxAnisotropy = m_MaxAnisotropy;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    vkCreateSampler(device, &sampler, nullptr, &m_Sampler);

    const uint8_t white[4] = { 255, 255, 255, 255 };
    const uint8_t flat[4] = { 128, 128, 255, 255 };
    m_White = CreateTexture(white, 1, 1, false);
    m_FlatNormal = CreateTexture(flat, 1, 1, false);
    m_Default = BuildMaterial(MaterialAsset{});

    for (int i = 1; i < static_cast<int>(PrimitiveType::Count); ++i)
    {
        auto type = static_cast<PrimitiveType>(i);
        m_Builtins[PrimitiveName(type)] = Upload(GeneratePrimitive(type));
    }
    return true;
}

void ResourceCache::Shutdown()
{
    vkDeviceWaitIdle(m_Vk->Device());
    for (auto& [k, e] : m_Textures) DestroyTexture(e.tex);
    for (auto& [k, e] : m_Models)
        for (Mesh& m : e.meshes) { m_Vk->DestroyBuffer(m.vertexBuffer); m_Vk->DestroyBuffer(m.indexBuffer); m_Vk->DestroyBuffer(m.skinBuffer); }
    for (auto& [k, m] : m_Builtins) { m_Vk->DestroyBuffer(m.vertexBuffer); m_Vk->DestroyBuffer(m.indexBuffer); m_Vk->DestroyBuffer(m.skinBuffer); }
    FlushPending();
    m_Textures.clear();
    m_Materials.clear();
    m_Models.clear();
    DestroyTexture(m_White);
    DestroyTexture(m_FlatNormal);
    vkDestroySampler(m_Vk->Device(), m_Sampler, nullptr);
    vkDestroyDescriptorPool(m_Vk->Device(), m_Pool, nullptr);
    vkDestroyDescriptorSetLayout(m_Vk->Device(), m_MaterialLayout, nullptr);
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------
Texture ResourceCache::CreateTexture(const uint8_t* rgba, uint32_t width, uint32_t height, bool srgb)
{
    Texture t;
    t.width = width;
    t.height = height;
    const uint32_t mips = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    const VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    t.image = m_Vk->CreateImage(width, height, format,
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT, 1, mips);

    const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4;
    GpuBuffer staging = m_Vk->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(staging.mapped, rgba, size);

    m_Vk->ImmediateSubmit([&](VkCommandBuffer cmd) {
        const auto transfer = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        for (uint32_t m = 0; m < mips; ++m)
            MipBarrier(cmd, t.image.image, m, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, transfer, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { width, height, 1 };
        vkCmdCopyBufferToImage(cmd, staging.buffer, t.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        // Build the mip chain by repeatedly blitting the previous level.
        int32_t w = static_cast<int32_t>(width), h = static_cast<int32_t>(height);
        for (uint32_t m = 1; m < mips; ++m)
        {
            MipBarrier(cmd, t.image.image, m - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       transfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, transfer, VK_ACCESS_2_TRANSFER_READ_BIT);
            VkImageBlit blit{};
            blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, 1 };
            blit.srcOffsets[1] = { w, h, 1 };
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
            blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1 };
            blit.dstOffsets[1] = { w, h, 1 };
            vkCmdBlitImage(cmd, t.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            MipBarrier(cmd, t.image.image, m - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       transfer, 0, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        }
        MipBarrier(cmd, t.image.image, mips - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   transfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    });
    m_Vk->DestroyBuffer(staging);
    t.valid = true;
    return t;
}

void ResourceCache::DestroyTexture(Texture& t)
{
    if (t.thumbnail) ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(t.thumbnail));
    m_Vk->DestroyImage(t.image);
    t = {};
}

Texture ResourceCache::LoadTexture(const std::string& path, bool srgb)
{
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels)
    {
        LOG_ERROR("Failed to load texture %s: %s", path.c_str(), stbi_failure_reason());
        return {};
    }
    Texture t = CreateTexture(pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h), srgb);
    stbi_image_free(pixels);
    return t;
}

Texture* ResourceCache::GetTexture(const std::string& path, bool srgb)
{
    if (path.empty()) return nullptr;
    const std::string key = path + (srgb ? "|srgb" : "|linear");
    auto it = m_Textures.find(key);
    if (it != m_Textures.end()) return it->second.tex.valid ? &it->second.tex : nullptr;
    TextureEntry entry;
    entry.stamp = Stamp(path);
    entry.tex = LoadTexture(path, srgb);
    auto& stored = m_Textures[key] = std::move(entry);
    return stored.tex.valid ? &stored.tex : nullptr;
}

ImTextureID ResourceCache::Thumbnail(const std::string& path)
{
    Texture* t = GetTexture(path, true);
    if (!t) return 0;
    if (!t->thumbnail)
        t->thumbnail = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(t->image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    return t->thumbnail;
}

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------
std::unique_ptr<GpuMaterial> ResourceCache::BuildMaterial(const MaterialAsset& data)
{
    auto m = std::make_unique<GpuMaterial>();
    m->data = data;
    VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    alloc.descriptorPool = m_Pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &m_MaterialLayout;
    if (vkAllocateDescriptorSets(m_Vk->Device(), &alloc, &m->set) != VK_SUCCESS)
    {
        LOG_ERROR("Out of material descriptor sets");
        return m;
    }
    WriteMaterialSet(*m);
    return m;
}

void ResourceCache::WriteMaterialSet(GpuMaterial& m)
{
    Texture* albedo = GetTexture(m.data.albedoMap, true);
    Texture* normal = GetTexture(m.data.normalMap, false);
    Texture* mask = GetTexture(m.data.maskMap, false);
    m.hasNormalMap = normal != nullptr;
    VkDescriptorImageInfo infos[3] = {
        { m_Sampler, (albedo ? albedo : &m_White)->image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { m_Sampler, (normal ? normal : &m_FlatNormal)->image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { m_Sampler, (mask ? mask : &m_White)->image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };
    VkWriteDescriptorSet writes[3] = {};
    for (uint32_t i = 0; i < 3; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m.set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(m_Vk->Device(), 3, writes, 0, nullptr);
}

const GpuMaterial& ResourceCache::GetMaterial(const std::string& path)
{
    if (path.empty()) return *m_Default;
    auto it = m_Materials.find(path);
    if (it != m_Materials.end()) return it->second.mat ? *it->second.mat : *m_Default;

    MaterialEntry entry;
    entry.stamp = Stamp(path);
    MaterialAsset data;
    if (data.Load(path)) entry.mat = BuildMaterial(data);
    else LOG_ERROR("Failed to load material %s", path.c_str());
    auto& stored = m_Materials[path] = std::move(entry);
    return stored.mat ? *stored.mat : *m_Default;
}

// ---------------------------------------------------------------------------
// Meshes & models
// ---------------------------------------------------------------------------
Mesh ResourceCache::Upload(MeshData data)
{
    Mesh mesh;
    if (!data.vertices.empty())
    {
        data.boundsMin = data.boundsMax = data.vertices[0].position;
        for (const Vertex& v : data.vertices)
        {
            data.boundsMin = glm::min(data.boundsMin, v.position);
            data.boundsMax = glm::max(data.boundsMax, v.position);
        }
    }
    if (data.vertices.empty() || data.indices.empty()) return mesh;
    mesh.vertexBuffer = m_Vk->CreateDeviceBuffer(data.vertices.data(), data.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    mesh.indexBuffer = m_Vk->CreateDeviceBuffer(data.indices.data(), data.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    mesh.indexCount = static_cast<uint32_t>(data.indices.size());
    if (data.Skinned())
        mesh.skinBuffer = m_Vk->CreateDeviceBuffer(data.skin.data(), data.skin.size() * sizeof(SkinVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    mesh.data = std::move(data);
    return mesh;
}

const Mesh* ResourceCache::GetMesh(const std::string& ref)
{
    const size_t hash = ref.rfind('#');
    if (hash == std::string::npos)
    {
        auto it = m_Builtins.find(ref);
        return it != m_Builtins.end() ? &it->second : nullptr;
    }
    const std::string path = ref.substr(0, hash);
    const int index = std::atoi(ref.c_str() + hash + 1);
    if (!GetModel(path)) return nullptr;
    auto& meshes = m_Models[path].meshes;
    return index >= 0 && index < static_cast<int>(meshes.size()) ? &meshes[index] : nullptr;
}

const ModelAsset* ResourceCache::GetModel(const std::string& path)
{
    auto it = m_Models.find(path);
    if (it != m_Models.end()) return it->second.model.valid ? &it->second.model : nullptr;
    ModelEntry entry;
    entry.stamp = Stamp(path);
    if (!LoadModel(path, entry.model, entry.meshes)) entry.model.valid = false;
    auto& stored = m_Models[path] = std::move(entry);
    return stored.model.valid ? &stored.model : nullptr;
}

bool ResourceCache::LoadModel(const std::string& path, ModelAsset& model, std::vector<Mesh>& meshes)
{
    if (Lower(fs::path(path).extension().string()) == ".fbx")
    {
        std::vector<MeshData> imported;
        if (!ImportFbxModel(path, imported, model)) return false;
        for (MeshData& md : imported) meshes.push_back(Upload(std::move(md)));
        model.meshCount = static_cast<int>(meshes.size());
        model.valid = model.meshCount > 0;
        if (model.valid)
            LOG_INFO("Imported %s (%d meshes, %d bones)", path.c_str(), model.meshCount, model.skeleton ? static_cast<int>(model.skeleton->names.size()) : 0);
        return model.valid;
    }
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
    {
        LOG_ERROR("Failed to parse glTF %s", path.c_str());
        return false;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success)
    {
        LOG_ERROR("Failed to load glTF buffers for %s", path.c_str());
        cgltf_free(data);
        return false;
    }

    const fs::path modelPath(path);
    const fs::path modelDir = modelPath.parent_path();
    const std::string stem = modelPath.stem().string();
    const fs::path materialDir = modelDir / (stem + "_Materials");
    const fs::path textureDir = modelDir / (stem + "_Textures");
    std::error_code ec;

    // Textures: external files are referenced in place; embedded images are extracted once.
    auto texturePath = [&](const cgltf_texture* tex) -> std::string {
        if (!tex || !tex->image) return {};
        const cgltf_image* img = tex->image;
        const size_t imageIndex = static_cast<size_t>(img - data->images);
        if (img->uri && std::strncmp(img->uri, "data:", 5) != 0)
        {
            std::string uri = img->uri;
            cgltf_decode_uri(uri.data());
            uri.resize(std::strlen(uri.c_str()));
            return RelativeAssetPath(modelDir / uri);
        }
        if (img->buffer_view)
        {
            const std::string mime = img->mime_type ? img->mime_type : "";
            const std::string ext = mime == "image/jpeg" ? ".jpg" : ".png";
            const std::string name = SafeFileName(img->name ? img->name : "", "image" + std::to_string(imageIndex));
            const fs::path out = textureDir / (name + ext);
            if (!fs::exists(out, ec))
            {
                fs::create_directories(textureDir, ec);
                const uint8_t* bytes = static_cast<const uint8_t*>(img->buffer_view->buffer->data) + img->buffer_view->offset;
                std::ofstream file(out, std::ios::binary);
                file.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(img->buffer_view->size));
            }
            return RelativeAssetPath(out);
        }
        return {};
    };

    // Materials: generated next to the model the first time (existing files are kept so edits survive).
    std::vector<std::string> materialPaths(data->materials_count);
    for (size_t i = 0; i < data->materials_count; ++i)
    {
        const cgltf_material& gm = data->materials[i];
        const fs::path out = materialDir / (SafeFileName(gm.name ? gm.name : "", "Material" + std::to_string(i)) + ".mat");
        materialPaths[i] = RelativeAssetPath(out);
        if (fs::exists(out, ec)) continue;
        MaterialAsset m;
        if (gm.has_pbr_metallic_roughness)
        {
            const auto& pbr = gm.pbr_metallic_roughness;
            m.albedo = glm::make_vec3(pbr.base_color_factor);
            m.metallic = pbr.metallic_factor;
            m.smoothness = 1.0f - pbr.roughness_factor;
            m.albedoMap = texturePath(pbr.base_color_texture.texture);
            m.maskMap = texturePath(pbr.metallic_roughness_texture.texture);
            if (pbr.base_color_texture.has_transform)
                m.tiling = glm::make_vec2(pbr.base_color_texture.transform.scale);
        }
        m.normalMap = texturePath(gm.normal_texture.texture);
        if (gm.normal_texture.texture) m.normalStrength = gm.normal_texture.scale;
        m.emission = glm::make_vec3(gm.emissive_factor);
        fs::create_directories(materialDir, ec);
        m.Save(out.string());
    }

    // Meshes: one engine mesh per glTF primitive.
    std::vector<std::pair<int, int>> meshRanges(data->meshes_count); // first primitive index, count
    for (size_t mi = 0; mi < data->meshes_count; ++mi)
    {
        const cgltf_mesh& gmesh = data->meshes[mi];
        meshRanges[mi] = { static_cast<int>(meshes.size()), 0 };
        for (size_t pi = 0; pi < gmesh.primitives_count; ++pi)
        {
            const cgltf_primitive& prim = gmesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            MeshData md;
            const cgltf_accessor* pos = nullptr, *nrm = nullptr, *uv = nullptr;
            for (size_t a = 0; a < prim.attributes_count; ++a)
            {
                const cgltf_attribute& attr = prim.attributes[a];
                if (attr.type == cgltf_attribute_type_position) pos = attr.data;
                else if (attr.type == cgltf_attribute_type_normal) nrm = attr.data;
                else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uv = attr.data;
            }
            if (!pos) continue;
            md.vertices.resize(pos->count);
            for (size_t v = 0; v < pos->count; ++v)
            {
                Vertex& vert = md.vertices[v];
                vert = {};
                cgltf_accessor_read_float(pos, v, glm::value_ptr(vert.position), 3);
                if (nrm) cgltf_accessor_read_float(nrm, v, glm::value_ptr(vert.normal), 3);
                if (uv) cgltf_accessor_read_float(uv, v, glm::value_ptr(vert.uv), 2);
            }
            if (prim.indices)
            {
                md.indices.resize(prim.indices->count);
                for (size_t k = 0; k < prim.indices->count; ++k)
                    md.indices[k] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, k));
            }
            else
            {
                md.indices.resize(pos->count);
                for (size_t k = 0; k < pos->count; ++k) md.indices[k] = static_cast<uint32_t>(k);
            }
            if (!nrm)
            {
                // Smooth normals from face normals.
                for (size_t k = 0; k + 2 < md.indices.size(); k += 3)
                {
                    Vertex& a = md.vertices[md.indices[k]];
                    Vertex& b = md.vertices[md.indices[k + 1]];
                    Vertex& c = md.vertices[md.indices[k + 2]];
                    glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
                    a.normal += n; b.normal += n; c.normal += n;
                }
                for (Vertex& v : md.vertices) v.normal = glm::length(v.normal) > 0 ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
            }
            meshes.push_back(Upload(std::move(md)));
            model.meshMaterials.push_back(prim.material ? materialPaths[prim.material - data->materials] : std::string());
            meshRanges[mi].second++;
        }
    }
    model.meshCount = static_cast<int>(meshes.size());

    // Node hierarchy of the default scene.
    std::vector<const cgltf_node*> roots;
    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if (scene)
        for (size_t i = 0; i < scene->nodes_count; ++i) roots.push_back(scene->nodes[i]);
    else
        for (size_t i = 0; i < data->nodes_count; ++i)
            if (!data->nodes[i].parent) roots.push_back(&data->nodes[i]);

    std::vector<std::pair<const cgltf_node*, int>> stack;
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back({ *it, -1 });
    while (!stack.empty())
    {
        auto [node, parent] = stack.back();
        stack.pop_back();
        ModelNode mn;
        mn.name = node->name ? node->name : (node->mesh && node->mesh->name ? node->mesh->name : "Node");
        mn.parent = parent;
        if (node->has_matrix)
        {
            glm::mat4 m = glm::make_mat4(node->matrix);
            glm::vec3 skew;
            glm::vec4 persp;
            glm::decompose(m, mn.scale, mn.rotation, mn.position, skew, persp);
        }
        else
        {
            if (node->has_translation) mn.position = glm::make_vec3(node->translation);
            if (node->has_rotation) mn.rotation = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
            if (node->has_scale) mn.scale = glm::make_vec3(node->scale);
        }
        if (node->mesh)
        {
            auto range = meshRanges[node->mesh - data->meshes];
            for (int k = 0; k < range.second; ++k) mn.meshes.push_back(range.first + k);
        }
        const int index = static_cast<int>(model.nodes.size());
        model.nodes.push_back(mn);
        for (size_t c = node->children_count; c-- > 0;) stack.push_back({ node->children[c], index });
    }

    cgltf_free(data);
    model.valid = model.meshCount > 0;
    if (model.valid) LOG_INFO("Imported %s (%d meshes, %d nodes)", path.c_str(), model.meshCount, static_cast<int>(model.nodes.size()));
    return model.valid;
}

// ---------------------------------------------------------------------------
// Hot reload
// ---------------------------------------------------------------------------
void ResourceCache::Invalidate(const std::string& path)
{
    bool textureChanged = false;
    for (auto it = m_Textures.begin(); it != m_Textures.end();)
    {
        if (it->first.rfind(path + "|", 0) == 0)
        {
            m_PendingTextures.push_back(it->second.tex);
            it = m_Textures.erase(it);
            textureChanged = true;
        }
        else ++it;
    }
    // Materials hold texture views, so any texture change rebuilds them all.
    for (auto it = m_Materials.begin(); it != m_Materials.end();)
    {
        if (textureChanged || it->first == path)
        {
            if (it->second.mat) m_PendingSets.push_back(it->second.mat->set);
            it = m_Materials.erase(it);
        }
        else ++it;
    }
    auto model = m_Models.find(path);
    if (model != m_Models.end())
    {
        for (Mesh& m : model->second.meshes) m_PendingMeshes.push_back(std::move(m));
        m_Models.erase(model);
    }
}

void ResourceCache::CheckForChanges()
{
    std::vector<std::string> changed;
    for (auto& [key, e] : m_Textures)
    {
        const std::string path = key.substr(0, key.rfind('|'));
        if (Stamp(path) != e.stamp) changed.push_back(path);
    }
    for (auto& [path, e] : m_Materials)
        if (Stamp(path) != e.stamp) changed.push_back(path);
    for (auto& [path, e] : m_Models)
        if (Stamp(path) != e.stamp) changed.push_back(path);
    for (const std::string& path : changed)
    {
        LOG_INFO("Reloading %s", path.c_str());
        Invalidate(path);
    }
    FlushPending();
}

void ResourceCache::FlushPending()
{
    if (m_PendingTextures.empty() && m_PendingSets.empty() && m_PendingMeshes.empty()) return;
    vkDeviceWaitIdle(m_Vk->Device());
    for (Texture& t : m_PendingTextures) DestroyTexture(t);
    if (!m_PendingSets.empty())
        vkFreeDescriptorSets(m_Vk->Device(), m_Pool, static_cast<uint32_t>(m_PendingSets.size()), m_PendingSets.data());
    for (Mesh& m : m_PendingMeshes) { m_Vk->DestroyBuffer(m.vertexBuffer); m_Vk->DestroyBuffer(m.indexBuffer); m_Vk->DestroyBuffer(m.skinBuffer); }
    m_PendingTextures.clear();
    m_PendingSets.clear();
    m_PendingMeshes.clear();
}

void ResourceCache::UpdateMaterialData(const std::string& path, const MaterialAsset& data)
{
    auto it = m_Materials.find(path);
    if (it == m_Materials.end() || !it->second.mat) return;
    it->second.mat->data = data;
    it->second.stamp = Stamp(path); // our own save must not trigger a reload
}
