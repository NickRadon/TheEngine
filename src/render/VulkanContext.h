#pragma once

#include <volk.h>

#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

struct GpuImage
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {};
    uint32_t layers = 1;
};

struct GpuBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

// Owns the Vulkan instance, device, swapchain and per-frame synchronization.
// Uses Vulkan 1.3 dynamic rendering + synchronization2 (no render passes / framebuffers).
class VulkanContext
{
public:
    static constexpr uint32_t kFramesInFlight = 2;

    bool Init(GLFWwindow* window);
    void Shutdown();

    // Returns VK_NULL_HANDLE when the frame must be skipped (minimized / swapchain rebuilt).
    VkCommandBuffer BeginFrame();
    void BeginSwapchainRendering(VkCommandBuffer cmd, const float clearColor[4]);
    void EndSwapchainRendering(VkCommandBuffer cmd);
    void EndFrame();

    uint32_t FrameIndex() const { return m_FrameIndex; }
    void SetVSync(bool enabled);
    bool VSync() const { return m_VSync; }

    // Resource helpers
    // layers > 1 creates a 2D array image whose default view is VK_IMAGE_VIEW_TYPE_2D_ARRAY.
    GpuImage CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                         VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, uint32_t layers = 1, uint32_t mipLevels = 1,
                         bool cube = false); // cube: layers is a multiple of 6, default view is CUBE / CUBE_ARRAY
    VkImageView CreateLayerView(const GpuImage& image, uint32_t layer, VkImageAspectFlags aspect);
    // View of a single layer + mip level (render target into one cube face / mip).
    VkImageView CreateSubView(const GpuImage& image, uint32_t layer, uint32_t mip, VkImageAspectFlags aspect);
    void DestroyImage(GpuImage& image);
    GpuBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    void DestroyBuffer(GpuBuffer& buffer);
    GpuBuffer CreateDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
    void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record);
    VkShaderModule LoadShader(const std::string& name);

    static void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                             VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess); // covers all array layers

    VkInstance Instance() const { return m_Instance; }
    VkPhysicalDevice PhysicalDevice() const { return m_PhysicalDevice; }
    VkDevice Device() const { return m_Device; }
    VkQueue Queue() const { return m_Queue; }
    uint32_t QueueFamily() const { return m_QueueFamily; }
    VkFormat SwapchainFormat() const { return m_SwapchainFormat; }
    // Saves the next presented frame (the whole editor window) as a BMP.
    void RequestScreenshot(const std::string& path) { m_ScreenshotPath = path; }
    uint32_t SwapchainImageCount() const { return static_cast<uint32_t>(m_SwapchainImages.size()); }
    uint32_t MinImageCount() const { return m_MinImageCount; }
    bool SupportsWireframe() const { return m_SupportsWireframe; }
    VkSampleCountFlagBits MaxMsaaSamples() const { return m_MaxMsaa; }
    float MaxAnisotropy() const { return m_MaxAnisotropy; } // 1 = anisotropic filtering unsupported
    const char* DeviceName() const { return m_DeviceName.c_str(); }

private:
    bool CreateInstance();
    bool PickPhysicalDevice();
    bool CreateDevice();
    bool CreateSwapchain();
    void DestroySwapchain();
    void RecreateSwapchain();
    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

    struct Frame
    {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
    };

    GLFWwindow* m_Window = nullptr;
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_Messenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_Queue = VK_NULL_HANDLE;
    uint32_t m_QueueFamily = 0;
    std::string m_DeviceName;
    bool m_SupportsWireframe = false;
    VkSampleCountFlagBits m_MaxMsaa = VK_SAMPLE_COUNT_1_BIT;
    float m_MaxAnisotropy = 1.0f;

    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    VkFormat m_SwapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D m_SwapchainExtent = {};
    std::vector<VkImage> m_SwapchainImages;
    std::vector<VkImageView> m_SwapchainViews;
    std::vector<VkSemaphore> m_RenderFinished; // one per swapchain image
    uint32_t m_MinImageCount = 2;
    bool m_VSync = true;
    bool m_SwapchainDirty = false;

    Frame m_Frames[kFramesInFlight];
    uint32_t m_FrameIndex = 0;
    uint32_t m_ImageIndex = 0;
    std::string m_ScreenshotPath;
    GpuBuffer m_ScreenshotBuffer;
    bool m_ScreenshotRecorded = false;

    VkCommandPool m_UploadPool = VK_NULL_HANDLE;
    VkFence m_UploadFence = VK_NULL_HANDLE;
};
