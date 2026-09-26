# C# scripting and input

## Host and lifecycle

`src/scripting/ScriptEngine.cpp` hosts .NET through hostfxr and bridges native entities, components, physics, animation and input. `scripting/ScriptCore` defines the managed API. Project scripts are `.cs` files under `Assets/`; saving one triggers a `dotnet build` into `Library/ScriptAssemblies`. Compilation errors appear with file and line in the Console and prevent Play mode until fixed.

A script derives from `MonoBehaviour` and can use `Awake`, `Start`, `Update`, `LateUpdate`, `FixedUpdate` and `OnDestroy`. `OnAnimatorMove` is dispatched for an animated object's scripts and lets a script apply root motion using `Animator.deltaPosition`/`deltaRotation`. `ScriptHost.cs` discovers lifecycle methods and keeps managed instances attached to native entity IDs. Public and `[SerializeField]` fields are reflected in the Inspector and stored in scene text. Stopping Play restores the edit-mode scene and reloads script changes.

Use `GetComponent<T>()` for supported engine components. A C# type name alone does not create a native component; the corresponding entity must have that component enabled. The scripting API intentionally covers a subset of Unity, so a Unity package cannot be assumed to compile unchanged.

## Reusable data assets

`ScriptableObject` is a typed JSON asset API in `scripting/ScriptCore/ScriptableObject.cs`. `CreateInstance<T>`, `Load<T>` and `Save` read or write paths inside the current project's `Assets/`. `assetPath` records the loaded path. JSON includes public fields and supports case-insensitive property names. The Inspector can reference `.asset` files through serialized fields. This is a native JSON format, not Unity's YAML ScriptableObject serialization or Unity's asset database.

## Input

`Input` exposes keys, mouse buttons and axes while the Game view has focus. The managed `Cursor` API can request None, Locked or Confined lock modes. `InputActionAsset.Load` reads Unity `.inputactions` JSON within `Assets/` and supports named keyboard and mouse bindings, including a four-direction composite through `ReadVector2`. `IsPressed` and `WasPressedThisFrame` query actions. The parser does not provide Unity `PlayerInput` message delivery, processors or a gamepad backend.

For a basic movement script, read input in `Update`, apply displacement through a CharacterController if present, and use `FixedUpdate` for forces on a Rigidbody. Do not multiply a `CharacterController.Move` displacement by delta time twice. For deterministic animation tests, `TheEngine --project <folder> --playtest <seconds>` supports `--hold`, timed `--press`, and `--mouse` injection; see [editor.md](editor.md).

## Change checklist

A new native C# method usually needs a managed declaration (`Native.cs`), a bridge function and API-table entry (`ScriptEngine.cpp`), and a public wrapper in ScriptCore. Changes to serialized field types also need reflection/Inspector handling. Build ScriptCore and run the engine self-test to catch a mismatched bridge or script compilation failure.
