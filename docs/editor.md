# Editor and validation

## Windows and interaction

`src/editor/Editor.cpp` owns the editor session and play-mode state. The Project window (`EditorAssets.cpp`) shows real files under `Assets/`; the Hierarchy and Inspector edit scene entities and components (`EditorPanels.cpp`). `EditorAnimator.cpp` edits controller layers, states, blend trees, parameters and transitions. `SceneView.cpp` renders Scene and Game views and handles selection, placement and gizmos. `EditorCamera.cpp` implements fly, orbit, pan and framing controls.

The Scene view uses RMB+WASD to fly, Alt+LMB to orbit, MMB to pan and F to frame a selection. Q/W/E/R/T/Y choose view and transform tools. Ctrl+S saves the scene; Ctrl+P starts or stops Play. Selected colliders and other component gizmos appear in the Scene view. Project assets and prefab instances have their own editing paths, so verify which object is selected before saving or applying overrides.

## Play mode and scripting

Play mode builds physics bodies, creates managed scripts and advances animation. Stopping it restores the edit-mode scene. Script errors block entry; the Console reports compiler file and line. The Game view supplies input focus to scripts. The editor periodically scans `Assets/` for changed scripts and resources, then rebuilds or reloads what changed.

## Automated validation

Build with CMake and run `TheEngine.exe --selftest`. The self-test creates a temporary project, drives editor controls through injected ImGui input and returns a failure count. It covers UI manipulation, scene save/load, materials, glTF import, script compilation and callbacks, rigidbody physics, prefab overrides, animator states/blend trees/root motion, volume rendering and error reporting.

For a real-project smoke test, run:

```text
TheEngine.exe --project C:\path\to\Project --playtest 2 --hold W
```

The optional `--press Space@1.5,Mouse0@3` taps keys at specified seconds, `--mouse 60,-20` injects mouse movement and `--capture-dir C:\path\to\captures` writes BMP frames. A passing self-test verifies engine paths, while a project playtest verifies its asset references and scripts. Visual animation, lighting and collision behavior still need inspection in representative scenes.

For the AE AK rig, run `./tools/Test-AkAnimation.ps1` from PowerShell. It finds `Assets/AE` in the AE Master project under your Documents folder, or accepts `-AeAssets C:\path\to\Assets\AE`. It checks that the three required FBX files exist, builds Debug, runs only the animation self-tests (`--selftest-animation`) with captures, and writes PNGs, labeled full-frame and close-up neutral/aim comparisons, `ae_ak_metrics.csv`, `selftest.log`, and `summary.json` to `build/test-captures/ak`. It also copies the two comparisons to `docs/test-captures/ak` for GitHub review after committing and pushing them. Use `-OutputDir` or `-ReviewDir` to choose other folders, `-SkipBuild` when the binary is already current, or `-ProcessOnly` to regenerate the PNGs and report from existing test captures. The test imports temporary copies of the body and weapon FBXs so it does not generate materials beside the AE source assets. The comparison is for visual review; a passing test does not yet mean the separate weapon model and magazine are aligned correctly.
