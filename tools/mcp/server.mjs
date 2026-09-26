import { readFile, rename, writeFile } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { McpServer } from '@modelcontextprotocol/server';
import { serveStdio } from '@modelcontextprotocol/server/stdio';
import * as z from 'zod/v4';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const rigPath = join(repo, 'tests/assets/AE_AK.rig');
const summaryPath = join(repo, 'build/test-captures/ak/summary.json');
const closeupPath = join(repo, 'docs/test-captures/ak/latest-closeup.png');
const comparisonPath = join(repo, 'docs/test-captures/ak/latest-comparison.png');
const runnerPath = join(repo, 'tools/Test-AkAnimation.ps1');
const rotationLine = /^prerotate "vb_ak_weapon" "vb_ak_weapon" ([-+\d.eE]+) ([-+\d.eE]+) ([-+\d.eE]+) ([-+\d.eE]+)$/gm;

function textResult(value, isError = false) {
  return { content: [{ type: 'text', text: JSON.stringify(value, null, 2) }], isError };
}

async function readJson(path) {
  try { return JSON.parse((await readFile(path, 'utf8')).replace(/^\uFEFF/, '')); }
  catch (error) { if (error.code === 'ENOENT') return null; throw error; }
}

async function readRig() {
  const rig = await readFile(rigPath, 'utf8');
  const matches = [...rig.matchAll(rotationLine)];
  if (matches.length !== 1) throw new Error('Expected exactly one AK weapon prerotate operation');
  return { rig, match: matches[0], quaternion: matches[0].slice(1).map(Number) };
}

function multiply(a, b) {
  const [ax, ay, az, aw] = a, [bx, by, bz, bw] = b;
  return [aw * bx + ax * bw + ay * bz - az * by,
          aw * by - ax * bz + ay * bw + az * bx,
          aw * bz + ax * by - ay * bx + az * bw,
          aw * bw - ax * bx - ay * by - az * bz];
}

function eulerQuaternion(xDeg, yDeg, zDeg) {
  const half = Math.PI / 360;
  const sx = Math.sin(xDeg * half), cx = Math.cos(xDeg * half);
  const sy = Math.sin(yDeg * half), cy = Math.cos(yDeg * half);
  const sz = Math.sin(zDeg * half), cz = Math.cos(zDeg * half);
  return multiply([0, 0, sz, cz], multiply([0, sy, 0, cy], [sx, 0, 0, cx]));
}

function format(value) {
  const rounded = Number(value.toFixed(7));
  return Object.is(rounded, -0) ? '0' : String(rounded);
}

async function runRunner(skipBuild) {
  return await new Promise((resolveRun, reject) => {
    const args = ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', runnerPath];
    if (skipBuild) args.push('-SkipBuild');
    const process = spawn('powershell.exe', args, { cwd: repo, windowsHide: true });
    let stdout = '', stderr = '';
    process.stdout.setEncoding('utf8');
    process.stderr.setEncoding('utf8');
    process.stdout.on('data', chunk => { stdout += chunk; });
    process.stderr.on('data', chunk => { stderr += chunk; });
    process.once('error', reject);
    process.once('close', code => resolveRun({ exitCode: code, stdout: stdout.trim(), stderr: stderr.trim() }));
  });
}

function createServer() {
  const server = new McpServer({ name: 'theengine-animation', version: '0.1.0' });

  server.registerTool('read_ak_state', {
    description: 'Read the AE AK rig and latest animation-only test measurements in TheEngine.',
    inputSchema: z.object({}),
    annotations: { readOnlyHint: true }
  }, async () => {
    try {
      const { rig, quaternion } = await readRig();
      return textResult({ rigPath, rig, weaponOffsetQuaternionXYZW: quaternion,
        latestTest: await readJson(summaryPath), reviewImages: [closeupPath, comparisonPath] });
    } catch (error) { return textResult({ error: error.message }, true); }
  });

  server.registerTool('set_ak_weapon_rotation', {
    description: 'Set the AK pre-look weapon rotation. Angles are degrees, composed X then Y then Z in the copied weapon frame. Does not edit Unity assets.',
    inputSchema: z.object({
      xDeg: z.number().finite().min(-180).max(180),
      yDeg: z.number().finite().min(-180).max(180),
      zDeg: z.number().finite().min(-180).max(180)
    }),
    annotations: { readOnlyHint: false, destructiveHint: false }
  }, async ({ xDeg, yDeg, zDeg }) => {
    try {
      const { rig, match, quaternion: oldQuaternion } = await readRig();
      const quaternion = eulerQuaternion(xDeg, yDeg, zDeg);
      if (oldQuaternion.every((value, index) => Math.abs(value - quaternion[index]) < 1e-7))
        return textResult({ rigPath, quaternionXYZW: oldQuaternion, unchanged: true });
      const line = `prerotate "vb_ak_weapon" "vb_ak_weapon" ${quaternion.map(format).join(' ')}`;
      const updated = rig.slice(0, match.index) + line + rig.slice(match.index + match[0].length);
      const temporary = `${rigPath}.mcp.tmp`;
      await writeFile(temporary, updated, 'utf8');
      await rename(temporary, rigPath);
      return textResult({ rigPath, oldQuaternionXYZW: oldQuaternion, quaternionXYZW: quaternion,
        eulerDegreesXYZ: { x: xDeg, y: yDeg, z: zDeg }, next: 'Run run_ak_animation_test to validate and capture the new pose.' });
    } catch (error) { return textResult({ error: error.message }, true); }
  });

  server.registerTool('run_ak_animation_test', {
    description: 'Build if needed, run only TheEngine animation tests, and return AK metrics plus the close-up screenshot. Review PNGs are copied into docs/test-captures/ak for a later git commit and push.',
    inputSchema: z.object({ skipBuild: z.boolean().optional().default(false) }),
    annotations: { readOnlyHint: false, destructiveHint: false }
  }, async ({ skipBuild }) => {
    try {
      const run = await runRunner(skipBuild);
      const summary = await readJson(summaryPath);
      const content = [{ type: 'text', text: JSON.stringify({ ...run, summary,
        reviewImages: [closeupPath, comparisonPath],
        note: 'Review PNGs are local until committed and pushed to GitHub.' }, null, 2) }];
      try { content.push({ type: 'image', data: (await readFile(closeupPath)).toString('base64'), mimeType: 'image/png' }); }
      catch (error) { if (error.code !== 'ENOENT') throw error; }
      return { content, isError: run.exitCode !== 0 || !summary || summary.failed > 0 };
    } catch (error) { return textResult({ error: error.message }, true); }
  });

  return server;
}

void serveStdio(createServer);
