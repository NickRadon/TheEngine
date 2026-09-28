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
    bool selfTest = false, animationOnly = false;
    float playtest = 0.0f, mouseDX = 0.0f, mouseDY = 0.0f;
    std::vector<int> heldKeys;
    std::vector<std::pair<int, float>> pressedKeys;
    std::string captureDir, projectArg;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--selftest") selfTest = true;
        else if (arg == "--selftest-animation") { selfTest = true; animationOnly = true; }
        else if (arg == "--playtest" && i + 1 < argc) playtest = static_cast<float>(std::atof(argv[++i]));
        else if (arg == "--mouse" && i + 1 < argc) std::sscanf(argv[++i], "%f,%f", &mouseDX, &mouseDY);
        else if ((arg == "--hold" || arg == "--press") && i + 1 < argc)
        {
            // Unity KeyCode names or numbers, comma separated (e.g. W,LeftShift). --press entries take a start time
            // in seconds (e.g. C@1.5,Space@3) and are tapped for 0.1 s so scripts see GetKeyDown/GetKeyUp.
            static const std::pair<const char*, int> names[] = { { "W", 119 }, { "A", 97 }, { "S", 115 }, { "D", 100 }, { "Q", 113 },
                { "E", 101 }, { "C", 99 }, { "F", 102 }, { "I", 105 }, { "J", 106 }, { "K", 107 }, { "L", 108 },
                { "M", 109 }, { "R", 114 }, { "V", 118 }, { "X", 120 }, { "Space", 32 },
                { "LeftShift", 304 }, { "LeftControl", 306 }, { "Mouse0", 323 }, { "Mouse1", 324 }, { "Mouse2", 325 },
                { "Mouse3", 326 }, { "Mouse4", 327 } };
            const bool press = arg == "--press";
            std::string list = argv[++i];
            size_t start = 0;
            while (start <= list.size())
            {
                const size_t comma = list.find(',', start);
                std::string key = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                float at = 0.0f;
                if (const size_t sep = key.find('@'); sep != std::string::npos)
                {
                    at = static_cast<float>(std::atof(key.c_str() + sep + 1));
                    key.resize(sep);
                }
                int code = std::atoi(key.c_str());
                for (auto& [n, c] : names)
                    if (key == n) code = c;
                if (code > 0 && press) pressedKeys.push_back({ code, at });
                else if (code > 0) heldKeys.push_back(code);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
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
    const bool automated = selfTest || playtest > 0.0f;
    glfwWindowHint(GLFW_MAXIMIZED, automated ? GLFW_FALSE : GLFW_TRUE);
    if (automated)
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
    io.IniFilename = automated ? nullptr : iniPath.c_str(); // tests always use the default layout

    float scaleX = 1.0f, scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    EditorUI::ApplyUnityTheme();
    ImGui::GetStyle().ScaleAllSizes(scaleX);
    EditorUI::LoadFonts(scaleX);

    // In self-test mode no OS input callbacks are installed, so real mouse/keyboard input can't interfere.
    ImGui_ImplGlfw_InitForVulkan(window, !automated);

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
    const fs::path startupDirectory = fs::current_path();
    std::string projectPath = projectArg;
    double lastTime = glfwGetTime();
    if (selfTest)
    {
        projectPath = (fs::temp_directory_path() / "TheEngineSelfTest").string();
        std::error_code ec;
        fs::remove_all(projectPath, ec);
        std::string error;
        const Project::Template testTemplate = animationOnly ? Project::Template::Empty3D : Project::Template::Sample3D;
        if (!Project::Create(projectPath, animationOnly ? "AnimationTest" : "SelfTest", testTemplate, error))
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
    PhysicsWorld::GlobalInit();
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
    if (selfTest) editor.EnableSelfTest(captureDir, animationOnly);
    if (playtest > 0.0f) editor.EnablePlaytest(playtest, heldKeys, pressedKeys, captureDir);
    if (playtest > 0.0f) editor.SetPlaytestMouse(mouseDX, mouseDY);

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
    PhysicsWorld::GlobalShutdown();
    shutdownImGui();
    if (selfTest)
    {
        // Self-tests use a scratch project, not a user project. Leave neither a launcher entry
        // nor a second project folder behind after the run.
        Project::RemoveRecent(projectPath);
        std::error_code ec;
        fs::current_path(startupDirectory, ec);
        ec.clear();
        fs::remove_all(projectPath, ec);
        if (ec) LOG_WARN("Could not remove self-test project %s: %s", projectPath.c_str(), ec.message().c_str());
    }
    return result;
}
