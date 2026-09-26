#include "render/SceneRenderer.h"

#include "core/Log.h"

#include <imgui_impl_vulkan.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

bool SceneRenderer::Init(VulkanContext* vk, ResourceCache* resources)
{
    m_Vk = vk;
    m_Res = resources;
    VkDevice device = vk->Device();

    // Descriptor pool / layouts
    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 768 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 8 },
    };
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 320;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = sizes;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool);

    VkDescriptorSetLayoutBinding uboBindings[5] = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }, // cascaded shadow map
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }, // ambient occlusion
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }, // point/spot shadows
        { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }, // reflection cube maps
    };
    VkDescriptorSetLayoutCreateInfo uboLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    uboLayout.bindingCount = 5;
    uboLayout.pBindings = uboBindings;
    vkCreateDescriptorSetLayout(device, &uboLayout, nullptr, &m_UboLayout);

    VkDescriptorSetLayoutBinding texBindings[2] = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    };
    VkDescriptorSetLayoutCreateInfo texLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    texLayout.bindingCount = 2;
    texLayout.pBindings = texBindings;
    vkCreateDescriptorSetLayout(device, &texLayout, nullptr, &m_CompositeLayout);

    VkDescriptorSetLayoutBinding finalBindings[3] = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    };
    VkDescriptorSetLayoutCreateInfo finalLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    finalLayout.bindingCount = 3;
    finalLayout.pBindings = finalBindings;
    vkCreateDescriptorSetLayout(device, &finalLayout, nullptr, &m_FinalLayout);

    VkDescriptorSetLayoutBinding skinBinding{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo skinLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    skinLayout.bindingCount = 1;
    skinLayout.pBindings = &skinBinding;
    vkCreateDescriptorSetLayout(device, &skinLayout, nullptr, &m_SkinLayout);

    VkPushConstantRange scenePush{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants) };
    VkDescriptorSetLayout sceneSets[3] = { m_UboLayout, resources->MaterialLayout(), m_SkinLayout };
    VkPipelineLayoutCreateInfo sceneLayout{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    sceneLayout.setLayoutCount = 3;
    sceneLayout.pSetLayouts = sceneSets;
    sceneLayout.pushConstantRangeCount = 1;
    sceneLayout.pPushConstantRanges = &scenePush;
    vkCreatePipelineLayout(device, &sceneLayout, nullptr, &m_SceneLayout);

    VkDescriptorSetLayout postSets[2] = { m_UboLayout, m_CompositeLayout };
    VkPipelineLayoutCreateInfo postLayout{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    postLayout.setLayoutCount = 2;
    postLayout.pSetLayouts = postSets;
    vkCreatePipelineLayout(device, &postLayout, nullptr, &m_PostLayout);

    VkPushConstantRange fragPush{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
    auto pushLayout = [&](VkDescriptorSetLayout set, VkPipelineLayout& out) {
        VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        info.setLayoutCount = 1;
        info.pSetLayouts = &set;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &fragPush;
        vkCreatePipelineLayout(device, &info, nullptr, &out);
    };
    pushLayout(m_FinalLayout, m_CompositeLayoutPipe);
    pushLayout(m_CompositeLayout, m_TexPushLayout);
    pushLayout(m_UboLayout, m_UboPushLayout);

    VkSamplerCreateInfo sampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 1.0f;
    vkCreateSampler(device, &sampler, nullptr, &m_Sampler);
    VkSamplerCreateInfo point = sampler;
    point.magFilter = point.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(device, &point, nullptr, &m_PointSampler);
    VkSamplerCreateInfo env = sampler;
    env.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    env.maxLod = static_cast<float>(kEnvMips);
    vkCreateSampler(device, &env, nullptr, &m_EnvSampler);

    // Shadow map: depth array with hardware comparison (PCF).
    VkSamplerCreateInfo shadowSampler = sampler;
    shadowSampler.compareEnable = VK_TRUE;
    shadowSampler.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    shadowSampler.addressModeU = shadowSampler.addressModeV = shadowSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowSampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    vkCreateSampler(device, &shadowSampler, nullptr, &m_ShadowSampler);
    m_ShadowMap = vk->CreateImage(kShadowSize, kShadowSize, kShadowFormat,
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                  VK_IMAGE_ASPECT_DEPTH_BIT, VK_SAMPLE_COUNT_1_BIT, kCascades);
    for (uint32_t i = 0; i < kCascades; ++i)
        m_ShadowLayerViews[i] = vk->CreateLayerView(m_ShadowMap, i, VK_IMAGE_ASPECT_DEPTH_BIT);
    vk->ImmediateSubmit([&](VkCommandBuffer cmd) {
        // Cleared to "no occluder" so views can sample it even when no shadows were rendered.
        VulkanContext::ImageBarrier(cmd, m_ShadowMap.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkClearDepthStencilValue clear{ 1.0f, 0 };
        VkImageSubresourceRange range{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kCascades };
        vkCmdClearDepthStencilImage(cmd, m_ShadowMap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        VulkanContext::ImageBarrier(cmd, m_ShadowMap.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                    VK_ACCESS_2_SHADER_READ_BIT);
    });

    // Point/spot light shadow layers and the reflection cube maps.
    m_LocalShadowMap = vk->CreateImage(kLocalShadowSize, kLocalShadowSize, kShadowFormat,
                                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                       VK_IMAGE_ASPECT_DEPTH_BIT, VK_SAMPLE_COUNT_1_BIT, kMaxLocalShadowLayers);
    for (uint32_t i = 0; i < kMaxLocalShadowLayers; ++i)
        m_LocalShadowViews[i] = vk->CreateLayerView(m_LocalShadowMap, i, VK_IMAGE_ASPECT_DEPTH_BIT);
    m_EnvCubes = vk->CreateImage(kEnvSize, kEnvSize, kHdrFormat,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                 VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT, 6 * kEnvCubes, kEnvMips, true);
    for (uint32_t layer = 0; layer < 6 * kEnvCubes; ++layer)
        for (uint32_t mip = 0; mip < kEnvMips; ++mip)
            m_EnvFaceViews.push_back(vk->CreateSubView(m_EnvCubes, layer, mip, VK_IMAGE_ASPECT_COLOR_BIT));
    m_EnvSource = vk->CreateImage(kEnvSize, kEnvSize, kHdrFormat,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT, 6, kEnvMips, true);
    for (uint32_t face = 0; face < 6; ++face)
        m_EnvSourceFaceViews[face] = vk->CreateSubView(m_EnvSource, face, 0, VK_IMAGE_ASPECT_COLOR_BIT);
    m_WhiteImage = vk->CreateImage(1, 1, kAoFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    vk->ImmediateSubmit([&](VkCommandBuffer cmd) {
        auto clearTo = [&](const GpuImage& img, VkImageAspectFlags aspect, float value) {
            VulkanContext::ImageBarrier(cmd, img.image, aspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            const VkImageSubresourceRange range{ aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
            if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
            {
                VkClearDepthStencilValue clear{ value, 0 };
                vkCmdClearDepthStencilImage(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
            }
            else
            {
                VkClearColorValue clear{ { value, value, value, 1.0f } };
                vkCmdClearColorImage(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
            }
            VulkanContext::ImageBarrier(cmd, img.image, aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        };
        clearTo(m_LocalShadowMap, VK_IMAGE_ASPECT_DEPTH_BIT, 1.0f);
        clearTo(m_EnvCubes, VK_IMAGE_ASPECT_COLOR_BIT, 0.0f);
        clearTo(m_EnvSource, VK_IMAGE_ASPECT_COLOR_BIT, 0.0f);
        clearTo(m_WhiteImage, VK_IMAGE_ASPECT_COLOR_BIT, 1.0f);
    });

    const VkSampleCountFlagBits maxSamples = vk->MaxMsaaSamples();
    m_Samples = maxSamples >= VK_SAMPLE_COUNT_4_BIT ? VK_SAMPLE_COUNT_4_BIT : maxSamples;

    // Per-view uniform buffers and descriptor sets
    auto allocate = [&](VkDescriptorSetLayout layout) { return Allocate(layout); };
    // Scene uniform sets: per view and frame, plus one per frame for the sky cube capture.
    auto makeUboSet = [&](GpuBuffer& buffer, VkDescriptorSet& set) {
        buffer = vk->CreateBuffer(sizeof(SceneUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        set = allocate(m_UboLayout);
        VkDescriptorBufferInfo bufInfo{ buffer.buffer, 0, sizeof(SceneUBO) };
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        WriteImage(set, 1, m_ShadowMap.view, m_ShadowSampler);
        WriteImage(set, 2, m_WhiteImage.view, m_Sampler);
        WriteImage(set, 3, m_LocalShadowMap.view, m_ShadowSampler);
        WriteImage(set, 4, m_EnvCubes.view, m_EnvSampler);
    };
    for (uint32_t f = 0; f < VulkanContext::kFramesInFlight; ++f) makeUboSet(m_EnvUbo[f], m_EnvUboSet[f]);
    for (uint32_t f = 0; f < VulkanContext::kFramesInFlight; ++f)
    {
        m_SkinBuffer[f] = vk->CreateBuffer(kSkinBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_SkinSet[f] = allocate(m_SkinLayout);
        VkDescriptorBufferInfo info{ m_SkinBuffer[f].buffer, 0, 256 * sizeof(glm::mat4) * 4 };
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = m_SkinSet[f];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        write.pBufferInfo = &info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    m_PrefilterSet = allocate(m_CompositeLayout);
    WriteImageSet(m_PrefilterSet, m_EnvSource.view, m_EnvSampler, m_EnvSource.view, m_EnvSampler);
    for (Target& t : m_Targets)
    {
        for (uint32_t f = 0; f < VulkanContext::kFramesInFlight; ++f) makeUboSet(t.ubo[f], t.uboSet[f]);
        t.compositeSet = allocate(m_FinalLayout);
        t.ssaoSet = allocate(m_CompositeLayout);
        t.blurSet = allocate(m_CompositeLayout);
        t.remapSet = allocate(m_CompositeLayout);
        for (uint32_t i = 0; i < kBloomMips; ++i)
        {
            t.bloomDownSets[i] = allocate(m_CompositeLayout);
            t.bloomUpSets[i] = allocate(m_CompositeLayout);
        }
    }

    CreatePipelines();
    return m_MeshPipeline != VK_NULL_HANDLE;
}

void SceneRenderer::Shutdown()
{
    VkDevice device = m_Vk->Device();
    vkDeviceWaitIdle(device);
    for (Target& t : m_Targets)
    {
        DestroyTarget(t);
        for (auto& b : t.ubo) m_Vk->DestroyBuffer(b);
    }
    for (VkPipeline p : { m_SkyPipeline, m_MeshPipeline, m_WirePipeline, m_GridPipeline, m_MaskPipeline, m_CompositePipeline,
                          m_ShadowPipeline, m_NormalsPipeline, m_SsaoPipeline, m_BlurPipeline, m_SkyCubePipeline, m_RemapPipeline,
                          m_PrefilterPipeline, m_BloomDownPipeline, m_BloomUpPipeline })
        if (p) vkDestroyPipeline(device, p, nullptr);
    for (auto& b : m_EnvUbo) m_Vk->DestroyBuffer(b);
    for (auto& b : m_SkinBuffer) m_Vk->DestroyBuffer(b);
    for (VkPipeline p : { m_MeshSkinnedPipeline, m_WireSkinnedPipeline, m_MaskSkinnedPipeline, m_ShadowSkinnedPipeline, m_NormalsSkinnedPipeline })
        if (p) vkDestroyPipeline(device, p, nullptr);
    vkDestroyDescriptorSetLayout(device, m_SkinLayout, nullptr);
    for (VkImageView v : m_LocalShadowViews) vkDestroyImageView(device, v, nullptr);
    for (VkImageView v : m_EnvFaceViews) vkDestroyImageView(device, v, nullptr);
    for (VkImageView v : m_EnvSourceFaceViews) vkDestroyImageView(device, v, nullptr);
    m_Vk->DestroyImage(m_LocalShadowMap);
    m_Vk->DestroyImage(m_EnvCubes);
    m_Vk->DestroyImage(m_EnvSource);
    m_Vk->DestroyImage(m_WhiteImage);
    vkDestroySampler(device, m_EnvSampler, nullptr);
    vkDestroyPipelineLayout(device, m_SceneLayout, nullptr);
    vkDestroyPipelineLayout(device, m_PostLayout, nullptr);
    vkDestroyPipelineLayout(device, m_CompositeLayoutPipe, nullptr);
    vkDestroyPipelineLayout(device, m_TexPushLayout, nullptr);
    vkDestroyPipelineLayout(device, m_UboPushLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_UboLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_CompositeLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_FinalLayout, nullptr);
    vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
    vkDestroySampler(device, m_Sampler, nullptr);
    vkDestroySampler(device, m_PointSampler, nullptr);
    vkDestroySampler(device, m_ShadowSampler, nullptr);
    for (VkImageView v : m_ShadowLayerViews) vkDestroyImageView(device, v, nullptr);
    m_Vk->DestroyImage(m_ShadowMap);
}

VkPipeline SceneRenderer::CreatePipeline(VkShaderModule vert, VkShaderModule frag, VkPipelineLayout layout,
                                         VkFormat colorFormat, VkFormat depthFormat, bool vertexInput,
                                         bool depthTest, bool depthWrite, bool blend, VkPolygonMode polygon,
                                         VkCullModeFlags cull, float depthBias, VkSampleCountFlagBits samples, bool additive,
                                         bool skinned)
{
    VkPipelineShaderStageCreateInfo stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr },
    };

    VkVertexInputBindingDescription bindings[2] = {
        { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX },
        { 1, sizeof(SkinVertex), VK_VERTEX_INPUT_RATE_VERTEX },
    };
    VkVertexInputAttributeDescription attrs[5] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) },
        { 3, 1, VK_FORMAT_R16G16B16A16_UINT, offsetof(SkinVertex, joints) },
        { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SkinVertex, weights) },
    };
    VkVertexInputAttributeDescription depthSkinned[3] = { attrs[0], attrs[3], attrs[4] };
    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    if (vertexInput)
    {
        vi.vertexBindingDescriptionCount = skinned ? 2 : 1;
        vi.pVertexBindingDescriptions = bindings;
        // Depth-only pipelines only consume the position (and skin influences).
        if (skinned)
        {
            vi.vertexAttributeDescriptionCount = frag ? 5 : 3;
            vi.pVertexAttributeDescriptions = frag ? attrs : depthSkinned;
        }
        else
        {
            vi.vertexAttributeDescriptionCount = frag ? 3 : 1;
            vi.pVertexAttributeDescriptions = attrs;
        }
    }

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    // depthBias > 0 pushes geometry away (shadow casters), < 0 pulls it closer (wireframe overlay).
    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = polygon;
    rs.cullMode = cull;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    rs.depthBiasEnable = depthBias != 0.0f ? VK_TRUE : VK_FALSE;
    rs.depthBiasConstantFactor = depthBias;
    rs.depthBiasSlopeFactor = depthBias * 1.5f;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = samples;

    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = colorFormat == kMaskFormat ? VK_COLOR_COMPONENT_R_BIT
                                                    : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT);
    if (blend)
    {
        att.blendEnable = VK_TRUE;
        att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        att.colorBlendOp = VK_BLEND_OP_ADD;
        att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        att.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    if (additive)
    {
        att.blendEnable = VK_TRUE;
        att.srcColorBlendFactor = att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        att.srcAlphaBlendFactor = att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        att.colorBlendOp = att.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    const bool hasColor = colorFormat != VK_FORMAT_UNDEFINED;
    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = hasColor ? 1 : 0;
    cb.pAttachments = &att;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    rendering.colorAttachmentCount = hasColor ? 1 : 0;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    info.pNext = &rendering;
    info.stageCount = frag ? 2 : 1;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dyn;
    info.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(m_Vk->Device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS)
        LOG_ERROR("Failed to create graphics pipeline");
    return pipeline;
}

void SceneRenderer::CreatePipelines()
{
    VkDevice device = m_Vk->Device();
    VkShaderModule fullscreen = m_Vk->LoadShader("fullscreen.vert");
    VkShaderModule sky = m_Vk->LoadShader("sky.frag");
    VkShaderModule meshVert = m_Vk->LoadShader("mesh.vert");
    VkShaderModule meshFrag = m_Vk->LoadShader("mesh.frag");
    VkShaderModule grid = m_Vk->LoadShader("grid.frag");
    VkShaderModule mask = m_Vk->LoadShader("mask.frag");
    VkShaderModule composite = m_Vk->LoadShader("composite.frag");
    VkShaderModule shadow = m_Vk->LoadShader("shadow.vert");
    VkShaderModule normals = m_Vk->LoadShader("normals.frag");
    VkShaderModule ssao = m_Vk->LoadShader("ssao.frag");
    VkShaderModule blur = m_Vk->LoadShader("ssao_blur.frag");
    VkShaderModule skyCube = m_Vk->LoadShader("skycube.frag");
    VkShaderModule remap = m_Vk->LoadShader("cube_remap.frag");
    VkShaderModule prefilter = m_Vk->LoadShader("prefilter.frag");
    VkShaderModule bloomDown = m_Vk->LoadShader("bloom_down.frag");
    VkShaderModule bloomUp = m_Vk->LoadShader("bloom_up.frag");
    VkShaderModule meshSkinned = m_Vk->LoadShader("mesh_skinned.vert");
    VkShaderModule shadowSkinned = m_Vk->LoadShader("shadow_skinned.vert");
    const VkShaderModule all[] = { fullscreen, sky, meshVert, meshFrag, grid, mask, composite, shadow, normals, ssao, blur,
                                   skyCube, remap, prefilter, bloomDown, bloomUp, meshSkinned, shadowSkinned };
    for (VkShaderModule m : all)
        if (!m)
        {
            LOG_ERROR("Missing shaders; pipelines not created");
            return;
        }

    const auto one = VK_SAMPLE_COUNT_1_BIT;
    const auto fill = VK_POLYGON_MODE_FILL;
    m_SkyPipeline = CreatePipeline(fullscreen, sky, m_SceneLayout, kHdrFormat, kDepthFormat, false,
                                   false, false, false, fill, VK_CULL_MODE_NONE, 0.0f, m_Samples);
    m_MeshPipeline = CreatePipeline(meshVert, meshFrag, m_SceneLayout, kHdrFormat, kDepthFormat, true,
                                    true, true, false, fill, VK_CULL_MODE_BACK_BIT, 0.0f, m_Samples);
    if (m_Vk->SupportsWireframe())
        m_WirePipeline = CreatePipeline(meshVert, meshFrag, m_SceneLayout, kHdrFormat, kDepthFormat, true,
                                        true, false, false, VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE, -1.0f, m_Samples);
    m_GridPipeline = CreatePipeline(fullscreen, grid, m_SceneLayout, kHdrFormat, kDepthFormat, false,
                                    true, false, true, fill, VK_CULL_MODE_NONE, 0.0f, m_Samples);
    m_MaskPipeline = CreatePipeline(meshVert, mask, m_SceneLayout, kMaskFormat, VK_FORMAT_UNDEFINED, true,
                                    false, false, false, fill, VK_CULL_MODE_NONE, 0.0f, one);
    m_CompositePipeline = CreatePipeline(fullscreen, composite, m_CompositeLayoutPipe, kOutputFormat, VK_FORMAT_UNDEFINED, false,
                                         false, false, false, fill, VK_CULL_MODE_NONE, 0.0f, one);
    m_ShadowPipeline = CreatePipeline(shadow, VK_NULL_HANDLE, m_SceneLayout, VK_FORMAT_UNDEFINED, kShadowFormat, true,
                                      true, true, false, fill, VK_CULL_MODE_NONE, 1.25f, one);
    m_NormalsPipeline = CreatePipeline(meshVert, normals, m_SceneLayout, kNormalFormat, kDepthFormat, true,
                                       true, true, false, fill, VK_CULL_MODE_BACK_BIT, 0.0f, one);
    m_SsaoPipeline = CreatePipeline(fullscreen, ssao, m_PostLayout, kAoFormat, VK_FORMAT_UNDEFINED, false,
                                    false, false, false, fill, VK_CULL_MODE_NONE, 0.0f, one);
    m_BlurPipeline = CreatePipeline(fullscreen, blur, m_PostLayout, kAoFormat, VK_FORMAT_UNDEFINED, false,
                                    false, false, false, fill, VK_CULL_MODE_NONE, 0.0f, one);
    m_MeshSkinnedPipeline = CreatePipeline(meshSkinned, meshFrag, m_SceneLayout, kHdrFormat, kDepthFormat, true,
                                           true, true, false, fill, VK_CULL_MODE_BACK_BIT, 0.0f, m_Samples, false, true);
    if (m_Vk->SupportsWireframe())
        m_WireSkinnedPipeline = CreatePipeline(meshSkinned, meshFrag, m_SceneLayout, kHdrFormat, kDepthFormat, true,
                                               true, false, false, VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE, -1.0f, m_Samples, false, true);
    m_MaskSkinnedPipeline = CreatePipeline(meshSkinned, mask, m_SceneLayout, kMaskFormat, VK_FORMAT_UNDEFINED, true,
                                           false, false, false, fill, VK_CULL_MODE_NONE, 0.0f, one, false, true);
    m_ShadowSkinnedPipeline = CreatePipeline(shadowSkinned, VK_NULL_HANDLE, m_SceneLayout, VK_FORMAT_UNDEFINED, kShadowFormat, true,
                                             true, true, false, fill, VK_CULL_MODE_NONE, 1.25f, one, false, true);
    m_NormalsSkinnedPipeline = CreatePipeline(meshSkinned, normals, m_SceneLayout, kNormalFormat, kDepthFormat, true,
                                              true, true, false, fill, VK_CULL_MODE_BACK_BIT, 0.0f, one, false, true);
    m_SkyCubePipeline = CreatePipeline(fullscreen, skyCube, m_UboPushLayout, kHdrFormat, VK_FORMAT_UNDEFINED, false,
                                       false, false, false, fill, VK_CULL_MODE_NONE, 0.0f, one);
    m_RemapPipeline = CreatePipeline(fullscreen, remap, m_TexPushLayout, kHdrFormat, VK_FORMAT_UNDEFINED, false,
                                     false, false, false, fill, VK_CULL_MODE_NONE, 0.0f, one);
    m_PrefilterPipeline = CreatePipeline(fullscreen, prefilter, m_TexPushLayout, kHdrFormat, VK_FORMAT_UNDEFINED, false,
                                         false, false, false, fill, VK_CULL_MODE_NONE, 0.0f, one);
    m_BloomDownPipeline = CreatePipeline(fullscreen, bloomDown, m_TexPushLayout, kHdrFormat, VK_FORMAT_UNDEFINED, false,
                                         false, false, false, fill, VK_CULL_MODE_NONE, 0.0f, one);
    m_BloomUpPipeline = CreatePipeline(fullscreen, bloomUp, m_TexPushLayout, kHdrFormat, VK_FORMAT_UNDEFINED, false,
                                       false, false, false, fill, VK_CULL_MODE_NONE, 0.0f, one, true);

    for (VkShaderModule m : all) vkDestroyShaderModule(device, m, nullptr);
}

void SceneRenderer::DestroyTarget(Target& t)
{
    if (t.imguiTexture) ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(t.imguiTexture));
    t.imguiTexture = 0;
    for (VkImageView& v : t.bloomViews)
    {
        if (v) vkDestroyImageView(m_Vk->Device(), v, nullptr);
        v = VK_NULL_HANDLE;
    }
    t.bloomMips = 0;
    for (GpuImage* img : { &t.hdrMsaa, &t.hdr, &t.depth, &t.mask, &t.output, &t.prepassDepth, &t.normals, &t.aoRaw, &t.ao, &t.bloom })
        m_Vk->DestroyImage(*img);
    t.width = t.height = 0;
}

void SceneRenderer::WriteImageSet(VkDescriptorSet set, VkImageView a, VkSampler sa, VkImageView b, VkSampler sb)
{
    VkDescriptorImageInfo images[2] = {
        { sa, a, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { sb, b, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };
    VkWriteDescriptorSet writes[2] = {};
    for (int i = 0; i < 2; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(m_Vk->Device(), 2, writes, 0, nullptr);
}

void SceneRenderer::EnsureSize(ViewId view, uint32_t width, uint32_t height)
{
    Target& t = m_Targets[view];
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (t.width == width && t.height == height) return;

    vkDeviceWaitIdle(m_Vk->Device());
    DestroyTarget(t);
    t.width = width;
    t.height = height;
    const VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const auto colorAspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (m_Samples != VK_SAMPLE_COUNT_1_BIT)
        t.hdrMsaa = m_Vk->CreateImage(width, height, kHdrFormat,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                                      colorAspect, m_Samples);
    t.hdr = m_Vk->CreateImage(width, height, kHdrFormat, colorUsage, colorAspect);
    t.depth = m_Vk->CreateImage(width, height, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                VK_IMAGE_ASPECT_DEPTH_BIT, m_Samples);
    t.mask = m_Vk->CreateImage(width, height, kMaskFormat, colorUsage, colorAspect);
    t.output = m_Vk->CreateImage(width, height, kOutputFormat, colorUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, colorAspect); // + CaptureView
    t.prepassDepth = m_Vk->CreateImage(width, height, kDepthFormat,
                                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                       VK_IMAGE_ASPECT_DEPTH_BIT);
    t.normals = m_Vk->CreateImage(width, height, kNormalFormat, colorUsage, colorAspect);
    t.aoRaw = m_Vk->CreateImage(width, height, kAoFormat, colorUsage, colorAspect);
    t.ao = m_Vk->CreateImage(width, height, kAoFormat, colorUsage | VK_IMAGE_USAGE_TRANSFER_DST_BIT, colorAspect);
    const uint32_t bw = std::max(width / 2, 1u), bh = std::max(height / 2, 1u);
    t.bloomMips = 1;
    while (t.bloomMips < kBloomMips && std::min(bw, bh) >> t.bloomMips >= 4) ++t.bloomMips;
    t.bloom = m_Vk->CreateImage(bw, bh, kHdrFormat, colorUsage | VK_IMAGE_USAGE_TRANSFER_DST_BIT, colorAspect, VK_SAMPLE_COUNT_1_BIT, 1, t.bloomMips);
    for (uint32_t i = 0; i < t.bloomMips; ++i) t.bloomViews[i] = m_Vk->CreateSubView(t.bloom, 0, i, colorAspect);

    // Start sampled images in a readable layout (the AO texture cleared to "unoccluded").
    m_Vk->ImmediateSubmit([&](VkCommandBuffer cmd) {
        VulkanContext::ImageBarrier(cmd, t.output.image, colorAspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        VulkanContext::ImageBarrier(cmd, t.ao.image, colorAspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkClearColorValue white{ { 1.0f, 1.0f, 1.0f, 1.0f } };
        VkImageSubresourceRange range{ colorAspect, 0, 1, 0, 1 };
        vkCmdClearColorImage(cmd, t.ao.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &white, 1, &range);
        VulkanContext::ImageBarrier(cmd, t.ao.image, colorAspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        VulkanContext::ImageBarrier(cmd, t.bloom.image, colorAspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkClearColorValue black{ { 0.0f, 0.0f, 0.0f, 1.0f } };
        VkImageSubresourceRange all{ colorAspect, 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
        vkCmdClearColorImage(cmd, t.bloom.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &all);
        VulkanContext::ImageBarrier(cmd, t.bloom.image, colorAspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    });

    WriteImage(t.compositeSet, 0, t.hdr.view, m_Sampler);
    WriteImage(t.compositeSet, 1, t.mask.view, m_Sampler);
    WriteImage(t.compositeSet, 2, t.bloomViews[0], m_Sampler);
    WriteImageSet(t.remapSet, t.hdr.view, m_Sampler, t.hdr.view, m_Sampler);
    for (uint32_t i = 0; i < t.bloomMips; ++i)
    {
        const VkImageView down = i == 0 ? t.hdr.view : t.bloomViews[i - 1];
        WriteImageSet(t.bloomDownSets[i], down, m_Sampler, down, m_Sampler);
        const VkImageView up = i + 1 < t.bloomMips ? t.bloomViews[i + 1] : t.bloomViews[i];
        WriteImageSet(t.bloomUpSets[i], up, m_Sampler, up, m_Sampler);
    }
    WriteImageSet(t.ssaoSet, t.prepassDepth.view, m_PointSampler, t.normals.view, m_PointSampler);
    WriteImageSet(t.blurSet, t.aoRaw.view, m_PointSampler, t.prepassDepth.view, m_PointSampler);
    for (VkDescriptorSet set : t.uboSet)
    {
        VkDescriptorImageInfo aoInfo{ m_Sampler, t.ao.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = set;
        write.dstBinding = 2;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &aoInfo;
        vkUpdateDescriptorSets(m_Vk->Device(), 1, &write, 0, nullptr);
    }

    t.imguiTexture = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(t.output.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
}

namespace
{
    // CPU port of the atmosphere in shaders/sky.glsl, used to derive per-frame ambient lighting colors.
    constexpr float kRPlanet = 6371e3f, kRAtmos = 6471e3f, kViewAltitude = 200.0f;
    constexpr float kHR = 8.0e3f, kHM = 1.2e3f, kBetaM = 21e-6f, kSunScale = 22.0f;

    glm::vec2 RaySphere(const glm::vec3& ro, const glm::vec3& rd, float r)
    {
        float b = glm::dot(ro, rd);
        float c = glm::dot(ro, ro) - r * r;
        float d = b * b - c;
        if (d < 0.0f) return glm::vec2(1e5f, -1e5f);
        d = std::sqrt(d);
        return glm::vec2(-b - d, -b + d);
    }

    struct AtmosphereParams
    {
        glm::vec3 betaR;
        glm::vec3 sunDir;
        float sunIntensity;
    };

    glm::vec3 Transmittance(const AtmosphereParams& p, glm::vec3 dir, int steps)
    {
        glm::vec3 ro(0.0f, kRPlanet + kViewAltitude, 0.0f);
        dir.y = std::max(dir.y, -0.02f);
        dir = glm::normalize(dir);
        float len = RaySphere(ro, dir, kRAtmos).y;
        float ds = len / steps;
        float odR = 0.0f, odM = 0.0f;
        for (int i = 0; i < steps; ++i)
        {
            float h = glm::length(ro + dir * ((i + 0.5f) * ds)) - kRPlanet;
            odR += std::exp(-h / kHR) * ds;
            odM += std::exp(-h / kHM) * ds;
        }
        return glm::exp(-(p.betaR * odR + glm::vec3(kBetaM * 1.1f * odM)));
    }

    glm::vec3 Atmosphere(const AtmosphereParams& p, const glm::vec3& rd, int iSteps, int jSteps)
    {
        glm::vec3 ro(0.0f, kRPlanet + kViewAltitude, 0.0f);
        float tEnd = RaySphere(ro, rd, kRAtmos).y;
        float ds = tEnd / iSteps;
        float mu = glm::dot(rd, p.sunDir);
        float pR = 3.0f / (16.0f * glm::pi<float>()) * (1.0f + mu * mu);
        float pM = 3.0f / (8.0f * glm::pi<float>()) * (1.0f + mu * mu) / 2.0f; // isotropic-ish (g = 0) for ambient
        glm::vec3 totalR(0.0f), totalM(0.0f);
        float odR = 0.0f, odM = 0.0f;
        for (int i = 0; i < iSteps; ++i)
        {
            glm::vec3 pos = ro + rd * ((i + 0.5f) * ds);
            float h = glm::length(pos) - kRPlanet;
            float stepR = std::exp(-h / kHR) * ds;
            float stepM = std::exp(-h / kHM) * ds;
            odR += stepR;
            odM += stepM;
            const glm::vec2 ground = RaySphere(pos, p.sunDir, kRPlanet); // x > y means no hit
            if (ground.x > 0.0f && ground.y >= ground.x) continue;
            float jLen = RaySphere(pos, p.sunDir, kRAtmos).y;
            float dsj = jLen / jSteps;
            float odRj = 0.0f, odMj = 0.0f;
            for (int j = 0; j < jSteps; ++j)
            {
                float hj = glm::length(pos + p.sunDir * ((j + 0.5f) * dsj)) - kRPlanet;
                odRj += std::exp(-hj / kHR) * dsj;
                odMj += std::exp(-hj / kHM) * dsj;
            }
            glm::vec3 attn = glm::exp(-(glm::vec3(kBetaM * 1.1f * (odM + odMj)) + p.betaR * (odR + odRj)));
            totalR += stepR * attn;
            totalM += stepM * attn;
        }
        return p.sunIntensity * (pR * p.betaR * totalR + pM * kBetaM * totalM);
    }
}

void SceneRenderer::ComputeAmbient(const SkySettings& sky, const glm::vec3& sunDir, float sunIntensity, SceneUBO& ubo)
{
    AtmosphereParams p;
    p.betaR = glm::vec3(5.5e-6f, 13.0e-6f, 22.4e-6f) * sky.atmosphereThickness * (glm::vec3(1.5f) - sky.skyTint);
    p.sunDir = glm::normalize(sunDir);
    p.sunIntensity = kSunScale * sunIntensity;

    const glm::vec3 zenith = Atmosphere(p, glm::vec3(0, 1, 0), 16, 8);
    glm::vec3 horizon(0.0f);
    for (int i = 0; i < 8; ++i)
    {
        float a = i * glm::two_pi<float>() / 8.0f;
        horizon += Atmosphere(p, glm::normalize(glm::vec3(std::cos(a), 0.08f, std::sin(a))), 16, 8);
    }
    horizon /= 8.0f;
    const glm::vec3 sunT = Transmittance(p, p.sunDir, 16);
    const glm::vec3 sunLight = sunT * p.sunIntensity * std::max(p.sunDir.y + 0.05f, 0.0f);
    const glm::vec3 ground = sky.groundColor * (sunLight * 0.045f + zenith * 0.6f);

    ubo.ambientZenith = glm::vec4(zenith, 0.0f);
    ubo.ambientHorizon = glm::vec4(horizon, 0.0f);
    ubo.ambientGround = glm::vec4(ground, 0.0f);
    ubo.sunTransmittance = glm::vec4(sunT, 0.0f);
}

bool SceneRenderer::FindSun(const Scene& scene, glm::vec3& dirToSun, glm::vec3& color, float& intensity, EntityId* sunEntity)
{
    for (const Entity& e : scene.entities)
    {
        if (!e.light.enabled || e.light.type != LightType::Directional || !scene.IsActiveInHierarchy(e.id)) continue;
        glm::mat4 world = scene.WorldMatrix(e.id);
        glm::vec3 forward = glm::normalize(glm::vec3(world * glm::vec4(0, 0, -1, 0)));
        dirToSun = -forward;
        color = e.light.color;
        intensity = e.light.intensity;
        if (sunEntity) *sunEntity = e.id;
        return true;
    }
    dirToSun = glm::normalize(glm::vec3(0.3f, 0.6f, 0.4f));
    color = glm::vec3(1.0f);
    intensity = 0.0f;
    if (sunEntity) *sunEntity = kNullEntity;
    return false;
}

void SceneRenderer::DrawEntity(VkCommandBuffer cmd, const Scene& scene, const Entity& e, DrawMode mode,
                               const glm::vec4& overrideColor, float cascade)
{
    const MeshRendererComponent& mr = e.meshRenderer;
    const Mesh* mesh = m_Res->GetMesh(mr.mesh);
    if (!mesh || !mesh->indexCount) return;

    // Skinned meshes use the skinned pipeline variant and their joint matrices.
    const std::vector<glm::mat4>* palette = mesh->data.Skinned() && m_Palettes && m_ActiveSkinned ? m_Palettes(e.id) : nullptr;
    if (palette && palette->size() != mesh->data.jointBones.size()) palette = nullptr;
    const VkPipeline pipeline = palette ? m_ActiveSkinned : m_ActiveNormal;
    if (pipeline && pipeline != m_BoundPipeline)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        m_BoundPipeline = pipeline;
    }
    if (palette)
    {
        const uint32_t offset = UploadPalette(e.id, *palette);
        if (offset == ~0u) return;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SceneLayout, 2, 1, &m_SkinSet[m_Vk->FrameIndex()], 1, &offset);
    }

    PushConstants pc{};
    pc.model = scene.WorldMatrix(e.id);
    pc.extra = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    if (mode == DrawMode::Lit)
    {
        const GpuMaterial& mat = m_Res->GetMaterial(mr.material);
        const MaterialAsset& d = mat.data;
        const bool asset = !mr.material.empty();
        pc.color = glm::vec4(asset ? d.albedo : mr.color, 1.0f);
        pc.params = glm::vec4(asset ? d.metallic : mr.metallic, asset ? d.smoothness : mr.smoothness, 0.0f, 0.0f);
        pc.extra = glm::vec4(d.tiling, d.normalStrength, mat.hasNormalMap ? 1.0f : 0.0f);
        pc.emission = glm::vec4(asset ? d.emission : glm::vec3(0.0f), 0.0f);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SceneLayout, 1, 1, &mat.set, 0, nullptr);
    }
    else if (mode == DrawMode::Unlit)
    {
        pc.color = overrideColor;
        pc.params = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        const VkDescriptorSet set = m_Res->DefaultMaterial().set; // the lit shader declares set 1
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SceneLayout, 1, 1, &set, 0, nullptr);
    }
    else
    {
        pc.params = glm::vec4(0.0f, 0.0f, 0.0f, cascade);
    }
    vkCmdPushConstants(cmd, m_SceneLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->vertexBuffer.buffer, &offset);
    if (palette) vkCmdBindVertexBuffers(cmd, 1, 1, &mesh->skinBuffer.buffer, &offset);
    vkCmdBindIndexBuffer(cmd, mesh->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    m_DrawCalls++;
    m_Triangles += mesh->indexCount / 3;
}

void SceneRenderer::ComputeCascades(const RenderView& rv, const glm::vec3& sunDir, float shadowDistance, SceneUBO& ubo) const
{
    // nearClip may be negative for orthographic cameras whose near plane sits behind the eye.
    const float nearClip = rv.nearClip;
    const float farClip = std::max(rv.farClip, nearClip + 0.01f);
    const float splitNear = std::max(nearClip, 0.05f);
    const float shadowFar = std::min(farClip, std::max(shadowDistance, splitNear + 0.01f));

    // Corners of the camera frustum at the near (ndc z 0) and far (ndc z 1) planes. Points along each
    // corner ray are linear in view depth, which works for both perspective and orthographic cameras.
    const glm::mat4 invViewProj = glm::inverse(rv.proj * rv.view);
    glm::vec3 nearCorners[4], farCorners[4];
    for (int i = 0; i < 4; ++i)
    {
        const glm::vec2 ndc((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f);
        glm::vec4 n = invViewProj * glm::vec4(ndc, 0.0f, 1.0f);
        glm::vec4 f = invViewProj * glm::vec4(ndc, 1.0f, 1.0f);
        nearCorners[i] = glm::vec3(n) / n.w;
        farCorners[i] = glm::vec3(f) / f.w;
    }

    const glm::vec3 lightDir = glm::normalize(sunDir);
    const glm::vec3 up = std::fabs(lightDir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    float prevSplit = nearClip;
    for (uint32_t c = 0; c < kCascades; ++c)
    {
        // Practical split scheme: blend of logarithmic and uniform distribution.
        const float p = static_cast<float>(c + 1) / kCascades;
        const float logSplit = splitNear * std::pow(shadowFar / splitNear, p);
        const float uniSplit = splitNear + (shadowFar - splitNear) * p;
        const float split = glm::mix(uniSplit, logSplit, 0.75f);

        const float t0 = (prevSplit - nearClip) / (farClip - nearClip);
        const float t1 = (split - nearClip) / (farClip - nearClip);
        glm::vec3 pts[8];
        glm::vec3 center(0.0f);
        for (int i = 0; i < 4; ++i)
        {
            pts[i] = glm::mix(nearCorners[i], farCorners[i], t0);
            pts[i + 4] = glm::mix(nearCorners[i], farCorners[i], t1);
            center += pts[i] + pts[i + 4];
        }
        center /= 8.0f;
        float radius = 0.0f;
        for (const glm::vec3& pt : pts) radius = std::max(radius, glm::length(pt - center));
        radius = std::ceil(radius * 16.0f) / 16.0f; // quantize so the projection size doesn't flicker

        // Bounding-sphere fit keeps the cascade size rotation invariant (stable shadows).
        const glm::mat4 lightView = glm::lookAt(center + lightDir, center, up);
        const float casterRange = 200.0f; // include casters between the sun and the cascade
        glm::mat4 lightProj = glm::orthoRH_ZO(-radius, radius, -radius, radius, -(radius + casterRange), radius + 1.0f);

        // Snap the projection to whole shadow-map texels to avoid shimmering when the camera moves.
        glm::mat4 shadowMatrix = lightProj * lightView;
        glm::vec4 origin = shadowMatrix * glm::vec4(0, 0, 0, 1) * (kShadowSize * 0.5f);
        glm::vec4 rounded = glm::round(origin);
        glm::vec2 offset = glm::vec2(rounded - origin) * (2.0f / kShadowSize);
        lightProj[3][0] += offset.x;
        lightProj[3][1] += offset.y;

        ubo.shadowMatrices[c] = lightProj * lightView;
        ubo.cascadeSplits[c] = split;
        ubo.cascadeTexel[c] = 2.0f * radius / kShadowSize;
        prevSplit = split;
    }
}

void SceneRenderer::RenderShadows(VkCommandBuffer cmd, const Scene& scene, VkDescriptorSet uboSet)
{
    const VkPipelineStageFlags2 depthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    // Previous users of the shadow map sampled it in fragment shaders (earlier view or earlier frame).
    VulkanContext::ImageBarrier(cmd, m_ShadowMap.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                                depthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);

    const VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(kShadowSize), static_cast<float>(kShadowSize), 0.0f, 1.0f };
    const VkRect2D scissor{ { 0, 0 }, { kShadowSize, kShadowSize } };
    for (uint32_t c = 0; c < kCascades; ++c)
    {
        VkRenderingAttachmentInfo depth{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        depth.imageView = m_ShadowLayerViews[c];
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
            DrawEntity(cmd, scene, e, DrawMode::DepthOnly, glm::vec4(1.0f), static_cast<float>(c));
        }
        vkCmdEndRendering(cmd);
    }

    VulkanContext::ImageBarrier(cmd, m_ShadowMap.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, depthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
}

void SceneRenderer::RenderSsao(VkCommandBuffer cmd, Target& t, const Scene& scene, VkDescriptorSet uboSet)
{
    const VkPipelineStageFlags2 colorOut = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkPipelineStageFlags2 frag = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    const VkPipelineStageFlags2 depthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    const VkImageAspectFlags color = VK_IMAGE_ASPECT_COLOR_BIT;
    const VkRect2D scissor{ { 0, 0 }, { t.width, t.height } };
    const VkViewport flipped{ 0.0f, static_cast<float>(t.height), static_cast<float>(t.width), -static_cast<float>(t.height), 0.0f, 1.0f };
    const VkViewport straight{ 0.0f, 0.0f, static_cast<float>(t.width), static_cast<float>(t.height), 0.0f, 1.0f };

    auto colorTarget = [](VkImageView view) {
        VkRenderingAttachmentInfo a{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        a.imageView = view;
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.clearValue.color = { { 0.0f, 0.0f, 1.0f, 1.0f } };
        return a;
    };

    // 1) Depth + view-space normal prepass (single sample).
    VulkanContext::ImageBarrier(cmd, t.normals.image, color, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                frag, 0, colorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    VulkanContext::ImageBarrier(cmd, t.prepassDepth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, frag, 0, depthStages,
                                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    {
        VkRenderingAttachmentInfo normals = colorTarget(t.normals.view);
        VkRenderingAttachmentInfo depth{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        depth.imageView = t.prepassDepth.view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil = { 1.0f, 0 };
        VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        info.renderArea = scissor;
        info.layerCount = 1;
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &normals;
        info.pDepthAttachment = &depth;
        vkCmdBeginRendering(cmd, &info);
        vkCmdSetViewport(cmd, 0, 1, &flipped);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        UsePipelines(cmd, m_NormalsPipeline, m_NormalsSkinnedPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SceneLayout, 0, 1, &uboSet, 0, nullptr);
        for (const Entity& e : scene.entities)
        {
            if (!e.meshRenderer.enabled || e.meshRenderer.shadowsOnly || !scene.IsActiveInHierarchy(e.id)) continue;
            DrawEntity(cmd, scene, e, DrawMode::DepthOnly);
        }
        vkCmdEndRendering(cmd);
    }
    VulkanContext::ImageBarrier(cmd, t.normals.image, color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                colorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, frag, VK_ACCESS_2_SHADER_READ_BIT);
    VulkanContext::ImageBarrier(cmd, t.prepassDepth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, depthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                frag, VK_ACCESS_2_SHADER_READ_BIT);

    // 2) Occlusion, then 3) depth-aware blur.
    auto fullscreenPass = [&](GpuImage& target, VkPipeline pipeline, VkDescriptorSet inputs) {
        VulkanContext::ImageBarrier(cmd, target.image, color, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    frag, 0, colorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo out = colorTarget(target.view);
        out.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        info.renderArea = scissor;
        info.layerCount = 1;
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &out;
        vkCmdBeginRendering(cmd, &info);
        vkCmdSetViewport(cmd, 0, 1, &straight);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkDescriptorSet sets[2] = { uboSet, inputs };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PostLayout, 0, 2, sets, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
        VulkanContext::ImageBarrier(cmd, target.image, color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    colorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, frag, VK_ACCESS_2_SHADER_READ_BIT);
        m_DrawCalls++;
    };
    fullscreenPass(t.aoRaw, m_SsaoPipeline, t.ssaoSet);
    fullscreenPass(t.ao, m_BlurPipeline, t.blurSet);
}

void SceneRenderer::Render(VkCommandBuffer cmd, ViewId viewId, const Scene& scene, const RenderView& rv)
{
    const SkySettings& sky = scene.sky;
    Target& t = m_Targets[viewId];
    if (!t.width || !m_MeshPipeline) return;

    // Uniforms
    glm::vec3 sunDir;
    float sunIntensity;
    EntityId sunId = kNullEntity;
    SceneUBO ubo{};
    FillSkyUniforms(scene, rv.time, ubo, sunDir, sunIntensity, sunId);
    ubo.view = rv.view;
    ubo.proj = rv.proj;
    ubo.viewProj = rv.proj * rv.view;
    ubo.invViewProj = glm::inverse(ubo.viewProj);
    ubo.cameraPos = glm::vec4(rv.cameraPos, rv.orthographic ? 1.0f : 0.0f);
    ubo.gridParams = glm::vec4(static_cast<float>(rv.gridPlane), rv.gridOpacity, rv.drawGrid ? 1.0f : 0.0f, 0.0f);
    ubo.viewportSize = glm::vec4(t.width, t.height, 1.0f / t.width, 1.0f / t.height);

    // Point, spot and additional directional lights. Shadowed point/spot lights get layers in the local shadow map.
    int lightCount = 0;
    int shadowLayers = 0;
    m_LocalShadowCount = 0;
    for (const Entity& e : scene.entities)
    {
        if (!e.light.enabled || e.id == sunId || !scene.IsActiveInHierarchy(e.id) || lightCount >= kMaxLights) continue;
        const glm::mat4 world = scene.WorldMatrix(e.id);
        const LightComponent& l = e.light;
        GpuLight& gl = ubo.lights[lightCount++];
        gl.positionRange = glm::vec4(glm::vec3(world[3]), l.range);
        gl.colorIntensity = glm::vec4(l.color, l.intensity);
        gl.directionType = glm::vec4(glm::normalize(glm::vec3(world * glm::vec4(0, 0, -1, 0))), static_cast<float>(l.type));
        const float outer = glm::radians(std::clamp(l.spotAngle, 1.0f, 179.0f)) * 0.5f;
        const float inner = glm::radians(std::clamp(std::min(l.innerSpotAngle, l.spotAngle), 0.0f, 179.0f)) * 0.5f;
        gl.spot = glm::vec4(std::cos(outer), std::max(std::cos(inner), std::cos(outer) + 1e-4f), -1.0f, l.shadowStrength);

        const int needed = l.type == LightType::Point ? 6 : l.type == LightType::Spot ? 1 : 0;
        if (needed && l.castShadows && rv.shadows && m_ShadowPipeline && shadowLayers + needed <= kMaxLocalShadowLayers)
        {
            const glm::vec3 pos(world[3]);
            const float range = std::max(l.range, 0.05f);
            const float nearPlane = std::clamp(range * 0.002f, 0.02f, 0.2f);
            gl.spot.z = static_cast<float>(shadowLayers);
            if (l.type == LightType::Spot)
            {
                const glm::vec3 dir(gl.directionType);
                const glm::vec3 up = std::fabs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
                const float fov = glm::radians(std::clamp(l.spotAngle + 4.0f, 10.0f, 170.0f));
                ubo.localShadowMatrices[shadowLayers] = glm::perspectiveRH_ZO(fov, 1.0f, nearPlane, range) * glm::lookAt(pos, pos + dir, up);
            }
            else
            {
                // Cube faces in the order the shader selects them (+X -X +Y -Y +Z -Z); slightly wider than 90 degrees
                // so filtering near face edges stays inside the map.
                static const glm::vec3 dirs[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
                static const glm::vec3 ups[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };
                const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(95.0f), 1.0f, nearPlane, range);
                for (int f = 0; f < 6; ++f)
                    ubo.localShadowMatrices[shadowLayers + f] = proj * glm::lookAt(pos, pos + dirs[f], ups[f]);
            }
            shadowLayers += needed;
            ++m_LocalShadowCount;
        }
    }
    ubo.lightCount = glm::ivec4(lightCount, 0, 0, 0);

    // Shadows from the sun light.
    const Entity* sunLight = scene.Find(sunId);
    const bool shadows = rv.shadows && sunLight && sunLight->light.castShadows && sky.shadowDistance > 0.0f &&
                         sunDir.y > -0.05f && m_ShadowPipeline;
    if (shadows)
    {
        ComputeCascades(rv, sunDir, sky.shadowDistance, ubo);
        ubo.shadowParams = glm::vec4(sunLight->light.shadowStrength, 1.0f, 0.0f, 0.0f);
    }

    const bool ssao = rv.ssao && sky.ssao && sky.ssaoIntensity > 0.0f && m_SsaoPipeline && rv.shading != ShadingMode::Wireframe;
    const float aoMode = !ssao ? 0.0f : rv.shading == ShadingMode::AmbientOcclusion ? 2.0f : 1.0f; // 2 = debug view
    ubo.aoParams = glm::vec4(aoMode, sky.ssaoIntensity, std::max(sky.ssaoRadius, 0.01f), 0.25f);

    // Reflections: prefiltered sky plus baked reflection probes.
    int probeCount = 0;
    for (const Entity& e : scene.entities)
    {
        if (!e.reflectionProbe.enabled || probeCount >= kMaxProbes || !scene.IsActiveInHierarchy(e.id)) continue;
        auto slot = m_ProbeSlots.find(e.id);
        if (slot == m_ProbeSlots.end()) continue;
        const glm::mat4 world = scene.WorldMatrix(e.id);
        const glm::vec3 pos(world[3]);
        const glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
        const glm::vec3 half = glm::abs(e.reflectionProbe.size * scale) * 0.5f;
        GpuProbe& p = ubo.probes[probeCount++];
        p.centerIndex = glm::vec4(pos, static_cast<float>(slot->second));
        p.boxMin = glm::vec4(pos - half, e.reflectionProbe.intensity);
        p.boxMax = glm::vec4(pos + half, e.reflectionProbe.boxProjection ? 1.0f : 0.0f);
    }
    ubo.envParams = glm::vec4(static_cast<float>(kEnvMips), sky.reflectionIntensity, static_cast<float>(probeCount), 0.0f);

    const uint32_t frame = m_Vk->FrameIndex();
    std::memcpy(t.ubo[frame].mapped, &ubo, sizeof(ubo));
    if (shadows) RenderShadows(cmd, scene, t.uboSet[frame]);
    if (shadowLayers > 0) RenderLocalShadows(cmd, scene, t.uboSet[frame], shadowLayers);
    if (ssao) RenderSsao(cmd, t, scene, t.uboSet[frame]);

    // Layout transitions (previous frame's reads happen in fragment shaders)
    const VkPipelineStageFlags2 colorOut = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkPipelineStageFlags2 fragShader = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    const VkPipelineStageFlags2 depthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    VulkanContext::ImageBarrier(cmd, t.hdr.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                fragShader, 0, colorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
    if (t.hdrMsaa.image)
        VulkanContext::ImageBarrier(cmd, t.hdrMsaa.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    colorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, colorOut,
                                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
    // The previous frame's depth store (even DONT_CARE) is a write, so it must be in the source scope.
    VulkanContext::ImageBarrier(cmd, t.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                depthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, depthStages,
                                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    VulkanContext::ImageBarrier(cmd, t.mask.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                fragShader, 0, colorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    // Flipped viewport keeps OpenGL-style (Y up) clip space conventions.
    VkViewport viewport{ 0.0f, static_cast<float>(t.height), static_cast<float>(t.width), -static_cast<float>(t.height), 0.0f, 1.0f };
    VkRect2D scissor{ { 0, 0 }, { t.width, t.height } };

    // --- Main pass ---
    {
        VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        if (t.hdrMsaa.image)
        {
            // MSAA: render multisampled, resolve (average) into the single-sample HDR target.
            color.imageView = t.hdrMsaa.view;
            color.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            color.resolveImageView = t.hdr.view;
            color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        else
        {
            color.imageView = t.hdr.view;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
        // Fallback color is authored in display space; convert to linear-ish HDR.
        glm::vec3 clear = glm::pow(sky.fallbackColor, glm::vec3(2.2f));
        color.clearValue.color = { { clear.r, clear.g, clear.b, 1.0f } };

        VkRenderingAttachmentInfo depth{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        depth.imageView = t.depth.view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        info.renderArea = scissor;
        info.layerCount = 1;
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &color;
        info.pDepthAttachment = &depth;
        vkCmdBeginRendering(cmd, &info);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SceneLayout, 0, 1, &t.uboSet[frame], 0, nullptr);

        if (sky.enabled && rv.drawSky)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            m_DrawCalls++;
        }

        const bool wire = (rv.shading == ShadingMode::Wireframe || rv.shading == ShadingMode::ShadedWireframe) && m_WirePipeline;
        if (rv.shading != ShadingMode::Wireframe || !m_WirePipeline)
        {
            UsePipelines(cmd, m_MeshPipeline, m_MeshSkinnedPipeline);
            for (const Entity& e : scene.entities)
            {
                if (!e.meshRenderer.enabled || e.meshRenderer.shadowsOnly || !scene.IsActiveInHierarchy(e.id)) continue;
                DrawEntity(cmd, scene, e, DrawMode::Lit);
            }
        }
        if (wire)
        {
            UsePipelines(cmd, m_WirePipeline, m_WireSkinnedPipeline);
            const glm::vec4 wireColor = rv.shading == ShadingMode::Wireframe ? glm::vec4(0.9f, 0.9f, 0.9f, 1.0f)
                                                                               : glm::vec4(0.02f, 0.02f, 0.02f, 1.0f);
            for (const Entity& e : scene.entities)
            {
                if (!e.meshRenderer.enabled || e.meshRenderer.shadowsOnly || !scene.IsActiveInHierarchy(e.id)) continue;
                DrawEntity(cmd, scene, e, DrawMode::Unlit, wireColor);
            }
        }

        if (rv.drawGrid)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GridPipeline);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            m_DrawCalls++;
        }
        vkCmdEndRendering(cmd);
    }

    // --- Selection mask pass ---
    {
        VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        color.imageView = t.mask.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        info.renderArea = scissor;
        info.layerCount = 1;
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &info);
        if (rv.drawOutline && !rv.selection.empty())
        {
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            UsePipelines(cmd, m_MaskPipeline, m_MaskSkinnedPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SceneLayout, 0, 1, &t.uboSet[frame], 0, nullptr);
            for (const Entity& e : scene.entities)
            {
                if (!e.meshRenderer.enabled || !scene.IsActiveInHierarchy(e.id)) continue;
                bool selected = false;
                for (EntityId s : rv.selection)
                    if (s == e.id || scene.IsAncestor(s, e.id)) { selected = true; break; }
                if (selected) DrawEntity(cmd, scene, e, DrawMode::DepthOnly);
            }
        }
        vkCmdEndRendering(cmd);
    }

    VulkanContext::ImageBarrier(cmd, t.hdr.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                fragShader, VK_ACCESS_2_SHADER_READ_BIT);
    VulkanContext::ImageBarrier(cmd, t.mask.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                fragShader, VK_ACCESS_2_SHADER_READ_BIT);
    // --- Post-processing volumes ---
    PostProcessSettings post;
    post.bloomIntensity = 0.0f;
    if (rv.postProcessing && rv.shading != ShadingMode::AmbientOcclusion) post = scene.ResolvePostProcess(rv.cameraPos);
    const bool bloom = post.bloom && post.bloomIntensity > 0.0f && m_BloomDownPipeline;
    if (bloom) RenderBloom(cmd, t, post);

    VulkanContext::ImageBarrier(cmd, t.output.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, fragShader, 0,
                                colorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    // --- Composite: bloom, grading, vignette, tonemap + outline ---
    {
        VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        color.imageView = t.output.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        info.renderArea = scissor;
        info.layerCount = 1;
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &info);
        VkViewport compViewport{ 0.0f, 0.0f, static_cast<float>(t.width), static_cast<float>(t.height), 0.0f, 1.0f };
        vkCmdSetViewport(cmd, 0, 1, &compViewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_CompositePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_CompositeLayoutPipe, 0, 1, &t.compositeSet, 0, nullptr);
        CompositePush push{};
        push.outlineColor = glm::vec4(1.0f, 0.4f, 0.0f, rv.drawOutline ? 1.0f : 0.0f);
        const float exposure = rv.exposure * (post.colorAdjustments ? std::exp2(post.postExposure) : 1.0f);
        push.params = glm::vec4(exposure, 2.0f, static_cast<float>(post.tonemapping ? post.tonemapper : 1), bloom ? post.bloomIntensity : 0.0f);
        push.bloomTint = glm::vec4(post.bloomTint, 0.0f);
        push.grading = glm::vec4(1.0f + post.contrast / 100.0f, 1.0f + post.saturation / 100.0f, post.colorAdjustments ? 1.0f : 0.0f, 0.0f);
        push.colorFilter = glm::vec4(post.colorFilter, 1.0f);
        if (post.whiteBalance)
        {
            // Unity's ColorUtils.ComputeColorBalance: shift the white point along the daylight locus in LMS space.
            const float t1 = post.temperature / 65.0f, t2 = post.tint / 65.0f;
            const float x = 0.31271f - t1 * (t1 < 0.0f ? 0.1f : 0.05f);
            const float y = 2.87f * x - 3.0f * x * x - 0.27509507f + t2 * 0.05f;
            const float X = x / y, Y = 1.0f, Z = (1.0f - x - y) / y;
            const glm::vec3 lms(0.7328f * X + 0.4296f * Y - 0.1624f * Z, -0.7036f * X + 1.6975f * Y + 0.0061f * Z,
                                0.0030f * X + 0.0136f * Y + 0.9834f * Z);
            push.balance = glm::vec4(glm::vec3(0.949237f, 1.03542f, 1.08728f) / lms, 1.0f);
        }
        push.vignette = glm::vec4(post.vignetteIntensity * 3.0f, post.vignetteSmoothness * 5.0f, 1.0f,
                                  post.vignette && post.vignetteIntensity > 0.0f ? 1.0f : 0.0f);
        push.vignetteColor = glm::vec4(post.vignetteColor, 1.0f);
        vkCmdPushConstants(cmd, m_CompositeLayoutPipe, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
    }

    VulkanContext::ImageBarrier(cmd, t.output.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                fragShader, VK_ACCESS_2_SHADER_READ_BIT);
}

bool SceneRenderer::CaptureView(ViewId viewId, const std::string& path)
{
    Target& t = m_Targets[viewId];
    if (!t.width) return false;
    vkDeviceWaitIdle(m_Vk->Device());

    const VkDeviceSize size = static_cast<VkDeviceSize>(t.width) * t.height * 4;
    GpuBuffer staging = m_Vk->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_Vk->ImmediateSubmit([&](VkCommandBuffer cmd) {
        VulkanContext::ImageBarrier(cmd, t.output.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                    VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { t.width, t.height, 1 };
        vkCmdCopyImageToBuffer(cmd, t.output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);
        VulkanContext::ImageBarrier(cmd, t.output.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0,
                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    });

    // 24-bit bottom-up BMP.
    const uint32_t rowSize = (t.width * 3 + 3) & ~3u;
    const uint32_t dataSize = rowSize * t.height;
    std::vector<uint8_t> file(54 + dataSize, 0);
    auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) file[at + i] = static_cast<uint8_t>(v >> (8 * i)); };
    file[0] = 'B';
    file[1] = 'M';
    put32(2, static_cast<uint32_t>(file.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, t.width);
    put32(22, t.height);
    file[26] = 1;
    file[28] = 24;
    put32(34, dataSize);
    const uint8_t* src = static_cast<const uint8_t*>(staging.mapped);
    for (uint32_t y = 0; y < t.height; ++y)
    {
        const uint8_t* row = src + static_cast<size_t>(t.height - 1 - y) * t.width * 4;
        uint8_t* dst = file.data() + 54 + static_cast<size_t>(y) * rowSize;
        for (uint32_t x = 0; x < t.width; ++x)
        {
            dst[x * 3 + 0] = row[x * 4 + 2];
            dst[x * 3 + 1] = row[x * 4 + 1];
            dst[x * 3 + 2] = row[x * 4 + 0];
        }
    }
    m_Vk->DestroyBuffer(staging);

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!out) LOG_ERROR("Failed to write %s", path.c_str());
    else LOG_INFO("Captured %s", path.c_str());
    return static_cast<bool>(out);
}
