# TheEngine animation MCP bridge

This local stdio server exposes three narrow tools to Codex:

- `read_ak_state` reads the AK `.rig` and latest animation test report.
- `set_ak_weapon_rotation` changes only the `vb_ak_weapon` pre-look rotation in `tests/assets/AE_AK.rig` (XYZ degrees).
- `run_ak_animation_test` runs `tools/Test-AkAnimation.ps1`, returns metrics and a close-up PNG, and refreshes the review PNGs under `docs/test-captures/ak`.

From `tools/mcp`, install the pinned dependencies with `pnpm install --frozen-lockfile`, then run `node smoke.mjs` to verify the MCP handshake and read tool. `node smoke.mjs --run` runs the animation suite through MCP and verifies that an image is returned. The project-scoped [Codex configuration](../../.codex/config.toml) starts the server automatically for this trusted checkout, with its working directory resolved from the `.codex` folder to the repository root. Restart Codex or open a new task after changing MCP configuration. Reads run without an extra prompt; Codex treats tool calls that write files or run tests as writes.

The bridge does not run arbitrary shell commands or edit AE Master assets. Test screenshots become remote-viewable only after the changed review PNGs are committed and pushed.
