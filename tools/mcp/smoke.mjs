import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { fileURLToPath } from 'node:url';

const serverPath = fileURLToPath(new URL('./server.mjs', import.meta.url));
const client = new Client({ name: 'theengine-mcp-smoke', version: '0.1.0' });
const transport = new StdioClientTransport({ command: process.execPath, args: [serverPath] });

await client.connect(transport);
try {
  const { tools } = await client.listTools();
  const names = tools.map(tool => tool.name);
  for (const name of ['read_ak_state', 'set_ak_weapon_rotation', 'run_ak_animation_test']) {
    if (!names.includes(name)) throw new Error(`Missing MCP tool: ${name}`);
  }
  const state = await client.callTool({ name: 'read_ak_state', arguments: {} });
  if (state.isError || state.content[0]?.type !== 'text') throw new Error(`read_ak_state failed: ${JSON.stringify(state)}`);
  const details = JSON.parse(state.content[0].text);
  if (details.weaponOffsetQuaternionXYZW?.length !== 4) throw new Error('AK rotation offset missing');
  console.log(`MCP connected: ${names.join(', ')}`);
  console.log(`Current AK offset XYZW: ${details.weaponOffsetQuaternionXYZW.join(', ')}`);

  if (process.argv.includes('--run')) {
    const result = await client.callTool({ name: 'run_ak_animation_test', arguments: { skipBuild: false } });
    const report = JSON.parse(result.content.find(block => block.type === 'text').text);
    if (result.isError || report.summary?.failed !== 0) throw new Error(`Animation test failed: ${report.stdout || report.stderr}`);
    if (!result.content.some(block => block.type === 'image')) throw new Error('AK screenshot missing from MCP result');
    console.log(`Animation test through MCP: ${report.summary.passed} passed, ${report.summary.failed} failed; screenshot returned`);
  }
} finally {
  await client.close();
}
