// TheEngine - a small Unity-style engine/editor built on Vulkan, Dear ImGui, ImGuizmo, ImAnim and .NET.
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Project.h"
#include "editor/Editor.h"
#include "editor/EditorUI.h"
#include "launcher/Launcher.h"
#include "render/Resources.h"
#include "render/SceneRenderer.h"
#include "render/VulkanContext.h"
#include "scripting/ScriptEngine.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <im_anim.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace
{
    Editor* g_Editor = nullptr;

    void GlfwError(int code, const char* message)
    {
        LOG_ERROR("GLFW error %d: %s", code, message);
    }

    void CheckVk(VkResult r)
    {
        if (r != VK_SUCCESS) LOG_ERROR("ImGui Vulkan backend error: VkResult %d", r);
    }

    void OnDrop(GLFWwindow*, int count, const char** paths)
    {
        if (!g_Editor) return;
        std::vector<std::string> files(paths, paths + count);
        g_Editor->OnFilesDropped(files);
    }

    // One frame of UI: ImGui frame setup, the caller's UI, offscreen rendering, then the swapchain pass.
    template <typename DrawUi, typename RenderOffscreen>
    bool RunFrame(VulkanContext& vk, double& lastTime, DrawUi&& drawUi, RenderOffscreen&& renderOffscreen)
    {
        VkCommandBuffer cmd = vk.BeginFrame();
        if (!cmd) return false;
        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - lastTime);
        lastTime = now;

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawUi(dt);
        ImGui::Render();
        renderOffscreen(cmd);

        const float clear[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        vk.BeginSwapchainRendering(cmd, clear);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vk.EndSwapchainRendering(cmd);
        vk.EndFrame();
        return true;
    }
}

int main(int argc, char** argv)
{
    bool selfTest = false;
    std::string captureDir, projectArg;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--selftest") selfTest = true;
        else if (arg == "--capture-dir" && i + 1 < argc) captureDir = argv[++i];
        else if (arg == "--project" && i + 1 < argc) projectArg = argv[++i];
    }

    glfwSetErrorCallback(GlfwError);
    if (!glfwInit()) return 1;
    if (!glfwVulkanSupported())
    {
        LOG_ERROR("GLFW: Vulkan not supported on this system");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_MAXIMIZED, selfTest ? GLFW_FALSE : GLFW_TRUE);
    if (selfTest)
    {
        // Don't steal focus from the user; the test injects input through ImGui only.
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }
    GLFWwindow* window = glfwCreateWindow(1600, 900, "TheEngine Hub", nullptr, nullptr);
    if (!window) return 1;
    glfwSetDropCallback(window, OnDrop);

    VulkanContext vk;
    if (!vk.Init(window))
    {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    static const std::string iniPath = (fs::path(Platform::AppDataDir()) / "EditorLayout.ini").string();
    io.IniFilename = selfTest ? nullptr : iniPath.c_str(); // tests always use the default layout

    float scaleX = 1.0f, scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    EditorUI::ApplyUnityTheme();
    ImGui::GetStyle().ScaleAllSizes(scaleX);
    EditorUI::LoadFonts(scaleX);

    // In self-test mode no OS input callbacks are installed, so real mouse/keyboard input can't interfere.
    ImGui_ImplGlfw_InitForVulkan(window, !selfTest);

    VkFormat swapFormat = vk.SwapchainFormat();
    ImGui_ImplVulkan_InitInfo init = {};
    init.ApiVersion = VK_API_VERSION_1_3;
    init.Instance = vk.Instance();
    init.PhysicalDevice = vk.PhysicalDevice();
    init.Device = vk.Device();
    init.QueueFamily = vk.QueueFamily();
    init.Queue = vk.Queue();
    init.DescriptorPoolSize = 256;
    init.MinImageCount = vk.MinImageCount();
    init.ImageCount = vk.SwapchainImageCount();
    init.UseDynamicRendering = true;
    init.PipelineInfoMain.PipelineRenderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    init.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapFormat;
    init.CheckVkResultFn = CheckVk;
    ImGui_ImplVulkan_Init(&init);

    auto shutdownImGui = [&]() {
        vkDeviceWaitIdle(vk.Device());
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        iam_clip_shutdown();
        iam_pool_clear();
        ImGui::DestroyContext();
        vk.Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
    };

    // ------------------------------------------------------------------
    // Choose a project: command line, self-test scratch project, or the launcher (Hub).
    // ------------------------------------------------------------------
    std::string projectPath = projectArg;
    double lastTime = glfwGetTime();
    if (selfTest)
    {
        projectPath = (fs::temp_directory_path() / "TheEngineSelfTest").string();
        std::error_code ec;
        fs::remove_all(projectPath, ec);
        std::string error;
        if (!Project::Create(projectPath, "SelfTest", Project::Template::Sample3D, error))
        {
            LOG_ERROR("Self test: %s", error.c_str());
            shutdownImGui();
            return 1;
        }
    }
    if (projectPath.empty())
    {
        Launcher launcher;
        while (!glfwWindowShouldClose(window) && !launcher.HasSelection())
        {
            glfwPollEvents();
            if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
            {
                glfwWaitEventsTimeout(0.05);
                continue;
            }
            RunFrame(vk, lastTime, [&](float dt) {
                iam_update_begin_frame();
                launcher.Draw(dt);
            }, [](VkCommandBuffer) {});
        }
        if (!launcher.HasSelection())
        {
            shutdownImGui();
            return 0;
        }
        projectPath = launcher.Selected();
    }

    std::string error;
    if (!Project::Open(projectPath, error))
    {
        LOG_ERROR("%s", error.c_str());
        shutdownImGui();
        return 1;
    }
    LOG_INFO("Opened project %s (%s)", Project::Name().c_str(), Project::Root().c_str());

    // ------------------------------------------------------------------
    // Editor
    // ------------------------------------------------------------------
    ResourceCache resources;
    resources.Init(&vk);
    ScriptEngine scripts;
    scripts.Init();
    SceneRenderer renderer;
    if (!renderer.Init(&vk, &resources))
        LOG_ERROR("Scene renderer failed to initialize (are the compiled shaders next to the executable?)");

    Editor editor;
    g_Editor = &editor;
    editor.Init(&vk, &renderer, &resources, &scripts, window);
    if (selfTest) editor.EnableSelfTest(captureDir);

    while (!glfwWindowShouldClose(window) && !editor.WantsQuit())
    {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
        {
            glfwWaitEventsTimeout(0.05);
            continue;
        }
        RunFrame(vk, lastTime, [&](float dt) { editor.Update(dt); }, [&](VkCommandBuffer cmd) { editor.RenderViews(cmd); });
    }

    const int result = selfTest ? editor.SelfTestFailures() : 0;
    vkDeviceWaitIdle(vk.Device());
    g_Editor = nullptr;
    editor.Shutdown();
    scripts.Shutdown();
    renderer.Shutdown();
    resources.Shutdown();
    shutdownImGui();
    return result;
}
