#pragma once

#include "physics/Physics.h"

class AnimationSystem;
#include "scene/Scene.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// C# scripting (Unity-style MonoBehaviours) hosted through .NET's hostfxr.
//  - scripting/ScriptCore is the engine API assembly (TheEngine.ScriptCore.dll, next to the executable)
//  - user scripts in Assets/**/*.cs are compiled by `dotnet build` into Library/ScriptAssemblies
//  - compiled scripts are loaded into a collectible AssemblyLoadContext and reloaded after each compile

struct ScriptFieldInfo
{
    std::string name;
    std::string type; // float, int, bool, string, Vector3, Color
    std::string defaultValue;
};

struct ScriptClassInfo
{
    std::string name;
    std::vector<ScriptFieldInfo> fields;
};

struct CompileMessage
{
    std::string file;
    int line = 0;
    int column = 0;
    bool error = true;
    std::string text;
};

// Input state handed to scripts (filled by the editor from the Game view each frame).
struct ScriptInput
{
    static constexpr int kKeyCount = 330; // Unity KeyCode range used by TheEngine.KeyCode
    bool key[kKeyCount] = {}, keyDown[kKeyCount] = {}, keyUp[kKeyCount] = {};
    bool mouse[5] = {}, mouseDown[5] = {}, mouseUp[5] = {};
    float mouseX = 0, mouseY = 0, mouseDX = 0, mouseDY = 0, wheel = 0;
};

class ScriptEngine
{
public:
    bool Init();
    void Shutdown();
    bool Available() const { return m_Available; }
    const std::string& Status() const { return m_Status; }

    // Compilation (asynchronous). Update() finishes compiles and reloads the scripts on the main thread.
    void RequestCompile();
    void Update();
    bool IsCompiling() const { return m_Compiling; }
    bool HasCompileErrors() const;
    const std::vector<CompileMessage>& Messages() const { return m_Messages; }
    uint64_t Generation() const { return m_Generation; } // increments on every successful reload

    const std::vector<ScriptClassInfo>& Classes() const { return m_Classes; }
    const ScriptClassInfo* FindClass(const std::string& name) const;

    // Play mode
    void BeginPlay(Scene* scene);
    void Tick(float dt, float time, int frame);
    void LateTick();                                              // LateUpdate (after animation)
    bool DispatchAnimatorMove(EntityId entity);                   // OnAnimatorMove; true when a script handled it
    void FixedTick(float fixedDt);                                // FixedUpdate on every script
    void DispatchCollisions(const std::vector<CollisionEvent>& events); // OnCollision*/OnTrigger* messages
    void EndPlay();
    bool Playing() const { return m_Playing; }
    int InstanceHandle(EntityId entity, size_t scriptIndex) const;
    std::map<std::string, std::string> InstanceFields(int handle);
    void SetInstanceField(int handle, const std::string& name, const std::string& value);
    void SetInstanceEnabled(int handle, bool enabled);

    // Context for engine callbacks made by scripts.
    void SetScene(Scene* scene) { m_Scene = scene; }
    void SetPhysics(PhysicsWorld* physics) { m_Physics = physics; }
    PhysicsWorld* GetPhysics() const { return m_Physics; }
    void SetAnimation(AnimationSystem* animation) { m_Animation = animation; }
    AnimationSystem* GetAnimation() const { return m_Animation; }
    Scene* GetScene() const { return m_Scene; }
    ScriptInput& Input() { return m_Input; }
    void QueueDestroy(EntityId id) { m_DestroyQueue.push_back(id); }
    // Cursor.lockState / Cursor.visible requested by scripts (applied by the editor while the Game view has focus).
    int cursorLock = 0;
    bool cursorVisible = true;
    // Creates script instances (Awake) for entities added while playing (Instantiate).
    void CreateInstances(const std::vector<EntityId>& ids);

    static bool WriteScriptTemplate(const std::string& path, const std::string& className);

private:
    bool HostRuntime();
    void CompileWorker();
    void LoadAssembly();
    void ParseClasses(const std::wstring& data);
    void FlushDestroyQueue();

    bool m_Available = false;
    std::string m_Status;
    std::string m_DotnetPath;
    std::string m_ScriptCoreDir;

    // Managed entry points
    struct Api;
    Api* m_Api = nullptr;

    std::vector<ScriptClassInfo> m_Classes;
    std::vector<CompileMessage> m_Messages;
    uint64_t m_Generation = 0;

    std::thread m_Thread;
    std::atomic<bool> m_Compiling{ false };
    std::atomic<bool> m_CompileDone{ false };
    bool m_CompileRequested = false;
    std::mutex m_Mutex;
    std::string m_CompileOutput;
    int m_CompileExitCode = 0;

    Scene* m_Scene = nullptr;
    PhysicsWorld* m_Physics = nullptr;
    AnimationSystem* m_Animation = nullptr;
    ScriptInput m_Input;
    bool m_Playing = false;
    bool m_ReloadPending = false; // compiled during play mode; reload when play mode ends (like Unity)
    std::map<std::pair<EntityId, size_t>, int> m_Handles;
    std::vector<EntityId> m_DestroyQueue;
};
