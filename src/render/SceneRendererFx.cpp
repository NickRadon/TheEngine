// SceneRenderer: point/spot light shadows, reflections (sky cube map + reflection probes) and bloom.
#include "render/SceneRenderer.h"

#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace
{
    // Barrier for a range of mips/layers (the context helper always covers the whole image).
    void SubBarrier(VkCommandBuffer cmd, VkImage image, uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount,
                    VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
    {
        VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = srcStage;
        barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, baseLayer, layerCount };
        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    constexpr VkPipelineStageFlags2 kFrag = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    constexpr VkPipelineStageFlags2 kColorOut = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    constexpr VkAccessFlags2 kColorWrite = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    constexpr VkAccessFlags2 kShaderRead = VK_ACCESS_2_SHADER_READ_BIT;

    // Cube face orientation for captures: forward and up per face (+X -X +Y -Y +Z -Z).
    const glm::vec3 kFaceForward[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    const glm::vec3 kFaceUp[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };
}

void SceneRenderer::ResetStats()
{
    m_DrawCalls = 0;
    m_Triangles = 0;
    m_SkinCursor = 0;
    m_SkinOffsets.clear();
}

void SceneRenderer::UsePipelines(VkCommandBuffer cmd, VkPipeline normal, VkPipeline skinned)
{
    m_ActiveNormal = normal;
    m_ActiveSkinned = skinned;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, normal);
    m_BoundPipeline = normal;
}

uint32_t SceneRenderer::UploadPalette(EntityId id, const std::vector<glm::mat4>& palette)
{
    // Each skinned object's joints are uploaded once per frame and shared by all passes and views.
    auto it = m_SkinOffsets.find(id);
    if (it != m_SkinOffsets.end()) return it->second;
    const VkDeviceSize bytes = palette.size() * sizeof(glm::mat4);
    const VkDeviceSize range = 256 * sizeof(glm::mat4) * 4; // descriptor range (1024 joints)
    if (palette.size() > 1024 || m_SkinCursor + range > kSkinBufferSize)
    {
        LOG_WARN("Skinning buffer full; skipping a skinned mesh");
        return ~0u;
    }
    const uint32_t offset = static_cast<uint32_t>(m_SkinCursor);
    std::memcpy(static_cast<uint8_t*>(m_SkinBuffer[m_Vk->FrameIndex()].mapped) + offset, palette.data(), bytes);
    m_SkinCursor += (bytes + 255) & ~VkDeviceSize(255);
    m_SkinOffsets[id] = offset;
    return offset;
}

VkDescriptorSet SceneRenderer::Allocate(VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    alloc.descriptorPool = m_DescriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_Vk->Device(), &alloc, &set) != VK_SUCCESS) LOG_ERROR("Descriptor pool exhausted");
    return set;
}

void SceneRenderer::WriteImage(VkDescriptorSet set, uint32_t binding, VkImageView view, VkSampler sampler)
{
    VkDescriptorImageInfo info{ sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(m_Vk->Device(), 1, &write, 0, nullptr);
}

void SceneRenderer::FillSkyUniforms(const Scene& scene, float time, SceneUBO& ubo, glm::vec3& sunDir, float& sunIntensity, EntityId& sunId) const
{
    const SkySettings& sky = scene.sky;
    glm::vec3 sunColor;
    FindSun(scene, sunDir, sunColor, sunIntensity, &sunId);
    ubo.sunDir = glm::vec4(sunDir, sunIntensity);
    ubo.sunColor = glm::vec4(sunColor, sky.ambientIntensity);
    ubo.skyTint = glm::vec4(sky.skyTint, sky.atmosphereThickness);
    ubo.groundColor = glm::vec4(sky.groundColor, sky.exposure);
    ubo.sunParams = glm::vec4(sky.sunSize, sky.sunConvergence, time, sky.stars);
    ubo.cloudParams = glm::vec4(sky.cloudCoverage, sky.cloudDensity, sky.cloudSpeed, sky.cloudScale);
    ComputeAmbient(sky, sunDir, sunIntensity, ubo);
}

void SceneRenderer::FullscreenPass(VkCommandBuffer cmd, VkImageView target, uint32_t width, uint32_t height, VkPipeline pipeline,
                                   VkPipelineLayout layout, const VkDescriptorSet* sets, uint32_t setCount, const void* push,
                                   uint32_t pushSize, bool load)
{
    VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    color.imageView = target;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{ { 0, 0 }, { width, height } };
    VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &info);
    const VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &area);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, setCount, sets, 0, nullptr);
    if (pushSize) vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
    m_DrawCalls++;
}

// ---------------------------------------------------------------------------
// Point / spot light shadows
// ---------------------------------------------------------------------------
void SceneRenderer::RenderLocalShadows(VkCommandBuffer cmd, const Scene& scene, VkDescriptorSet uboSet, int layers)
{
    const VkPipelineStageFlags2 depthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    VulkanContext::ImageBarrier(cmd, m_LocalShadowMap.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kFrag, 0, depthStages,
                                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    const VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(kLocalShadowSize), static_cast<float>(kLocalShadowSize), 0.0f, 1.0f };
    const VkRect2D scissor{ { 0, 0 }, { kLocalShadowSize, kLocalShadowSize } };
    // Only the assigned layers are rendered; the shader never samples the others.
    for (int layer = 0; layer < layers; ++layer)
    {
        VkRenderingAttachmentInfo depth{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        depth.imageView = m_LocalShadowViews[layer];
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil = { 1.0f, 0 };
        VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        info.renderArea = scissor;
        info.layerCount = 1;
        info.pDepthAttachment = &depth;
        vkCmdBeginRendering(cmd, &info);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        UsePipelines(cmd, m_ShadowPipeline, m_ShadowSkinnedPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SceneLayout, 0, 1, &uboSet, 0, nullptr);
        for (const Entity& e : scene.entities)
        {
            if (!e.meshRenderer.enabled || !e.meshRenderer.castShadows || !scene.IsActiveInHierarchy(e.id)) continue;
            DrawEntity(cmd, scene, e, DrawMode::DepthOnly, glm::vec4(1.0f), static_cast<float>(kCascades + layer));
        }
        vkCmdEndRendering(cmd);
    }
    VulkanContext::ImageBarrier(cmd, m_LocalShadowMap.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, depthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                kFrag, kShaderRead);
}

// ---------------------------------------------------------------------------
// Bloom
// ---------------------------------------------------------------------------
void SceneRenderer::RenderBloom(VkCommandBuffer cmd, Target& t, const PostProcessSettings& post)
{
    struct Push
    {
        glm::vec4 params;
        glm::vec4 knee;
    };
    const uint32_t bw = t.bloom.extent.width, bh = t.bloom.extent.height;
    // Downsample chain (the first pass reads the full resolution HDR image and applies the threshold).
    for (uint32_t i = 0; i < t.bloomMips; ++i)
    {
        const uint32_t srcW = i == 0 ? t.width : std::max(bw >> (i - 1), 1u);
        const uint32_t srcH = i == 0 ? t.height : std::max(bh >> (i - 1), 1u);
        SubBarrier(cmd, t.bloom.image, i, 1, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   kFrag, 0, kColorOut, kColorWrite);
        Push push{ glm::vec4(1.0f / srcW, 1.0f / srcH, post.bloomThreshold, i == 0 ? 1.0f : 0.0f), glm::vec4(0.5f, 0, 0, 0) };
        FullscreenPass(cmd, t.bloomViews[i], std::max(bw >> i, 1u), std::max(bh >> i, 1u), m_BloomDownPipeline, m_TexPushLayout,
                       &t.bloomDownSets[i], 1, &push, sizeof(push));
        SubBarrier(cmd, t.bloom.image, i, 1, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   kColorOut, kColorWrite, kFrag, kShaderRead);
    }
    // Upsample: each mip adds the blurred smaller mip (scatter controls how much wide glow is kept).
    for (int i = static_cast<int>(t.bloomMips) - 2; i >= 0; --i)
    {
        const uint32_t srcW = std::max(bw >> (i + 1), 1u), srcH = std::max(bh >> (i + 1), 1u);
        SubBarrier(cmd, t.bloom.image, i, 1, 0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   kFrag, kShaderRead, kColorOut, kColorWrite | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
        Push push{ glm::vec4(1.0f / srcW, 1.0f / srcH, std::clamp(post.bloomScatter, 0.0f, 1.0f) * 1.3f, 0.0f), glm::vec4(0.0f) };
        FullscreenPass(cmd, t.bloomViews[i], std::max(bw >> i, 1u), std::max(bh >> i, 1u), m_BloomUpPipeline, m_TexPushLayout,
                       &t.bloomUpSets[i], 1, &push, sizeof(push), true);
        SubBarrier(cmd, t.bloom.image, i, 1, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   kColorOut, kColorWrite, kFrag, kShaderRead);
    }
}

// ---------------------------------------------------------------------------
// Reflections
// ---------------------------------------------------------------------------
void SceneRenderer::GenerateSourceMips(VkCommandBuffer cmd)
{
    // Mip 0 was just rendered (color attachment); build the chain with linear blits for filtered importance sampling.
    SubBarrier(cmd, m_EnvSource.image, 0, 1, 0, 6, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               kColorOut, kColorWrite, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    for (uint32_t mip = 1; mip < kEnvMips; ++mip)
    {
        SubBarrier(cmd, m_EnvSource.image, mip, 1, 0, 6, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   kFrag, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const int32_t src = static_cast<int32_t>(std::max(kEnvSize >> (mip - 1), 1u));
        const int32_t dst = static_cast<int32_t>(std::max(kEnvSize >> mip, 1u));
        VkImageBlit blit{};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 6 };
        blit.srcOffsets[1] = { src, src, 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 6 };
        blit.dstOffsets[1] = { dst, dst, 1 };
        vkCmdBlitImage(cmd, m_EnvSource.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_EnvSource.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        SubBarrier(cmd, m_EnvSource.image, mip, 1, 0, 6, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                   VK_ACCESS_2_TRANSFER_READ_BIT);
    }
    SubBarrier(cmd, m_EnvSource.image, 0, kEnvMips, 0, 6, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, kFrag, kShaderRead);
}

void SceneRenderer::PrefilterInto(VkCommandBuffer cmd, uint32_t cubeIndex)
{
    SubBarrier(cmd, m_EnvCubes.image, 0, kEnvMips, cubeIndex * 6, 6, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               kFrag, 0, kColorOut, kColorWrite);
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t mip = 0; mip < kEnvMips; ++mip)
        {
            const glm::vec4 push(static_cast<float>(face), static_cast<float>(mip) / (kEnvMips - 1), static_cast<float>(kEnvSize), 0.0f);
            const uint32_t size = std::max(kEnvSize >> mip, 1u);
            FullscreenPass(cmd, m_EnvFaceViews[(cubeIndex * 6 + face) * kEnvMips + mip], size, size, m_PrefilterPipeline,
                           m_TexPushLayout, &m_PrefilterSet, 1, &push, sizeof(push));
        }
    }
    SubBarrier(cmd, m_EnvCubes.image, 0, kEnvMips, cubeIndex * 6, 6, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kColorOut, kColorWrite, kFrag, kShaderRead);
}

void SceneRenderer::UpdateEnvironment(VkCommandBuffer cmd, const Scene& scene, float time)
{
    if (!m_SkyCubePipeline) return;

    // Probes whose object is gone free their slot.
    for (auto it = m_ProbeSlots.begin(); it != m_ProbeSlots.end();)
    {
        const Entity* e = scene.Find(it->first);
        it = (!e || !e->reflectionProbe.enabled) ? m_ProbeSlots.erase(it) : std::next(it);
    }

    SceneUBO ubo{};
    glm::vec3 sunDir;
    float sunIntensity;
    EntityId sunId;
    FillSkyUniforms(scene, time, ubo, sunDir, sunIntensity, sunId);

    // Only re-capture when the sky changed (moving clouds: twice a second).
    const SkySettings& sky = scene.sky;
    std::ostringstream sig;
    sig << sky.enabled << ' ' << sky.sunSize << ' ' << sky.sunConvergence << ' ' << sky.atmosphereThickness << ' ' << sky.skyTint.x << ' '
        << sky.skyTint.y << ' ' << sky.skyTint.z << ' ' << sky.groundColor.x << ' ' << sky.groundColor.y << ' ' << sky.groundColor.z << ' '
        << sky.exposure << ' ' << sky.cloudCoverage << ' ' << sky.cloudDensity << ' ' << sky.cloudScale << ' ' << sky.stars << ' '
        << sky.fallbackColor.x << ' ' << sky.fallbackColor.y << ' ' << sky.fallbackColor.z << ' '
        << sunDir.x << ' ' << sunDir.y << ' ' << sunDir.z << ' ' << sunIntensity << ' ' << ubo.sunColor.x << ubo.sunColor.y << ubo.sunColor.z;
    if (sig.str() != m_LightingSignature)
    {
        m_LightingSignature = sig.str();
        ++m_LightingVersion;
    }
    if (sky.cloudCoverage > 0.0f && sky.cloudSpeed != 0.0f) sig << " t" << static_cast<int>(time * 2.0f);
    if (sig.str() == m_EnvSignature) return;
    m_EnvSignature = sig.str();

    const uint32_t frame = m_Vk->FrameIndex();
    std::memcpy(m_EnvUbo[frame].mapped, &ubo, sizeof(ubo));

    SubBarrier(cmd, m_EnvSource.image, 0, 1, 0, 6, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               kFrag, 0, kColorOut, kColorWrite);
    for (uint32_t face = 0; face < 6; ++face)
    {
        if (sky.enabled)
        {
            const glm::vec4 push(static_cast<float>(face), 0.0f, 0.0f, 0.0f);
            FullscreenPass(cmd, m_EnvSourceFaceViews[face], kEnvSize, kEnvSize, m_SkyCubePipeline, m_UboPushLayout,
                           &m_EnvUboSet[frame], 1, &push, sizeof(push));
        }
        else
        {
            // No skybox: reflect the solid background color.
            VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            color.imageView = m_EnvSourceFaceViews[face];
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            const glm::vec3 c = glm::pow(sky.fallbackColor, glm::vec3(2.2f));
            color.clearValue.color = { { c.r, c.g, c.b, 1.0f } };
            VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            info.renderArea = { { 0, 0 }, { kEnvSize, kEnvSize } };
            info.layerCount = 1;
            info.colorAttachmentCount = 1;
            info.pColorAttachments = &color;
            vkCmdBeginRendering(cmd, &info);
            vkCmdEndRendering(cmd);
        }
    }
    GenerateSourceMips(cmd);
    PrefilterInto(cmd, 0);
}

bool SceneRenderer::BakeProbe(VkCommandBuffer cmd, const Scene& scene, EntityId probe, float time)
{
    const Entity* e = scene.Find(probe);
    if (!e || !e->reflectionProbe.enabled || !m_RemapPipeline) return false;

    uint32_t slot = 0;
    if (auto it = m_ProbeSlots.find(probe); it != m_ProbeSlots.end()) slot = it->second;
    else
    {
        for (uint32_t candidate = 1; candidate <= kMaxProbes && !slot; ++candidate)
        {
            bool used = false;
            for (auto& [id, s] : m_ProbeSlots) used |= s == candidate;
            if (!used) slot = candidate;
        }
        if (!slot)
        {
            LOG_WARN("Too many reflection probes (max %d); '%s' is not baked", kMaxProbes, e->name.c_str());
            return false;
        }
    }

    // Capture the six directions with regular 90 degree cameras.
    const glm::vec3 pos(scene.WorldMatrix(probe)[3]);
    for (int face = 0; face < 6; ++face)
    {
        const ViewId view = static_cast<ViewId>(ProbeFace0 + face);
        EnsureSize(view, kEnvSize, kEnvSize);
        RenderView rv;
        rv.view = glm::lookAt(pos, pos + kFaceForward[face], kFaceUp[face]);
        rv.proj = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 0.05f, 1000.0f);
        rv.cameraPos = pos;
        rv.nearClip = 0.05f;
        rv.farClip = 1000.0f;
        rv.time = time;
        rv.ssao = false;
        rv.postProcessing = false;
        rv.exposure = 1.0f;
        Render(cmd, view, scene, rv);
    }

    // Resample the captures into the source cube, then prefilter into this probe's slice.
    SubBarrier(cmd, m_EnvSource.image, 0, 1, 0, 6, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               kFrag, 0, kColorOut, kColorWrite);
    for (int face = 0; face < 6; ++face)
    {
        const glm::vec3 f = kFaceForward[face];
        const glm::vec3 r = glm::normalize(glm::cross(f, kFaceUp[face]));
        const glm::vec3 u = glm::cross(r, f);
        const glm::vec4 push[4] = { glm::vec4(static_cast<float>(face), 0, 0, 0), glm::vec4(f, 0), glm::vec4(r, 0), glm::vec4(u, 0) };
        FullscreenPass(cmd, m_EnvSourceFaceViews[face], kEnvSize, kEnvSize, m_RemapPipeline, m_TexPushLayout,
                       &m_Targets[ProbeFace0 + face].remapSet, 1, push, sizeof(push));
    }
    GenerateSourceMips(cmd);
    PrefilterInto(cmd, slot);
    m_ProbeSlots[probe] = slot;
    return true;
}
