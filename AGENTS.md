# Testing and remote review

- The user reviews this project remotely. When running an engine test, capture its visual result and include the screenshot in the chat response. For AK animation work, run `tools/Test-AkAnimation.ps1` and show `ae_ak_closeup.png` and `ae_ak_comparison.png` (or the individual neutral/aim PNGs), along with the test result and relevant grip metrics. If a check cannot produce a screenshot, say so plainly.
- `tools/Test-AkAnimation.ps1` uses `Assets/AE` from AE Master or `-AeAssets` and writes logs, metrics and PNGs under `build/test-captures/ak` by default. It imports temporary copies; do not write generated materials beside the Unity source FBXs.
- Keep unrelated working-tree changes intact. Do not modify the separate `AnimationSetup` playtest project.
