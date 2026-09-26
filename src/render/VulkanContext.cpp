#include "render/VulkanContext.h"

#include "core/Log.h"

#ifdef _WIN32
#include <windows.h> // before GLFW so APIENTRY is defined once
#endif
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#define VK_CHECK(expr)                                                              \
    do {                                                                            \
        VkResult _r = (expr);                                                       \
        if (_r != VK_SUCCESS) { LOG_ERROR("%s failed (VkResult %d)", #expr, _r); }  \
    } while (0)

namespace
{
    VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                 VkDebugUtilsMessageTypeFlagsEXT,
                                                 const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
    {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            LOG_ERROR("[vulkan] %s", data->pMessage);
        else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            LOG_WARN("[vulkan] %s", data->pMessage);
        return VK_FALSE;
    }

    std::filesystem::path ExecutableDir()
    {
#ifdef _WIN32
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return std::filesystem::path(path).parent_path();
#else
        return std::filesystem::current_path();
#endif
    }
}

bool VulkanContext::Init(GLFWwindow* window)
{
    m_Window = window;
    if (volkInitialize() != VK_SUCCESS)
    {
        LOG_ERROR("Vulkan loader not found. Install a GPU driver with Vulkan support.");
        return false;
    }
    if (!CreateInstance()) return false;
    if (glfwCreateWindowSurface(m_Instance, window, nullptr, &m_Surface) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create window surface");
        return false;
    }
    if (!PickPhysicalDevice()) return false;
    if (!CreateDevice()) return false;
    if (!CreateSwapchain()) return false;

    for (auto& frame : m_Frames)
    {
        VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_QueueFamily;
        VK_CHECK(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &frame.pool));

        VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        alloc.commandPool = frame.pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(m_Device, &alloc, &frame.cmd));

        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(m_Device, &fenceInfo, nullptr, &frame.fence));

        VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VK_CHECK(vkCreateSemaphore(m_Device, &semInfo, nullptr, &frame.imageAvailable));
    }

    VkCommandPoolCreateInfo uploadPool{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    uploadPool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    uploadPool.queueFamilyIndex = m_QueueFamily;
    VK_CHECK(vkCreateCommandPool(m_Device, &uploadPool, nullptr, &m_UploadPool));
    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK_CHECK(vkCreateFence(m_Device, &fenceInfo, nullptr, &m_UploadFence));

    LOG_INFO("Vulkan initialized on %s", m_DeviceName.c_str());
    return true;
}

void VulkanContext::Shutdown()
{
    if (!m_Device) return;
    vkDeviceWaitIdle(m_Device);
    for (auto& frame : m_Frames)
    {
        vkDestroyCommandPool(m_Device, frame.pool, nullptr);
        vkDestroyFence(m_Device, frame.fence, nullptr);
        vkDestroySemaphore(m_Device, frame.imageAvailable, nullptr);
    }
    vkDestroyCommandPool(m_Device, m_UploadPool, nullptr);
    vkDestroyFence(m_Device, m_UploadFence, nullptr);
    DestroySwapchain();
    vkDestroyDevice(m_Device, nullptr);
    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    if (m_Messenger) vkDestroyDebugUtilsMessengerEXT(m_Instance, m_Messenger, nullptr);
    vkDestroyInstance(m_Instance, nullptr);
    m_Device = VK_NULL_HANDLE;
}

bool VulkanContext::CreateInstance()
{
    uint32_t version = 0;
    vkEnumerateInstanceVersion(&version);
    if (version < VK_API_VERSION_1_3)
    {
        LOG_ERROR("Vulkan 1.3 is required (loader reports %u.%u)", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version));
        return false;
    }

    uint32_t glfwCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwCount);

    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, available.data());
    bool hasDebugUtils = std::any_of(available.begin(), available.end(),
        [](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0; });

    std::vector<const char*> layers;
#ifndef NDEBUG
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layerProps(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layerProps.data());
    for (const auto& l : layerProps)
        if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            layers.push_back("VK_LAYER_KHRONOS_validation");
    if (layers.empty())
        LOG_WARN("Vulkan validation layer not available (install the Vulkan SDK to enable it)");
#endif
    if (hasDebugUtils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "TheEngine";
    app.pEngineName = "TheEngine";
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    // Debug builds also enable synchronization validation (missing barriers, hazards between passes).
    VkValidationFeatureEnableEXT enables[] = { VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT };
    VkValidationFeaturesEXT features{ VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
    features.enabledValidationFeatureCount = 1;
    features.pEnabledValidationFeatures = enables;
    if (!layers.empty()) info.pNext = &features;
    if (vkCreateInstance(&info, nullptr, &m_Instance) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateInstance failed");
        return false;
    }
    volkLoadInstance(m_Instance);

    if (hasDebugUtils && !layers.empty())
    {
        VkDebugUtilsMessengerCreateInfoEXT dbg{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbg.pfnUserCallback = DebugCallback;
        vkCreateDebugUtilsMessengerEXT(m_Instance, &dbg, nullptr, &m_Messenger);
    }
    return true;
}

bool VulkanContext::PickPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_Instance, &count, devices.data());

    int bestScore = -1;
    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        if (props.apiVersion < VK_API_VERSION_1_3) continue;

        VkPhysicalDeviceVulkan13Features f13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        VkPhysicalDeviceFeatures2 f2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        f2.pNext = &f13;
        vkGetPhysicalDeviceFeatures2(device, &f2);
        if (!f13.dynamicRendering || !f13.synchronization2 || !f13.shaderDemoteToHelperInvocation) continue;

        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &qCount, queues.data());
        int family = -1;
        for (uint32_t i = 0; i < qCount; ++i)
        {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &present);
            if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { family = static_cast<int>(i); break; }
        }
        if (family < 0) continue;

        int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 :
                    props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 100 : 10;
        if (score > bestScore)
        {
            bestScore = score;
            m_PhysicalDevice = device;
            m_QueueFamily = static_cast<uint32_t>(family);
            m_DeviceName = props.deviceName;
            m_SupportsWireframe = f2.features.fillModeNonSolid == VK_TRUE;
            m_MaxAnisotropy = f2.features.samplerAnisotropy ? std::min(8.0f, props.limits.maxSamplerAnisotropy) : 1.0f;
            const VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
            m_MaxMsaa = (counts & VK_SAMPLE_COUNT_8_BIT) ? VK_SAMPLE_COUNT_8_BIT
                      : (counts & VK_SAMPLE_COUNT_4_BIT) ? VK_SAMPLE_COUNT_4_BIT
                      : (counts & VK_SAMPLE_COUNT_2_BIT) ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;
        }
    }
    if (!m_PhysicalDevice)
    {
        LOG_ERROR("No GPU with Vulkan 1.3 (dynamic rendering + synchronization2) found");
        return false;
    }
    return true;
}

bool VulkanContext::CreateDevice()
{
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueInfo.queueFamilyIndex = m_QueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features f13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    f13.shaderDemoteToHelperInvocation = VK_TRUE; // GLSL discard compiles to OpDemoteToHelperInvocation in SPIR-V 1.6
    VkPhysicalDeviceFeatures2 f2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    f2.pNext = &f13;
    f2.features.fillModeNonSolid = m_SupportsWireframe ? VK_TRUE : VK_FALSE;
    f2.features.samplerAnisotropy = m_MaxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    f2.features.imageCubeArray = VK_TRUE; // reflection probes live in one cube map array

    const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo info{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    info.pNext = &f2;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;
    if (vkCreateDevice(m_PhysicalDevice, &info, nullptr, &m_Device) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateDevice failed");
        return false;
    }
    volkLoadDevice(m_Device);
    vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
    return true;
}

bool VulkanContext::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data());
    // ImGui expects a UNORM target (colors are authored in gamma space).
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats)
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }
    m_SwapchainFormat = chosen.format;

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &modeCount, modes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!m_VSync)
    {
        for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) presentMode = m;
        if (presentMode == VK_PRESENT_MODE_FIFO_KHR)
            for (auto m : modes) if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) presentMode = m;
    }

    int w = 0, h = 0;
    glfwGetFramebufferSize(m_Window, &w, &h);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        extent.width = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) return false;
    m_SwapchainExtent = extent;

    m_MinImageCount = std::max(2u, caps.minImageCount);
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    info.surface = m_Surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(m_Device, &info, nullptr, &m_Swapchain) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateSwapchainKHR failed");
        return false;
    }

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &count, nullptr);
    m_SwapchainImages.resize(count);
    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &count, m_SwapchainImages.data());
    m_SwapchainViews.resize(count);
    m_RenderFinished.resize(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view.image = m_SwapchainImages[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = m_SwapchainFormat;
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(m_Device, &view, nullptr, &m_SwapchainViews[i]));
        VkSemaphoreCreateInfo sem{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VK_CHECK(vkCreateSemaphore(m_Device, &sem, nullptr, &m_RenderFinished[i]));
    }
    return true;
}

void VulkanContext::DestroySwapchain()
{
    for (auto v : m_SwapchainViews) vkDestroyImageView(m_Device, v, nullptr);
    for (auto s : m_RenderFinished) vkDestroySemaphore(m_Device, s, nullptr);
    m_SwapchainViews.clear();
    m_RenderFinished.clear();
    m_SwapchainImages.clear();
    if (m_Swapchain) vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
    m_Swapchain = VK_NULL_HANDLE;
}

void VulkanContext::RecreateSwapchain()
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_Window, &w, &h);
    if (w == 0 || h == 0) return; // minimized; try again later
    vkDeviceWaitIdle(m_Device);
    DestroySwapchain();
    if (CreateSwapchain())
        m_SwapchainDirty = false;
}

void VulkanContext::SetVSync(bool enabled)
{
    if (m_VSync == enabled) return;
    m_VSync = enabled;
    m_SwapchainDirty = true;
}

VkCommandBuffer VulkanContext::BeginFrame()
{
    if (m_SwapchainDirty || !m_Swapchain)
    {
        RecreateSwapchain();
        if (m_SwapchainDirty || !m_Swapchain) return VK_NULL_HANDLE;
    }

    Frame& frame = m_Frames[m_FrameIndex];
    vkWaitForFences(m_Device, 1, &frame.fence, VK_TRUE, UINT64_MAX);

    VkResult r = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &m_ImageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
    {
        m_SwapchainDirty = true;
        return VK_NULL_HANDLE;
    }
    if (r == VK_SUBOPTIMAL_KHR) m_SwapchainDirty = true;

    vkResetFences(m_Device, 1, &frame.fence);
    vkResetCommandBuffer(frame.cmd, 0);
    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(frame.cmd, &begin);
    return frame.cmd;
}

void VulkanContext::BeginSwapchainRendering(VkCommandBuffer cmd, const float clearColor[4])
{
    ImageBarrier(cmd, m_SwapchainImages[m_ImageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    color.imageView = m_SwapchainViews[m_ImageIndex];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    std::memcpy(color.clearValue.color.float32, clearColor, sizeof(float) * 4);

    VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    info.renderArea = { { 0, 0 }, m_SwapchainExtent };
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &info);
}

void VulkanContext::EndSwapchainRendering(VkCommandBuffer cmd)
{
    vkCmdEndRendering(cmd);
    ImageBarrier(cmd, m_SwapchainImages[m_ImageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
}

void VulkanContext::EndFrame()
{
    Frame& frame = m_Frames[m_FrameIndex];
    vkEndCommandBuffer(frame.cmd);

    VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    wait.semaphore = frame.imageAvailable;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    signal.semaphore = m_RenderFinished[m_ImageIndex];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cmdInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    cmdInfo.commandBuffer = frame.cmd;

    VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    VK_CHECK(vkQueueSubmit2(m_Queue, 1, &submit, frame.fence));

    VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &m_RenderFinished[m_ImageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &m_Swapchain;
    present.pImageIndices = &m_ImageIndex;
    VkResult r = vkQueuePresentKHR(m_Queue, &present);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) m_SwapchainDirty = true;

    m_FrameIndex = (m_FrameIndex + 1) % kFramesInFlight;
}

uint32_t VulkanContext::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const
{
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    LOG_ERROR("No suitable memory type");
    return 0;
}

GpuImage VulkanContext::CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                                     VkSampleCountFlagBits samples, uint32_t layers, uint32_t mipLevels, bool cube)
{
    GpuImage img;
    img.format = format;
    img.extent = { width, height };
    img.layers = layers;

    VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = { width, height, 1 };
    info.mipLevels = mipLevels;
    info.arrayLayers = layers;
    info.samples = samples;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (cube) info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    VK_CHECK(vkCreateImage(m_Device, &info, nullptr, &img.image));

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_Device, img.image, &req);
    VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(m_Device, &alloc, nullptr, &img.memory));
    vkBindImageMemory(m_Device, img.image, img.memory, 0);

    VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view.image = img.image;
    view.viewType = cube ? (layers > 6 ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE)
                         : layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = { aspect, 0, mipLevels, 0, layers };
    VK_CHECK(vkCreateImageView(m_Device, &view, nullptr, &img.view));
    return img;
}

VkImageView VulkanContext::CreateSubView(const GpuImage& image, uint32_t layer, uint32_t mip, VkImageAspectFlags aspect)
{
    VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view.image = image.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = image.format;
    view.subresourceRange = { aspect, mip, 1, layer, 1 };
    VkImageView result = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(m_Device, &view, nullptr, &result));
    return result;
}

VkImageView VulkanContext::CreateLayerView(const GpuImage& image, uint32_t layer, VkImageAspectFlags aspect)
{
    VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view.image = image.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = image.format;
    view.subresourceRange = { aspect, 0, 1, layer, 1 };
    VkImageView result = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(m_Device, &view, nullptr, &result));
    return result;
}

void VulkanContext::DestroyImage(GpuImage& image)
{
    if (image.view) vkDestroyImageView(m_Device, image.view, nullptr);
    if (image.image) vkDestroyImage(m_Device, image.image, nullptr);
    if (image.memory) vkFreeMemory(m_Device, image.memory, nullptr);
    image = {};
}

GpuBuffer VulkanContext::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
    GpuBuffer buf;
    buf.size = size;
    VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(m_Device, &info, nullptr, &buf.buffer));

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_Device, buf.buffer, &req);
    VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, properties);
    VK_CHECK(vkAllocateMemory(m_Device, &alloc, nullptr, &buf.memory));
    vkBindBufferMemory(m_Device, buf.buffer, buf.memory, 0);

    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        vkMapMemory(m_Device, buf.memory, 0, size, 0, &buf.mapped);
    return buf;
}

void VulkanContext::DestroyBuffer(GpuBuffer& buffer)
{
    if (buffer.mapped) vkUnmapMemory(m_Device, buffer.memory);
    if (buffer.buffer) vkDestroyBuffer(m_Device, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(m_Device, buffer.memory, nullptr);
    buffer = {};
}

GpuBuffer VulkanContext::CreateDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage)
{
    GpuBuffer staging = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(staging.mapped, data, size);
    GpuBuffer buffer = CreateBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy{ 0, 0, size };
        vkCmdCopyBuffer(cmd, staging.buffer, buffer.buffer, 1, &copy);
    });
    DestroyBuffer(staging);
    return buffer;
}

void VulkanContext::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record)
{
    VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc.commandPool = m_UploadPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_Device, &alloc, &cmd);

    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    record(cmd);
    vkEndCommandBuffer(cmd);

    VkCommandBufferSubmitInfo cmdInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    cmdInfo.commandBuffer = cmd;
    VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    vkQueueSubmit2(m_Queue, 1, &submit, m_UploadFence);
    vkWaitForFences(m_Device, 1, &m_UploadFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_Device, 1, &m_UploadFence);
    vkFreeCommandBuffers(m_Device, m_UploadPool, 1, &cmd);
}

VkShaderModule VulkanContext::LoadShader(const std::string& name)
{
    const std::filesystem::path candidates[] = {
        ExecutableDir() / "shaders" / (name + ".spv"),
        std::filesystem::path(ENGINE_SHADER_DIR) / (name + ".spv"),
    };
    for (const auto& path : candidates)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) continue;
        size_t size = static_cast<size_t>(file.tellg());
        std::vector<uint32_t> code((size + 3) / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(code.data()), size);

        VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        info.codeSize = size;
        info.pCode = code.data();
        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(m_Device, &info, nullptr, &module));
        return module;
    }
    LOG_ERROR("Shader not found: %s", name.c_str());
    return VK_NULL_HANDLE;
}

void VulkanContext::ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                                 VkImageLayout oldLayout, VkImageLayout newLayout,
                                 VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                 VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };

    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}
