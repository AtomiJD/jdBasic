// mcp_exec_stdin.js - OS.EXEC inside jdb_eval under the stdio MCP server.
//
// Spawns jdbasic --mcp the way Claude Code does (node pipes) and runs three
// evals without any further message in between: git with an argument array,
// a child that reads stdin (findstr), and git through the command-line path.
// Each must answer within the deadline; the stdin reader must see EOF.
//
//   node tests/mcp_exec_stdin.js [path/to/jdBasic.exe]
//
// Exit 0 = all green, 1 = a call failed or hung, 2 = the binary is missing.
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

const repo = path.resolve(__dirname, '..');
const bin = process.argv[2] || path.join(repo, 'build', 'jdBasic.exe');
const DEADLINE_MS = 15000;
if (!fs.existsSync(bin)) { console.log('FAIL: binary not found: ' + bin); process.exit(2); }

const git_dir = repo.replace(/\\/g, '/');
const cases = [
  { name: 'git args', code: `DIM r = OS.EXEC("git", ["-C", "${git_dir}", "log", "-20", "--numstat"]) : PRINT r{"exit_code"}`, want: /^0\s*$/ },
  { name: 'stdin reader', code: `DIM r2 = OS.EXEC("findstr", ["x"]) : PRINT r2{"exit_code"}`, want: /^1\s*$/ },
  { name: 'git string', code: `DIM r3 = OS.EXEC("git -C ${git_dir} log -20 --numstat") : PRINT LEN(r3{"output"}) > 0`, want: /^(1|-1|TRUE)\s*$/i },
];

const srv = spawn(bin, ['--mcp'], { stdio: 'pipe', windowsHide: true });
let buf = '', idx = 0, t0 = 0, failed = 0, timer = null;

function send(o) { srv.stdin.write(JSON.stringify(o) + '\n'); }
function finish(code) { clearTimeout(timer); srv.kill(); process.exit(code); }
function arm() {
  clearTimeout(timer);
  timer = setTimeout(() => { console.log(`FAIL: ${idx ? cases[idx - 1].name : "initialize"} - no answer in ${DEADLINE_MS} ms`); finish(1); }, DEADLINE_MS);
}
function next() {
  if (idx >= cases.length) { console.log(failed ? `RESULTS: ${cases.length - failed} passed, ${failed} failed` : 'ALL MCP EXEC TESTS PASSED!'); finish(failed ? 1 : 0); }
  t0 = Date.now();
  send({ jsonrpc: '2.0', id: 10 + idx, method: 'tools/call', params: { name: 'jdb_eval', arguments: { code: cases[idx].code } } });
  idx++;
  arm();
}

srv.stdout.on('data', d => {
  buf += d;
  let k;
  while ((k = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, k).trim();
    buf = buf.slice(k + 1);
    if (!line) continue;
    let msg;
    try { msg = JSON.parse(line); } catch (e) { continue; }
    if (msg.id === 1) { send({ jsonrpc: '2.0', method: 'notifications/initialized' }); next(); }
    else if (msg.id >= 10) {
      const c = cases[msg.id - 10];
      const text = msg.result && msg.result.content ? msg.result.content[0].text : JSON.stringify(msg);
      const ok = c.want.test(text);
      if (!ok) failed++;
      console.log(`${ok ? 'PASS' : 'FAIL'}: ${c.name} (${Date.now() - t0} ms) ${JSON.stringify(text)}`);
      next();
    }
  }
});
srv.on("exit", code => { console.log("FAIL: server exited with " + code); finish(1); });

send({ jsonrpc: '2.0', id: 1, method: 'initialize', params: { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'mcp_exec_stdin', version: '1' } } });
arm();
