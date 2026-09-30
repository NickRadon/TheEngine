# FPS and animation reference pass

AE Master was inspected as a read-only reference. The relevant sources are
`Assets/AE/Scripts/Movement/Runtime/CharacterMovementSettings.cs` (acceleration,
ground sticking, capped fall speed, and coyote time) and
`Assets/CAS Demo/Scripts/FPS/FPSExampleController.cs` (separate FPS animation and
movement state, with actions taking precedence over sprinting).

## Runtime

Animator triggers now remain visible until every layer has evaluated. A reload
trigger used by the base layer can also start an upper-body layer in the same
update, including a layer currently at zero weight. Only triggers used by a
transition are cleared. The animation self-test covers this behavior.

## AK player template

`tools/templates/AkArmsController.cs` now accelerates and brakes instead of
instantly translating at full speed. With an enabled CharacterController it
uses one collision-aware Move call per update, applies gravity and ground
sticking, and supports Space to jump with coyote time and input buffering.
Horizontal animation state follows achieved movement, so pushing into a wall
does not keep the walk animation running. Older scenes without a capsule retain
horizontal movement, but need a CharacterController and solid colliders for
collision and jumping.

Aiming slows movement and prevents sprinting. Sprint requires forward input.
Reload takes priority over magazine check, inspect, and firing when pressed
together. Actions cannot interrupt an existing exclusive action, including its
outgoing blend. Aim blending uses exponential smoothing.

`tools/Setup-AkArmsProject.ps1` installs the updated template and Jump binding
into a supplied TheEngine project. Newly generated scenes include a player
capsule, ground collider, and a wall for testing. Existing playtest projects
are not automatically rewritten; this pass was tested in
`build/fps-reference-test`. AE Master and AnimationSetup were not modified.

## Validation

Run `tools/Test-AkAnimation.ps1` for the animation regression suite, neutral/aim
captures, and grip metrics. Run `tools/Test-AkArmsProject.ps1 -ProjectRoot <path>`
on a project created with the updated setup for FPS captures, firing/action
checks, jump-and-land verification, and a wall-collision check. Its full set
includes aiming while sprint is held and simultaneous reload/fire/inspect input.

The animation captures validate rig alignment. FPS captures and their playtest
logs separately validate the controller; neither substitutes for subjective
mouse-feel testing. Coyote-time and buffered-jump edge timing are implemented
but not individually exercised by the current capture cases.

On September 30, 2026, the Debug build passed all 70 animation checks and all
22 FPS cases (20 cases in the main run, followed by jump, wall-stop, and a repeat
of reload-priority with the added assertion). The jump camera peaked at 2.497 m
and returned to its 1.650 m standing height. The capsule stopped against the
wall at x = 1.180 m. Maximum grip errors were 6.01e-8 m for neutral and
2.43e-7 m for aim. Captures are under `docs/test-captures/ak` and
`docs/test-captures/fps-reference`.
