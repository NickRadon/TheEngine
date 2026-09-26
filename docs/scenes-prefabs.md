# Scenes, hierarchy and prefabs

## Scene model

`Scene` in `src/scene/Scene.h` owns entities with stable numeric IDs, parent IDs, names, active state and components. A Transform stores local position, quaternion rotation, scale and an Euler hint for stable Inspector editing. `Scene::WorldMatrix` resolves the parent hierarchy. Components include meshes, lights, cameras, colliders, rigidbodies, character controllers, animators, sockets, reflection probes, volumes and C# script records.

Scenes are text `.scene` files read and written by `Scene.cpp`. Component records persist only their serialized fields; runtime physics bodies, managed C# instances, animation poses and GPU resources are rebuilt when a scene opens or Play starts. The project's `ProjectSettings/ProjectSettings.txt` selects the last scene. A scene file may be inspected as text, but hand edits should preserve the file format and entity parent IDs.

## Prefab workflow

Dragging a hierarchy object into the Project window creates a `.prefab` containing that entity subtree. Dropping a prefab into the scene instantiates it. Double-clicking opens isolated prefab editing; leaving prefab mode returns to the scene. Instances retain the prefab path and local prefab IDs. The root's name, position and rotation belong to the instance.

`src/scene/Prefab.cpp` tracks overrides by serialized property token and keeps those values during prefab synchronization. The Inspector's Overrides menu shows differences and offers **Apply All** and **Revert All**. Adding or removing children in a prefab propagates to instances while instance-added children and stored overrides are preserved. C# can instantiate a referenced prefab with `Instantiate`.

## Practical checks

After changing a component's serialization, update scene save/load, prefab token ranking and Inspector editing together. Check that old scenes still load when new fields are optional. The self-test exercises scene round-trips, prefab creation, isolated editing, propagation, overrides, Apply/Revert and script instantiation. Prefab edits are real project-asset writes; play-mode scene changes are restored on exit unless explicitly saved outside play mode.
