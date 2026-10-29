// mcp_doc_lookup.js - jdb_doc finds doc/languages.md for a server run from
// the repo's build/ directory, started in a working directory that has no
// doc/ of its own.
//
//   node tests/mcp_doc_lookup.js [path/to/jdBasic.exe]
//
// Exit 0 = found, 1 = not found or no answer, 2 = the binary is missing.
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const repo = path.resolve(__dirname, '..');
const bin = path.resolve(process.argv[2] || path.join(repo, 'build', 'jdBasic.exe'));
if (!fs.existsSync(bin)) { console.log('FAIL: binary not found: ' + bin); process.exit(2); }

const srv = spawn(bin, ['--mcp'], { stdio: 'pipe', windowsHide: true, cwd: os.tmpdir() });
let buf = '';
const timer = setTimeout(() => { console.log('FAIL: no answer'); finish(1); }, 15000);
function send(o) { srv.stdin.write(JSON.stringify(o) + '\n'); }
function finish(code) { clearTimeout(timer); srv.kill(); process.exit(code); }

srv.stdout.on('data', d => {
  buf += d;
  let k;
  while ((k = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, k).trim();
    buf = buf.slice(k + 1);
    if (!line) continue;
    let msg;
    try { msg = JSON.parse(line); } catch (e) { continue; }
    if (msg.id === 1) {
      send({ jsonrpc: '2.0', method: 'notifications/initialized' });
      send({ jsonrpc: '2.0', id: 2, method: 'tools/call', params: { name: 'jdb_doc', arguments: { query: 'DATEADD' } } });
    } else if (msg.id === 2) {
      const r = msg.result || {};
      const text = r.content ? r.content[0].text : JSON.stringify(msg);
      const ok = !r.isError && /DATEADD/i.test(text);
      console.log(ok ? 'ALL MCP DOC TESTS PASSED!' : 'FAIL: ' + text.slice(0, 200));
      finish(ok ? 0 : 1);
    }
  }
});

send({ jsonrpc: '2.0', id: 1, method: 'initialize', params: { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'mcp_doc_lookup', version: '1' } } });
