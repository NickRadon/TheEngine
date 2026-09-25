#pragma once

#include "render/Mesh.h"
#include "render/Resources.h"
#include "render/VulkanContext.h"
#include "scene/Scene.h"

#include <imgui.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

enum class ShadingMode : int { Shaded = 0, Wireframe, ShadedWireframe, AmbientOcclusion };

struct RenderView
{
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f }; // Vulkan (0..1 depth) projection
    glm::vec3 cameraPos{ 0.0f };
    bool orthographic = false;
    bool drawGrid = false;
    int gridPlane = 0; // 0 = XZ, 1 = XY, 2 = YZ
    float gridOpacity = 1.0f;
    ShadingMode shading = ShadingMode::Shaded;
    bool drawSky = true;
    bool drawOutline = false;
    std::vector<EntityId> selection;
    float time = 0.0f;
    float exposure = 0.8f; // camera exposure before ACES tonemapping
    float nearClip = 0.1f; // must match proj; used to fit shadow cascades
    float farClip = 1000.0f;
    bool shadows = true;
    bool ssao = true;
};

constexpr int kMaxLights = 32;

// Must match struct Light in shaders/common.glsl
struct GpuLight
{
    glm::vec4 positionRange;
    glm::vec4 colorIntensity;
    glm::vec4 directionType;
    glm::vec4 spot;
};

// Must match the uniform block in shaders/common.glsl
struct SceneUBO
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::vec4 cameraPos;
    glm::vec4 sunDir;
    glm::vec4 sunColor;
    glm::vec4 skyTint;
    glm::vec4 groundColor;
    glm::vec4 sunParams;
    glm::vec4 cloudParams;
    glm::vec4 gridParams;
    glm::vec4 ambientZenith;
    glm::vec4 ambientHorizon;
    glm::vec4 ambientGround;
    glm::vec4 sunTransmittance;
    glm::mat4 shadowMatrices[4]; // world -> light clip space, one per cascade
    glm::vec4 cascadeSplits;     // view-space far distance of each cascade
    glm::vec4 cascadeTexel;      // world-space size of one shadow texel per cascade
    glm::vec4 shadowParams;      // x = strength, y = enabled
    glm::vec4 aoParams;          // x = enabled, y = intensity, z = radius, w = direct strength
    glm::vec4 viewportSize;      // xy = size, zw = 1 / size
    glm::ivec4 lightCount;
    GpuLight lights[kMaxLights];
};

class SceneRenderer
{
public:
    enum ViewId { SceneViewId = 0, GameViewId = 1, PreviewViewId = 2, ViewCount };

    bool Init(VulkanContext* vk, ResourceCache* resources);
    void Shutdown();

    // Recreates the view's render targets if the size changed. Call before Texture().
    void EnsureSize(ViewId view, uint32_t width, uint32_t height);
    ImTextureID Texture(ViewId view) const { return m_Targets[view].imguiTexture; }
    bool HasTarget(ViewId view) const { return m_Targets[view].width > 0; }

    void Render(VkCommandBuffer cmd, ViewId view, const Scene& scene, const RenderView& rv);

    // Reads the view's final (tonemapped) image back from the GPU and writes it as a 24-bit BMP.
    bool CaptureView(ViewId view, const std::string& path);

    uint32_t DrawCalls() const { return m_DrawCalls; }
    VkSampleCountFlagBits MsaaSamples() const { return m_Samples; }
    uint32_t Triangles() const { return m_Triangles; }
    void ResetStats() { m_DrawCalls = 0; m_Triangles = 0; }

    // Direction towards the sun derived from the first active directional light.
    static bool FindSun(const Scene& scene, glm::vec3& dirToSun, glm::vec3& color, float& intensity, EntityId* sunEntity = nullptr);
    static void ComputeAmbient(const SkySettings& sky, const glm::vec3& sunDir, float sunIntensity, SceneUBO& ubo);

private:
    struct Target
    {
        uint32_t width = 0, height = 0;
        GpuImage hdrMsaa;             // multisampled color (only when MSAA is on), resolved into hdr
        GpuImage hdr, depth, mask, output;
        GpuImage prepassDepth, normals, aoRaw, ao; // SSAO inputs / outputs (single sample)
        VkDescriptorSet compositeSet = VK_NULL_HANDLE;
        VkDescriptorSet ssaoSet = VK_NULL_HANDLE; // depth + normals
        VkDescriptorSet blurSet = VK_NULL_HANDLE; // raw ao + depth
        ImTextureID imguiTexture = 0;
        GpuBuffer ubo[VulkanContext::kFramesInFlight];
        VkDescriptorSet uboSet[VulkanContext::kFramesInFlight] = {};
    };

    struct PushConstants
    {
        glm::mat4 model;
        glm::vec4 color;
        glm::vec4 params;   // x metallic, y smoothness, z unlit, w cascade
        glm::vec4 extra;    // xy tiling, z normal strength, w has normal map
        glm::vec4 emission;
    };
    static_assert(sizeof(PushConstants) == 128, "push constants must fit the guaranteed 128 bytes");

    enum class DrawMode { Lit, Unlit, DepthOnly };

    void CreatePipelines();
    VkPipeline CreatePipeline(VkShaderModule vert, VkShaderModule frag, VkPipelineLayout layout,
                              VkFormat colorFormat, VkFormat depthFormat, bool vertexInput,
                              bool depthTest, bool depthWrite, bool blend, VkPolygonMode polygon,
                              VkCullModeFlags cull, float depthBias, VkSampleCountFlagBits samples);
    void DestroyTarget(Target& t);
    void WriteImageSet(VkDescriptorSet set, VkImageView a, VkSampler sa, VkImageView b, VkSampler sb);
    void ComputeCascades(const RenderView& rv, const glm::vec3& sunDir, float shadowDistance, SceneUBO& ubo) const;
    void RenderShadows(VkCommandBuffer cmd, const Scene& scene, VkDescriptorSet uboSet);
    void RenderSsao(VkCommandBuffer cmd, Target& t, const Scene& scene, VkDescriptorSet uboSet);
    void DrawEntity(VkCommandBuffer cmd, const Scene& scene, const Entity& e, DrawMode mode,
                    const glm::vec4& overrideColor = glm::vec4(1.0f), float cascade = 0.0f);

    VulkanContext* m_Vk = nullptr;
    ResourceCache* m_Res = nullptr;
    Target m_Targets[ViewCount];

    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_UboLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_CompositeLayout = VK_NULL_HANDLE; // two samplers (also used by SSAO passes)
    VkPipelineLayout m_SceneLayout = VK_NULL_HANDLE;          // set 0 scene, set 1 material
    VkPipelineLayout m_PostLayout = VK_NULL_HANDLE;           // set 0 scene, set 1 two textures
    VkPipelineLayout m_CompositeLayoutPipe = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
    VkSampler m_PointSampler = VK_NULL_HANDLE;

    VkPipeline m_SkyPipeline = VK_NULL_HANDLE;
    VkPipeline m_MeshPipeline = VK_NULL_HANDLE;
    VkPipeline m_WirePipeline = VK_NULL_HANDLE;
    VkPipeline m_GridPipeline = VK_NULL_HANDLE;
    VkPipeline m_MaskPipeline = VK_NULL_HANDLE;
    VkPipeline m_CompositePipeline = VK_NULL_HANDLE;
    VkPipeline m_ShadowPipeline = VK_NULL_HANDLE;
    VkPipeline m_NormalsPipeline = VK_NULL_HANDLE;
    VkPipeline m_SsaoPipeline = VK_NULL_HANDLE;
    VkPipeline m_BlurPipeline = VK_NULL_HANDLE;

    // Cascaded shadow map shared by all views (views render sequentially in one command buffer).
    static constexpr uint32_t kCascades = 4;
    static constexpr uint32_t kShadowSize = 2048;
    GpuImage m_ShadowMap;
    VkImageView m_ShadowLayerViews[kCascades] = {};
    VkSampler m_ShadowSampler = VK_NULL_HANDLE;

    VkSampleCountFlagBits m_Samples = VK_SAMPLE_COUNT_1_BIT;

    uint32_t m_DrawCalls = 0;
    uint32_t m_Triangles = 0;

    static constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
    static constexpr VkFormat kMaskFormat = VK_FORMAT_R8_UNORM;
    static constexpr VkFormat kOutputFormat = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat kShadowFormat = VK_FORMAT_D32_SFLOAT;
    static constexpr VkFormat kNormalFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kAoFormat = VK_FORMAT_R8_UNORM;
};
