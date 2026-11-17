// Runs tests/wasm/png_load.jdb in the WASM runtime inside headless Chromium.
// Graphics need a browser (SDL3 opens its window on a canvas), so plain node
// is not enough: this script serves the runtime, the PNG fixtures and a test
// page on 127.0.0.1, starts Chromium on that page, and the page posts the
// program output back. Exit code 0 when the last line reports 0 failed.
//
// Usage (repo root, emsdk node is fine):
//   node tests/wasm/png_load.cjs [build_dir]          default build_dir: build_wasm
// Chromium: $CHROME, else the Playwright headless shell under
// ~/.cache/ms-playwright/chromium_headless_shell-*/.
"use strict";
const http = require("http");
const fs = require("fs");
const path = require("path");
const os = require("os");
const { spawn } = require("child_process");

const repo = path.resolve(__dirname, "..", "..");
const buildDir = path.resolve(process.argv[2] || path.join(repo, "build_wasm"));
const fixtureDir = path.join(repo, "tests", "jdlibs", "fixtures", "img_png");
const program = path.join(__dirname, "png_load.jdb");
const TIMEOUT_MS = 120000;

function findChrome() {
    if (process.env.CHROME) return process.env.CHROME;
    const base = path.join(os.homedir(), ".cache", "ms-playwright");
    if (fs.existsSync(base)) {
        for (const d of fs.readdirSync(base).filter(n => n.startsWith("chromium_headless_shell-")).sort().reverse()) {
            for (const sub of fs.readdirSync(path.join(base, d))) {
                const exe = path.join(base, d, sub, "chrome-headless-shell");
                if (fs.existsSync(exe)) return exe;
            }
        }
    }
    return null;
}

const page = `<!doctype html><html><body><canvas id="canvas"></canvas><script src="/jdbasic.js"></script><script>
(async () => {
  let out = "";
  const report = (text) => fetch("/result", { method: "POST", body: text });
  try {
    const Mod = await createJdBasic({
      canvas: document.getElementById("canvas"),
      jdbTermWrite: (b) => { for (const c of b) out += String.fromCharCode(c); },
      onJdbReady: () => {}
    });
    Mod.FS.mkdir("/assets");
    const names = await (await fetch("/fixtures.json")).json();
    for (const n of names) {
      const r = await fetch("/fixtures/" + n);
      Mod.FS.writeFile("/assets/" + n, new Uint8Array(await r.arrayBuffer()));
    }
    const code = await (await fetch("/png_load.jdb")).text();
    await Mod.ccall("jdb_run", "void", ["string"], [code], { async: true });
    await report(out);
  } catch (e) {
    await report(out + "\\nPAGE ERROR " + e);
  }
})();
</script></body></html>`;

const types = { ".js": "text/javascript", ".wasm": "application/wasm", ".png": "image/png" };

function send(res, file) {
    fs.readFile(file, (err, data) => {
        if (err) { res.writeHead(404); res.end(); return; }
        res.writeHead(200, { "Content-Type": types[path.extname(file)] || "text/plain" });
        res.end(data);
    });
}

const chromeExe = findChrome();
if (!chromeExe) { console.error("no Chromium: set CHROME"); process.exit(2); }
for (const f of ["jdbasic.js", "jdbasic.wasm"]) {
    if (!fs.existsSync(path.join(buildDir, f))) { console.error("missing " + path.join(buildDir, f)); process.exit(2); }
}

let chrome = null;
let timer = null;
const server = http.createServer((req, res) => {
    const url = decodeURIComponent(req.url.split("?")[0]);
    if (url === "/") { res.writeHead(200, { "Content-Type": "text/html" }); res.end(page); return; }
    if (url === "/jdbasic.js" || url === "/jdbasic.wasm") return send(res, path.join(buildDir, url.slice(1)));
    if (url === "/png_load.jdb") return send(res, program);
    if (url === "/fixtures.json") {
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify(fs.readdirSync(fixtureDir).filter(n => n.endsWith(".png"))));
        return;
    }
    if (url.startsWith("/fixtures/")) return send(res, path.join(fixtureDir, path.basename(url)));
    if (url === "/result" && req.method === "POST") {
        let body = "";
        req.on("data", (c) => { body += c; });
        req.on("end", () => {
            res.writeHead(200); res.end();
            process.stdout.write(body.endsWith("\n") ? body : body + "\n");
            const ok = /PNG RESULTS: *\d+ passed, *0 failed/.test(body);
            finish(ok ? 0 : 1);
        });
        return;
    }
    res.writeHead(404); res.end();
});

function finish(code) {
    clearTimeout(timer);
    if (chrome) chrome.kill("SIGKILL");
    server.close();
    process.exit(code);
}

server.listen(0, "127.0.0.1", () => {
    const url = "http://127.0.0.1:" + server.address().port + "/";
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), "jdb-png-"));
    chrome = spawn(chromeExe, [
        "--headless", "--no-sandbox", "--disable-gpu", "--user-data-dir=" + profile,
        "--remote-debugging-port=0", url
    ], { stdio: "ignore" });
    chrome.on("exit", (code) => { if (code !== null) { console.error("Chromium exited " + code); finish(1); } });
    timer = setTimeout(() => { console.error("timeout: no result after " + TIMEOUT_MS + " ms"); finish(1); }, TIMEOUT_MS);
});
