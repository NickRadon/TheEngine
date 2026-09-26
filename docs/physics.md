# Physics and character movement

## World and components

`PhysicsWorld` in `src/physics/Physics.cpp` hosts Jolt Physics during play mode. The fixed step defaults to 0.02 seconds (50 Hz). A collider without a Rigidbody is static; adding a Rigidbody makes a simulated or kinematic body. Box, sphere, capsule and mesh colliders share the `ColliderComponent` representation. Their dimensions are local and affected by the entity transform. Trigger colliders report overlap events without solid contact.

The Rigidbody exposes mass, drag, angular drag, gravity and kinematic state. The native bridge and `scripting/ScriptCore/Physics.cs` expose velocity, `AddForce`, `AddTorque`, force modes, `Physics.gravity` and `Physics.Raycast`. Collision/trigger enter and exit events are routed to attached scripts. Scene gizmos draw a selected collider for inspection.

## CharacterController

The CharacterController is a separate capsule backed by Jolt `CharacterVirtual`, not a Rigidbody. Its component stores height, radius, center, slope limit and step offset. `CharacterController.Move` accepts a displacement, performs collision-aware movement and returns `CollisionFlags` (`Sides`, `Above`, `Below`). Scripts can read `isGrounded` and velocity. The move path uses Jolt's stair-step and floor-sticking settings. A gameplay script must still apply gravity and decide acceleration, jumping and input behavior.

Use the Inspector to add and size the component, then fetch it from C# with `GetComponent<CharacterController>()`. Avoid enabling a Rigidbody for the same character unless the behavior explicitly calls for both simulation styles. The clean AnimationSetup player has neither a CAS controller nor a required first-person movement script.

## Limits and validation

The scripting API provides raycasts, but this implementation does not claim Unity's full physics query surface, layer-mask behavior or batched raycast jobs. Collision callbacks are delivered in play mode only. A mesh collider needs a valid mesh reference; missing mesh data produces a warning.

The engine self-test checks gravity, fixed updates, resting contact, collision and trigger messages, raycast hits and restoration when play mode ends. Changes to capsule movement should also be checked in a scene with stairs, slopes and ceilings because those paths are not covered by the generic self-test.
