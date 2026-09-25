#include "scripting/ScriptEngine.h"

#include "core/Log.h"
#include "core/Platform.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef THEENGINE_HAS_DOTNET
#include <windows.h>
#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>
#endif

namespace fs = std::filesystem;

// Function table handed to C# (must match TheEngine.Internal.NativeApi field order).
struct ScriptNativeApi
{
    void (*Log)(int, const wchar_t*);
    int (*EntityExists)(uint64_t);
    int (*EntityGetName)(uint64_t, wchar_t*, int);
    void (*EntitySetName)(uint64_t, const wchar_t*);
    int (*EntityGetActive)(uint64_t);
    void (*EntitySetActive)(uint64_t, int);
    uint64_t (*EntityFind)(const wchar_t*);
    uint64_t (*EntityCreate)(const wchar_t*, int);
    void (*EntityDestroy)(uint64_t);
    uint64_t (*EntityGetParent)(uint64_t);
    void (*EntitySetParent)(uint64_t, uint64_t);
    void (*TransformGet)(uint64_t, int, float*);
    void (*TransformSet)(uint64_t, int, const float*);
    int (*HasComponent)(uint64_t, int);
    int (*ComponentGet)(uint64_t, int, float*);
    void (*ComponentSet)(uint64_t, int, const float*);
    int (*InputGetKey)(int, int);
    int (*InputGetMouseButton)(int, int);
    void (*InputGetMouse)(float*);
};

// Managed entry points (TheEngine.Internal.ScriptHost, [UnmanagedCallersOnly]).
struct ScriptEngine::Api
{
    int (*Initialize)(ScriptNativeApi*) = nullptr;
    int (*LoadGameAssembly)(const wchar_t*) = nullptr;
    wchar_t* (*GetScriptTypes)() = nullptr;
    void (*FreeString)(wchar_t*) = nullptr;
    int (*CreateInstance)(uint64_t, const wchar_t*, const wchar_t*, int) = nullptr;
    void (*Tick)(float, float, int) = nullptr;
    void (*DestroyEntityInstances)(uint64_t) = nullptr;
    void (*EndPlay)() = nullptr;
    wchar_t* (*GetInstanceFields)(int) = nullptr;
    void (*SetInstanceField)(int, const wchar_t*, const wchar_t*) = nullptr;
    void (*SetInstanceEnabled)(int, int) = nullptr;
};

namespace
{
    ScriptEngine* g_Engine = nullptr;
    ScriptNativeApi g_NativeApi{};
    constexpr wchar_t kTypeSep = L'\x1D', kEntrySep = L'\x1E', kPartSep = L'\x1F';

    Scene* S() { return g_Engine ? g_Engine->GetScene() : nullptr; }
    Entity* E(uint64_t id)
    {
        Scene* s = S();
        return s ? s->Find(static_cast<EntityId>(id)) : nullptr;
    }

    std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep)
    {
        std::vector<std::wstring> parts;
        size_t start = 0;
        while (true)
        {
            size_t pos = s.find(sep, start);
            parts.push_back(s.substr(start, pos == std::wstring::npos ? std::wstring::npos : pos - start));
            if (pos == std::wstring::npos) break;
            start = pos + 1;
        }
        return parts;
    }

    // ---- Native callbacks -------------------------------------------------
    void NLog(int level, const wchar_t* msg)
    {
        std::string text = Platform::Narrow(msg ? msg : L"");
        if (level == 2) LOG_ERROR("%s", text.c_str());
        else if (level == 1) LOG_WARN("%s", text.c_str());
        else LOG_INFO("%s", text.c_str());
    }
    int NEntityExists(uint64_t id) { return E(id) ? 1 : 0; }
    int NEntityGetName(uint64_t id, wchar_t* buffer, int capacity)
    {
        Entity* e = E(id);
        if (!e || capacity <= 0) return 0;
        std::wstring w = Platform::Widen(e->name);
        int n = std::min(static_cast<int>(w.size()), capacity);
        std::memcpy(buffer, w.data(), n * sizeof(wchar_t));
        return n;
    }
    void NEntitySetName(uint64_t id, const wchar_t* name) { if (Entity* e = E(id)) e->name = Platform::Narrow(name); }
    int NEntityGetActive(uint64_t id) { Entity* e = E(id); return e && e->active ? 1 : 0; }
    void NEntitySetActive(uint64_t id, int active) { if (Entity* e = E(id)) e->active = active != 0; }
    uint64_t NEntityFind(const wchar_t* name)
    {
        Scene* s = S();
        if (!s) return 0;
        const std::string n = Platform::Narrow(name);
        for (const Entity& e : s->entities)
            if (e.name == n) return e.id;
        return 0;
    }
    uint64_t NEntityCreate(const wchar_t* name, int primitive)
    {
        Scene* s = S();
        if (!s) return 0;
        Entity& e = s->Create(Platform::Narrow(name));
        if (primitive > 0 && primitive < static_cast<int>(PrimitiveType::Count))
        {
            e.meshRenderer.enabled = true;
            e.meshRenderer.mesh = PrimitiveName(static_cast<PrimitiveType>(primitive));
        }
        return e.id;
    }
    void NEntityDestroy(uint64_t id) { if (g_Engine && E(id)) g_Engine->QueueDestroy(static_cast<EntityId>(id)); }
    uint64_t NEntityGetParent(uint64_t id) { Entity* e = E(id); return e ? e->parent : 0; }
    void NEntitySetParent(uint64_t id, uint64_t parent) { if (Scene* s = S()) s->SetParent(static_cast<EntityId>(id), static_cast<EntityId>(parent)); }

    void NTransformGet(uint64_t id, int channel, float* out)
    {
        Entity* e = E(id);
        if (!e) { std::memset(out, 0, sizeof(float) * 4); return; }
        const Transform& t = e->transform;
        switch (channel)
        {
        case 0: std::memcpy(out, glm::value_ptr(t.position), 12); break;
        case 1: out[0] = t.rotation.x; out[1] = t.rotation.y; out[2] = t.rotation.z; out[3] = t.rotation.w; break;
        case 2: std::memcpy(out, glm::value_ptr(t.scale), 12); break;
        case 3: { glm::vec3 p = glm::vec3(S()->WorldMatrix(e->id)[3]); std::memcpy(out, glm::value_ptr(p), 12); break; }
        case 4:
        {
            glm::vec3 scale, pos, skew;
            glm::vec4 persp;
            glm::quat rot;
            glm::decompose(S()->WorldMatrix(e->id), scale, rot, pos, skew, persp);
            rot = glm::normalize(rot);
            out[0] = rot.x; out[1] = rot.y; out[2] = rot.z; out[3] = rot.w;
            break;
        }
        }
    }

    void NTransformSet(uint64_t id, int channel, const float* in)
    {
        Entity* e = E(id);
        if (!e) return;
        Transform& t = e->transform;
        Scene* s = S();
        switch (channel)
        {
        case 0: t.position = glm::make_vec3(in); break;
        case 1: t.rotation = glm::normalize(glm::quat(in[3], in[0], in[1], in[2])); t.SyncEulerFromRotation(); break;
        case 2: t.scale = glm::make_vec3(in); break;
        case 3:
            if (e->parent) t.position = glm::vec3(glm::inverse(s->WorldMatrix(e->parent)) * glm::vec4(glm::make_vec3(in), 1.0f));
            else t.position = glm::make_vec3(in);
            break;
        case 4:
        {
            glm::quat world = glm::normalize(glm::quat(in[3], in[0], in[1], in[2]));
            if (e->parent)
            {
                glm::vec3 scale, pos, skew;
                glm::vec4 persp;
                glm::quat parentRot;
                glm::decompose(s->WorldMatrix(e->parent), scale, parentRot, pos, skew, persp);
                world = glm::normalize(glm::inverse(glm::normalize(parentRot)) * world);
            }
            t.rotation = world;
            t.SyncEulerFromRotation();
            break;
        }
        }
    }

    int NHasComponent(uint64_t id, int component)
    {
        Entity* e = E(id);
        if (!e) return 0;
        switch (component)
        {
        case 0: return e->meshRenderer.enabled ? 1 : 0;
        case 1: return e->light.enabled ? 1 : 0;
        case 2: return e->camera.enabled ? 1 : 0;
        default: return 0;
        }
    }

    int NComponentGet(uint64_t id, int property, float* out)
    {
        Entity* e = E(id);
        if (!e) return 0;
        switch (property)
        {
        case 0: std::memcpy(out, glm::value_ptr(e->meshRenderer.color), 12); out[3] = 1.0f; break;
        case 1: out[0] = e->meshRenderer.enabled ? 1.0f : 0.0f; break;
        case 2: std::memcpy(out, glm::value_ptr(e->light.color), 12); out[3] = 1.0f; break;
        case 3: out[0] = e->light.intensity; break;
        case 4: out[0] = e->light.range; break;
        case 5: out[0] = e->light.enabled ? 1.0f : 0.0f; break;
        case 6: out[0] = e->camera.fov; break;
        default: return 0;
        }
        return 1;
    }

    void NComponentSet(uint64_t id, int property, const float* in)
    {
        Entity* e = E(id);
        if (!e) return;
        switch (property)
        {
        case 0: e->meshRenderer.color = glm::make_vec3(in); e->meshRenderer.material.clear(); break;
        case 1: e->meshRenderer.enabled = in[0] != 0.0f; break;
        case 2: e->light.color = glm::make_vec3(in); break;
        case 3: e->light.intensity = in[0]; break;
        case 4: e->light.range = in[0]; break;
        case 5: e->light.enabled = in[0] != 0.0f; break;
        case 6: e->camera.fov = in[0]; break;
        }
    }

    int NInputGetKey(int key, int mode)
    {
        if (!g_Engine || key < 0 || key >= ScriptInput::kKeyCount) return 0;
        const ScriptInput& in = g_Engine->Input();
        return (mode == 0 ? in.key[key] : mode == 1 ? in.keyDown[key] : in.keyUp[key]) ? 1 : 0;
    }
    int NInputGetMouseButton(int button, int mode)
    {
        if (!g_Engine || button < 0 || button > 2) return 0;
        const ScriptInput& in = g_Engine->Input();
        return (mode == 0 ? in.mouse[button] : mode == 1 ? in.mouseDown[button] : in.mouseUp[button]) ? 1 : 0;
    }
    void NInputGetMouse(float* out)
    {
        if (!g_Engine) { std::memset(out, 0, sizeof(float) * 5); return; }
        const ScriptInput& in = g_Engine->Input();
        out[0] = in.mouseX; out[1] = in.mouseY; out[2] = in.mouseDX; out[3] = in.mouseDY; out[4] = in.wheel;
    }
}

// ---------------------------------------------------------------------------
// Hosting
// ---------------------------------------------------------------------------
bool ScriptEngine::Init()
{
    g_Engine = this;
    m_Api = new Api();
    m_DotnetPath = Platform::FindOnPath("dotnet");
    m_ScriptCoreDir = (fs::path(Platform::ExecutableDir()) / "Scripting").string();
    if (m_DotnetPath.empty())
    {
        m_Status = "The .NET SDK was not found on PATH; C# scripting is disabled.";
        LOG_WARN("%s", m_Status.c_str());
        return false;
    }
    if (!HostRuntime())
    {
        LOG_WARN("C# scripting disabled: %s", m_Status.c_str());
        return false;
    }
    m_Available = true;
    m_Status = "Ready";
    LOG_INFO("C# scripting ready (.NET hosted, %s)", m_DotnetPath.c_str());
    return true;
}

bool ScriptEngine::HostRuntime()
{
#ifdef THEENGINE_HAS_DOTNET
    const fs::path dll = fs::path(m_ScriptCoreDir) / "TheEngine.ScriptCore.dll";
    const fs::path config = fs::path(m_ScriptCoreDir) / "TheEngine.ScriptCore.runtimeconfig.json";
    if (!fs::exists(dll) || !fs::exists(config))
    {
        m_Status = "TheEngine.ScriptCore.dll not found next to the executable (build the ScriptCore target).";
        return false;
    }

    wchar_t hostfxrPath[MAX_PATH];
    size_t size = MAX_PATH;
    if (get_hostfxr_path(hostfxrPath, &size, nullptr) != 0)
    {
        m_Status = "Could not locate hostfxr (is the .NET runtime installed?)";
        return false;
    }
    HMODULE lib = LoadLibraryW(hostfxrPath);
    if (!lib)
    {
        m_Status = "Could not load hostfxr.dll";
        return false;
    }
    auto initFn = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(GetProcAddress(lib, "hostfxr_initialize_for_runtime_config"));
    auto getDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(GetProcAddress(lib, "hostfxr_get_runtime_delegate"));
    auto closeFn = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(lib, "hostfxr_close"));
    if (!initFn || !getDelegate || !closeFn)
    {
        m_Status = "hostfxr exports missing";
        return false;
    }

    hostfxr_handle handle = nullptr;
    int rc = initFn(config.wstring().c_str(), nullptr, &handle);
    if (rc < 0 || !handle)
    {
        m_Status = "hostfxr_initialize_for_runtime_config failed (" + std::to_string(rc) + ")";
        if (handle) closeFn(handle);
        return false;
    }
    load_assembly_and_get_function_pointer_fn load = nullptr;
    rc = getDelegate(handle, hdt_load_assembly_and_get_function_pointer, reinterpret_cast<void**>(&load));
    closeFn(handle);
    if (rc < 0 || !load)
    {
        m_Status = "Could not get the .NET load delegate";
        return false;
    }

    const std::wstring assembly = dll.wstring();
    const wchar_t* type = L"TheEngine.Internal.ScriptHost, TheEngine.ScriptCore";
    bool ok = true;
    auto get = [&](const wchar_t* method, auto& fn) {
        void* ptr = nullptr;
        int r = load(assembly.c_str(), type, method, UNMANAGEDCALLERSONLY_METHOD, nullptr, &ptr);
        if (r < 0 || !ptr)
        {
            ok = false;
            m_Status = "Missing managed entry point " + Platform::Narrow(method);
        }
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(ptr);
    };
    get(L"Initialize", m_Api->Initialize);
    get(L"LoadGameAssembly", m_Api->LoadGameAssembly);
    get(L"GetScriptTypes", m_Api->GetScriptTypes);
    get(L"FreeString", m_Api->FreeString);
    get(L"CreateInstance", m_Api->CreateInstance);
    get(L"Tick", m_Api->Tick);
    get(L"DestroyEntityInstances", m_Api->DestroyEntityInstances);
    get(L"EndPlay", m_Api->EndPlay);
    get(L"GetInstanceFields", m_Api->GetInstanceFields);
    get(L"SetInstanceField", m_Api->SetInstanceField);
    get(L"SetInstanceEnabled", m_Api->SetInstanceEnabled);
    if (!ok) return false;

    g_NativeApi = {
        NLog, NEntityExists, NEntityGetName, NEntitySetName, NEntityGetActive, NEntitySetActive, NEntityFind,
        NEntityCreate, NEntityDestroy, NEntityGetParent, NEntitySetParent, NTransformGet, NTransformSet,
        NHasComponent, NComponentGet, NComponentSet, NInputGetKey, NInputGetMouseButton, NInputGetMouse,
    };
    return m_Api->Initialize(&g_NativeApi) == 1;
#else
    m_Status = "TheEngine was built without .NET hosting support (nethost not found at build time).";
    return false;
#endif
}

void ScriptEngine::Shutdown()
{
    if (m_Thread.joinable()) m_Thread.join();
    if (m_Playing) EndPlay();
    delete m_Api;
    m_Api = nullptr;
    g_Engine = nullptr;
}

// ---------------------------------------------------------------------------
// Compilation
// ---------------------------------------------------------------------------
bool ScriptEngine::HasCompileErrors() const
{
    for (const CompileMessage& m : m_Messages)
        if (m.error) return true;
    return false;
}

void ScriptEngine::RequestCompile()
{
    if (!m_Available) return;
    if (m_Compiling)
    {
        m_CompileRequested = true; // compile again once the current one finishes
        return;
    }
    if (m_Thread.joinable()) m_Thread.join();

    // Project file that compiles every script under Assets/ against the engine API.
    std::error_code ec;
    fs::create_directories("Library/ScriptProject", ec);
    const std::string scriptCore = (fs::path(m_ScriptCoreDir) / "TheEngine.ScriptCore.dll").string();
    const std::string assets = fs::absolute("Assets").string();
    std::ofstream proj("Library/ScriptProject/Assembly-CSharp.csproj");
    proj << "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
            "  <!-- Generated by TheEngine; do not edit. -->\n"
            "  <PropertyGroup>\n"
            "    <TargetFramework>net10.0</TargetFramework>\n"
            "    <AssemblyName>Assembly-CSharp</AssemblyName>\n"
            "    <Nullable>disable</Nullable>\n"
            "    <ImplicitUsings>disable</ImplicitUsings>\n"
            "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
            "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n"
            "    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>\n"
            "    <DebugType>portable</DebugType>\n"
            "    <NoWarn>CS0169;CS0414;CS0649</NoWarn>\n"
            "  </PropertyGroup>\n"
            "  <ItemGroup>\n"
            "    <Compile Include=\"" << assets << "\\**\\*.cs\" />\n"
            "    <Reference Include=\"TheEngine.ScriptCore\">\n"
            "      <HintPath>" << scriptCore << "</HintPath>\n"
            "      <Private>false</Private>\n"
            "    </Reference>\n"
            "  </ItemGroup>\n"
            "</Project>\n";
    proj.close();

    m_Compiling = true;
    m_CompileDone = false;
    m_CompileRequested = false;
    m_Thread = std::thread([this] { CompileWorker(); });
}

void ScriptEngine::CompileWorker()
{
    const std::string cmd = "\"" + m_DotnetPath + "\" build \"Library/ScriptProject/Assembly-CSharp.csproj\" -c Debug "
                            "-o \"Library/ScriptAssemblies\" --nologo -v q -clp:NoSummary";
    Platform::ProcessResult r = Platform::RunProcess(cmd, fs::current_path().string());
    std::lock_guard lock(m_Mutex);
    m_CompileOutput = r.output;
    m_CompileExitCode = r.exitCode;
    m_CompileDone = true;
}

void ScriptEngine::Update()
{
    if (!m_CompileDone) return;
    if (m_Thread.joinable()) m_Thread.join();
    m_CompileDone = false;
    m_Compiling = false;

    std::string output;
    int exitCode;
    {
        std::lock_guard lock(m_Mutex);
        output = m_CompileOutput;
        exitCode = m_CompileExitCode;
    }

    // Parse MSBuild/csc lines: path(line,col): error CS1002: ; expected [project]
    m_Messages.clear();
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t tag = line.find("): error ");
        bool error = tag != std::string::npos;
        if (!error) tag = line.find("): warning ");
        if (tag == std::string::npos) continue;
        CompileMessage m;
        m.error = error;
        size_t open = line.rfind('(', tag);
        m.file = line.substr(0, open);
        std::sscanf(line.c_str() + open, "(%d,%d)", &m.line, &m.column);
        m.text = line.substr(tag + (error ? 9 : 11));
        size_t bracket = m.text.rfind(" [");
        if (bracket != std::string::npos) m.text.resize(bracket);
        std::error_code ec;
        fs::path rel = fs::relative(m.file, fs::current_path(), ec);
        if (!ec && !rel.empty()) m.file = rel.generic_string();
        // MSBuild prints each diagnostic more than once; keep the first.
        bool duplicate = false;
        for (const CompileMessage& o : m_Messages)
            if (o.file == m.file && o.line == m.line && o.column == m.column && o.text == m.text) duplicate = true;
        if (!duplicate) m_Messages.push_back(m);
    }

    for (const CompileMessage& m : m_Messages)
    {
        if (m.error) LOG_ERROR("%s(%d,%d): %s", m.file.c_str(), m.line, m.column, m.text.c_str());
        else LOG_WARN("%s(%d,%d): %s", m.file.c_str(), m.line, m.column, m.text.c_str());
    }

    if (exitCode == 0 && !HasCompileErrors())
    {
        if (m_Playing)
        {
            m_ReloadPending = true;
            LOG_INFO("Scripts compiled; they will be reloaded when play mode ends");
        }
        else
        {
            LoadAssembly();
        }
    }
    else if (!HasCompileErrors())
    {
        CompileMessage m;
        m.text = "Script compilation failed:\n" + output;
        m_Messages.push_back(m);
        LOG_ERROR("%s", m.text.c_str());
    }
    else
    {
        LOG_ERROR("Script compilation failed. All compiler errors have to be fixed before you can enter play mode!");
    }

    if (m_CompileRequested) RequestCompile();
}

void ScriptEngine::LoadAssembly()
{
    const std::wstring path = fs::absolute("Library/ScriptAssemblies/Assembly-CSharp.dll").wstring();
    int count = m_Api->LoadGameAssembly(path.c_str());
    if (count < 0) return;
    wchar_t* types = m_Api->GetScriptTypes();
    ParseClasses(types ? std::wstring(types) : std::wstring());
    if (types) m_Api->FreeString(types);
    m_Generation++;
    LOG_INFO("Scripts compiled (%d script%s)", count, count == 1 ? "" : "s");
}

void ScriptEngine::ParseClasses(const std::wstring& data)
{
    m_Classes.clear();
    if (data.empty()) return;
    for (const std::wstring& typeEntry : Split(data, kTypeSep))
    {
        auto entries = Split(typeEntry, kEntrySep);
        ScriptClassInfo info;
        info.name = Platform::Narrow(entries[0]);
        for (size_t i = 1; i < entries.size(); ++i)
        {
            auto parts = Split(entries[i], kPartSep);
            if (parts.size() < 3) continue;
            info.fields.push_back({ Platform::Narrow(parts[0]), Platform::Narrow(parts[1]), Platform::Narrow(parts[2]) });
        }
        m_Classes.push_back(info);
    }
}

const ScriptClassInfo* ScriptEngine::FindClass(const std::string& name) const
{
    for (const ScriptClassInfo& c : m_Classes)
        if (c.name == name) return &c;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Play mode
// ---------------------------------------------------------------------------
void ScriptEngine::BeginPlay(Scene* scene)
{
    m_Scene = scene;
    m_Handles.clear();
    m_DestroyQueue.clear();
    m_Input = ScriptInput{};
    if (!m_Available) return;
    m_Playing = true;

    // Copy the ids first: Awake may create entities.
    std::vector<EntityId> ids;
    for (const Entity& e : scene->entities) ids.push_back(e.id);
    for (EntityId id : ids)
    {
        const Entity* e = scene->Find(id);
        if (!e || !scene->IsActiveInHierarchy(id)) continue;
        for (size_t i = 0; i < e->scripts.size(); ++i)
        {
            const ScriptComponent sc = e->scripts[i];
            std::wstring fields;
            for (const ScriptField& f : sc.fields)
            {
                if (!fields.empty()) fields += kEntrySep;
                fields += Platform::Widen(f.name) + kPartSep + Platform::Widen(f.value);
            }
            int handle = m_Api->CreateInstance(id, Platform::Widen(sc.className).c_str(), fields.c_str(), sc.enabled ? 1 : 0);
            if (handle > 0) m_Handles[{ id, i }] = handle;
            e = scene->Find(id); // the scene vector may have grown
            if (!e) break;
        }
    }
    FlushDestroyQueue();
}

void ScriptEngine::Tick(float dt, float time, int frame)
{
    if (!m_Playing) return;
    m_Api->Tick(dt, time, frame);
    FlushDestroyQueue();
}

void ScriptEngine::FlushDestroyQueue()
{
    if (!m_Scene) return;
    for (EntityId id : m_DestroyQueue)
    {
        if (!m_Scene->Find(id)) continue;
        // Destroy script instances on the entity and its children, then the entities themselves.
        std::vector<EntityId> subtree{ id };
        for (const Entity& e : m_Scene->entities)
            if (m_Scene->IsAncestor(id, e.id)) subtree.push_back(e.id);
        for (EntityId s : subtree) m_Api->DestroyEntityInstances(s);
        m_Scene->Destroy(id);
    }
    m_DestroyQueue.clear();
}

void ScriptEngine::EndPlay()
{
    if (m_Playing && m_Api) m_Api->EndPlay();
    m_Playing = false;
    m_Handles.clear();
    m_DestroyQueue.clear();
    if (m_ReloadPending)
    {
        m_ReloadPending = false;
        LoadAssembly();
    }
}

int ScriptEngine::InstanceHandle(EntityId entity, size_t scriptIndex) const
{
    auto it = m_Handles.find({ entity, scriptIndex });
    return it != m_Handles.end() ? it->second : 0;
}

std::map<std::string, std::string> ScriptEngine::InstanceFields(int handle)
{
    std::map<std::string, std::string> result;
    if (!m_Playing || handle <= 0) return result;
    wchar_t* data = m_Api->GetInstanceFields(handle);
    if (!data) return result;
    std::wstring s(data);
    m_Api->FreeString(data);
    if (s.empty()) return result;
    for (const std::wstring& entry : Split(s, kEntrySep))
    {
        size_t sep = entry.find(kPartSep);
        if (sep != std::wstring::npos) result[Platform::Narrow(entry.substr(0, sep))] = Platform::Narrow(entry.substr(sep + 1));
    }
    return result;
}

void ScriptEngine::SetInstanceField(int handle, const std::string& name, const std::string& value)
{
    if (m_Playing && handle > 0) m_Api->SetInstanceField(handle, Platform::Widen(name).c_str(), Platform::Widen(value).c_str());
}

void ScriptEngine::SetInstanceEnabled(int handle, bool enabled)
{
    if (m_Playing && handle > 0) m_Api->SetInstanceEnabled(handle, enabled ? 1 : 0);
}

bool ScriptEngine::WriteScriptTemplate(const std::string& path, const std::string& className)
{
    std::ofstream out(path);
    out << "using TheEngine;\n\n"
           "public class " << className << " : MonoBehaviour\n"
           "{\n"
           "    // Start is called before the first frame update\n"
           "    void Start()\n"
           "    {\n"
           "    }\n\n"
           "    // Update is called once per frame\n"
           "    void Update()\n"
           "    {\n"
           "    }\n"
           "}\n";
    return static_cast<bool>(out);
}
