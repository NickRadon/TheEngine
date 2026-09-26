#pragma once

#include "render/Mesh.h"
#include "render/Resources.h"
#include "render/VulkanContext.h"
#include "scene/Scene.h"

#include <imgui.h>

#include <glm/glm.hpp>

#include <functional>
#include <map>
#include <unordered_map>
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
    bool postProcessing = true; // volumes (bloom, color grading, vignette, tonemapping)
};

constexpr int kMaxLights = 32;
constexpr int kMaxLocalShadowLayers = 24; // spot light = 1 layer, point light = 6 (cube faces)
constexpr int kMaxProbes = 8;

// Must match struct Light in shaders/common.glsl
struct GpuLight
{
    glm::vec4 positionRange;
    glm::vec4 colorIntensity;
    glm::vec4 directionType;
    glm::vec4 spot; // x/y = cone cosines, z = first shadow layer (-1 = none), w = shadow strength
};

// Must match struct Probe in shaders/common.glsl
struct GpuProbe
{
    glm::vec4 centerIndex;
    glm::vec4 boxMin;
    glm::vec4 boxMax;
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
    glm::mat4 localShadowMatrices[kMaxLocalShadowLayers];
    glm::vec4 envParams;         // x = env mip count, y = sky reflection intensity, z = probe count
    GpuProbe probes[kMaxProbes];
};

class SceneRenderer
{
public:
    enum ViewId
    {
        SceneViewId = 0, GameViewId = 1, PreviewViewId = 2,
        ProbeFace0, ProbeFace1, ProbeFace2, ProbeFace3, ProbeFace4, ProbeFace5, // reflection probe capture
        ViewCount
    };

    bool Init(VulkanContext* vk, ResourceCache* resources);
    void Shutdown();

    // Recreates the view's render targets if the size changed. Call before Texture().
    void EnsureSize(ViewId view, uint32_t width, uint32_t height);
    ImTextureID Texture(ViewId view) const { return m_Targets[view].imguiTexture; }
    bool HasTarget(ViewId view) const { return m_Targets[view].width > 0; }

    // Once per frame before the views: refreshes the sky reflection cube map when the sky changed.
    void UpdateEnvironment(VkCommandBuffer cmd, const Scene& scene, float time);
    // Captures a reflection probe (6 scene renders) and prefilters it. Returns false if all probe slots are taken.
    bool BakeProbe(VkCommandBuffer cmd, const Scene& scene, EntityId probe, float time);
    bool IsProbeBaked(EntityId probe) const { return m_ProbeSlots.count(probe) != 0; }
    void ResetProbes() { m_ProbeSlots.clear(); }
    // Changes whenever the sky/sun lighting changes (not for moving clouds); probes re-bake when it does.
    uint32_t LightingVersion() const { return m_LightingVersion; }

    void Render(VkCommandBuffer cmd, ViewId view, const Scene& scene, const RenderView& rv);

    // Reads the view's final (tonemapped) image back from the GPU and writes it as a 24-bit BMP.
    bool CaptureView(ViewId view, const std::string& path);

    uint32_t DrawCalls() const { return m_DrawCalls; }
    VkSampleCountFlagBits MsaaSamples() const { return m_Samples; }
    uint32_t Triangles() const { return m_Triangles; }
    int ShadowedLocalLights() const { return m_LocalShadowCount; }
    // Once per frame before rendering: resets statistics and the skinning buffer.
    void ResetStats();

    // Joint matrices for skinned mesh entities (provided by the AnimationSystem).
    using PaletteProvider = std::function<const std::vector<glm::mat4>*(EntityId)>;
    void SetPaletteProvider(PaletteProvider provider) { m_Palettes = std::move(provider); }

    // Direction towards the sun derived from the first active directional light.
    static bool FindSun(const Scene& scene, glm::vec3& dirToSun, glm::vec3& color, float& intensity, EntityId* sunEntity = nullptr);
    static void ComputeAmbient(const SkySettings& sky, const glm::vec3& sunDir, float sunIntensity, SceneUBO& ubo);

private:
    static constexpr uint32_t kBloomMips = 6;

    struct Target
    {
        uint32_t width = 0, height = 0;
        GpuImage hdrMsaa;             // multisampled color (only when MSAA is on), resolved into hdr
        GpuImage hdr, depth, mask, output;
        GpuImage prepassDepth, normals, aoRaw, ao; // SSAO inputs / outputs (single sample)
        GpuImage bloom;                            // half resolution mip chain
        uint32_t bloomMips = 0;
        VkImageView bloomViews[kBloomMips] = {};
        VkDescriptorSet bloomDownSets[kBloomMips] = {}; // down[i] reads hdr (i = 0) or mip i-1
        VkDescriptorSet bloomUpSets[kBloomMips] = {};   // up[i] reads mip i + 1
        VkDescriptorSet compositeSet = VK_NULL_HANDLE;
        VkDescriptorSet ssaoSet = VK_NULL_HANDLE; // depth + normals
        VkDescriptorSet blurSet = VK_NULL_HANDLE; // raw ao + depth
        VkDescriptorSet remapSet = VK_NULL_HANDLE; // probe faces: hdr, for the cube remap
        ImTextureID imguiTexture = 0;
        GpuBuffer ubo[VulkanContext::kFramesInFlight];
        VkDescriptorSet uboSet[VulkanContext::kFramesInFlight] = {};
    };

    struct PushConstants
    {
        glm::mat4 model;
        glm::vec4 color;
        glm::vec4 params;   // x metallic, y smoothness, z unlit, w shadow layer
        glm::vec4 extra;    // xy tiling, z normal strength, w has normal map
        glm::vec4 emission;
    };
    static_assert(sizeof(PushConstants) == 128, "push constants must fit the guaranteed 128 bytes");

    // Final composite (post-processing) parameters; must match composite.frag.
    struct CompositePush
    {
        glm::vec4 outlineColor;
        glm::vec4 params;
        glm::vec4 bloomTint;
        glm::vec4 grading;
        glm::vec4 colorFilter;
        glm::vec4 balance;
        glm::vec4 vignette;
        glm::vec4 vignetteColor;
    };
    static_assert(sizeof(CompositePush) == 128, "composite push constants must fit 128 bytes");

    enum class DrawMode { Lit, Unlit, DepthOnly };

    void CreatePipelines();
    VkPipeline CreatePipeline(VkShaderModule vert, VkShaderModule frag, VkPipelineLayout layout,
                              VkFormat colorFormat, VkFormat depthFormat, bool vertexInput,
                              bool depthTest, bool depthWrite, bool blend, VkPolygonMode polygon,
                              VkCullModeFlags cull, float depthBias, VkSampleCountFlagBits samples, bool additive = false,
                              bool skinned = false);
    // Selects the pipelines used by DrawEntity for static and skinned meshes (binds the static one).
    void UsePipelines(VkCommandBuffer cmd, VkPipeline normal, VkPipeline skinned);
    uint32_t UploadPalette(EntityId id, const std::vector<glm::mat4>& palette);
    void DestroyTarget(Target& t);
    VkDescriptorSet Allocate(VkDescriptorSetLayout layout);
    void WriteImageSet(VkDescriptorSet set, VkImageView a, VkSampler sa, VkImageView b, VkSampler sb);
    void WriteImage(VkDescriptorSet set, uint32_t binding, VkImageView view, VkSampler sampler);
    void FillSkyUniforms(const Scene& scene, float time, SceneUBO& ubo, glm::vec3& sunDir, float& sunIntensity, EntityId& sunId) const;
    void ComputeCascades(const RenderView& rv, const glm::vec3& sunDir, float shadowDistance, SceneUBO& ubo) const;
    void RenderShadows(VkCommandBuffer cmd, const Scene& scene, VkDescriptorSet uboSet);
    void RenderLocalShadows(VkCommandBuffer cmd, const Scene& scene, VkDescriptorSet uboSet, int layers);
    void RenderSsao(VkCommandBuffer cmd, Target& t, const Scene& scene, VkDescriptorSet uboSet);
    void RenderBloom(VkCommandBuffer cmd, Target& t, const PostProcessSettings& post);
    void FullscreenPass(VkCommandBuffer cmd, VkImageView target, uint32_t width, uint32_t height, VkPipeline pipeline,
                        VkPipelineLayout layout, const VkDescriptorSet* sets, uint32_t setCount, const void* push, uint32_t pushSize,
                        bool load = false);
    void PrefilterInto(VkCommandBuffer cmd, uint32_t cubeIndex); // m_EnvSource (with mips) -> m_EnvCubes slice
    void GenerateSourceMips(VkCommandBuffer cmd);
    void DrawEntity(VkCommandBuffer cmd, const Scene& scene, const Entity& e, DrawMode mode,
                    const glm::vec4& overrideColor = glm::vec4(1.0f), float cascade = 0.0f);

    VulkanContext* m_Vk = nullptr;
    ResourceCache* m_Res = nullptr;
    Target m_Targets[ViewCount];

    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_UboLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_CompositeLayout = VK_NULL_HANDLE; // two samplers (also used by SSAO passes)
    VkDescriptorSetLayout m_FinalLayout = VK_NULL_HANDLE;     // hdr, mask, bloom
    VkPipelineLayout m_SceneLayout = VK_NULL_HANDLE;          // set 0 scene, set 1 material
    VkPipelineLayout m_PostLayout = VK_NULL_HANDLE;           // set 0 scene, set 1 two textures
    VkPipelineLayout m_CompositeLayoutPipe = VK_NULL_HANDLE;  // set 0 final textures + 128 byte push
    VkPipelineLayout m_TexPushLayout = VK_NULL_HANDLE;        // set 0 two textures + 128 byte push (bloom, cube passes)
    VkPipelineLayout m_UboPushLayout = VK_NULL_HANDLE;        // set 0 scene + 128 byte push (sky cube capture)
    VkSampler m_Sampler = VK_NULL_HANDLE;
    VkSampler m_PointSampler = VK_NULL_HANDLE;
    VkSampler m_EnvSampler = VK_NULL_HANDLE; // trilinear, all mips

    VkPipeline m_SkyPipeline = VK_NULL_HANDLE;
    VkPipeline m_MeshPipeline = VK_NULL_HANDLE;
    VkPipeline m_WirePipeline = VK_NULL_HANDLE;
    VkPipeline m_GridPipeline = VK_NULL_HANDLE;
    VkPipeline m_MaskPipeline = VK_NULL_HANDLE;
    VkPipeline m_CompositePipeline = VK_NULL_HANDLE;
    VkPipeline m_ShadowPipeline = VK_NULL_HANDLE;
    VkPipeline m_LocalShadowPipeline = VK_NULL_HANDLE;
    VkPipeline m_NormalsPipeline = VK_NULL_HANDLE;
    VkPipeline m_SsaoPipeline = VK_NULL_HANDLE;
    VkPipeline m_BlurPipeline = VK_NULL_HANDLE;
    VkPipeline m_SkyCubePipeline = VK_NULL_HANDLE;
    VkPipeline m_RemapPipeline = VK_NULL_HANDLE;
    VkPipeline m_PrefilterPipeline = VK_NULL_HANDLE;
    VkPipeline m_BloomDownPipeline = VK_NULL_HANDLE;
    VkPipeline m_BloomUpPipeline = VK_NULL_HANDLE;
    // Skinned variants
    VkPipeline m_MeshSkinnedPipeline = VK_NULL_HANDLE;
    VkPipeline m_WireSkinnedPipeline = VK_NULL_HANDLE;
    VkPipeline m_MaskSkinnedPipeline = VK_NULL_HANDLE;
    VkPipeline m_ShadowSkinnedPipeline = VK_NULL_HANDLE;
    VkPipeline m_NormalsSkinnedPipeline = VK_NULL_HANDLE;
    VkPipeline m_ActiveNormal = VK_NULL_HANDLE, m_ActiveSkinned = VK_NULL_HANDLE, m_BoundPipeline = VK_NULL_HANDLE;

    // Skinning: joint matrices of every skinned mesh this frame (one storage buffer per frame in flight).
    static constexpr VkDeviceSize kSkinBufferSize = 16 * 1024 * 1024;
    VkDescriptorSetLayout m_SkinLayout = VK_NULL_HANDLE;
    GpuBuffer m_SkinBuffer[VulkanContext::kFramesInFlight];
    VkDescriptorSet m_SkinSet[VulkanContext::kFramesInFlight] = {};
    VkDeviceSize m_SkinCursor = 0;
    std::unordered_map<EntityId, uint32_t> m_SkinOffsets;
    PaletteProvider m_Palettes;

    // Cascaded shadow map shared by all views (views render sequentially in one command buffer).
    static constexpr uint32_t kCascades = 4;
    static constexpr uint32_t kShadowSize = 2048;
    GpuImage m_ShadowMap;
    VkImageView m_ShadowLayerViews[kCascades] = {};
    VkSampler m_ShadowSampler = VK_NULL_HANDLE;

    // Point / spot light shadow layers.
    static constexpr uint32_t kLocalShadowSize = 1024;
    GpuImage m_LocalShadowMap;
    VkImageView m_LocalShadowViews[kMaxLocalShadowLayers] = {};
    int m_LocalShadowCount = 0;

    // Reflections: prefiltered cube maps (slice 0 = sky, 1..kMaxProbes = probes) and a capture source cube.
    static constexpr uint32_t kEnvSize = 128;
    static constexpr uint32_t kEnvMips = 8;
    static constexpr uint32_t kEnvCubes = 1 + kMaxProbes;
    GpuImage m_EnvCubes;
    std::vector<VkImageView> m_EnvFaceViews; // [(cube * 6 + face) * kEnvMips + mip]
    GpuImage m_EnvSource;
    GpuImage m_WhiteImage; // 1x1 white, bound where no AO texture exists
    VkImageView m_EnvSourceFaceViews[6] = {};
    VkDescriptorSet m_PrefilterSet = VK_NULL_HANDLE;
    GpuBuffer m_EnvUbo[VulkanContext::kFramesInFlight];
    VkDescriptorSet m_EnvUboSet[VulkanContext::kFramesInFlight] = {};
    std::string m_EnvSignature;
    std::string m_LightingSignature;
    uint32_t m_LightingVersion = 0;
    std::map<EntityId, uint32_t> m_ProbeSlots; // baked probes -> cube index (1..kMaxProbes)

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
