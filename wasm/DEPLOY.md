# jdBasic Web IDE - deploy bundle

`wasm/` is a self-contained static site. Upload its contents to any web host
(or `python3 -m http.server`) and open `index.html`. No build step, no CDN,
no server-side code - just static files. The host only needs to send the
correct MIME type for `.wasm` (`application/wasm`); most do.

## Files

Tracked in git (source):

- `index.html` - the IDE (xterm REPL + Monaco editor), references only local paths
- `programs/*.jdb` - web-tuned demo sources (already embedded into the .wasm; not needed at runtime)
- `DEPLOY.md` - this file

Generated / vendored, NOT in git (see `.gitignore`) - assemble before deploying:

- `jdbasic.js`, `jdbasic.wasm` - the runtime (core + GFX + ImGui + SQLite), demos + default font embedded
- `vendor/xterm/` - `xterm.js`, `xterm.css`, `addon-fit.js`
- `vendor/monaco/vs/` - Monaco editor 0.45.0 (`min/vs` tree, includes the worker)
- `vendor/fonts/` - Fira Code woff2 + `fira-code.css`

## (Re)assemble the bundle

1. Build the runtime (on the Emscripten host, e.g. cortex):

       ./build_wasm.sh            # emits build_wasm/jdbasic.{js,wasm}

   Copy `jdbasic.js` and `jdbasic.wasm` into `wasm/`.

2. Vendor the front-end libs:

       mkdir -p wasm/vendor/xterm wasm/vendor/monaco wasm/vendor/fonts
       curl -sL https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/lib/xterm.js        -o wasm/vendor/xterm/xterm.js
       curl -sL https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/css/xterm.css       -o wasm/vendor/xterm/xterm.css
       curl -sL https://cdn.jsdelivr.net/npm/@xterm/addon-fit@0.10.0/lib/addon-fit.js -o wasm/vendor/xterm/addon-fit.js
       curl -sL https://registry.npmjs.org/monaco-editor/-/monaco-editor-0.45.0.tgz -o /tmp/monaco.tgz
       tar -xzf /tmp/monaco.tgz -C /tmp && cp -r /tmp/package/min/vs wasm/vendor/monaco/vs
       for w in 400 500 600 700; do
         curl -sL "https://cdn.jsdelivr.net/fontsource/fonts/fira-code@latest/latin-$w-normal.woff2" -o "wasm/vendor/fonts/fira-code-$w.woff2"
       done

   `vendor/fonts/fira-code.css` (the @font-face block) is tracked in git.

## Notes

- The demos load via the in-page REPL (`DIR`, `LOAD <name>`, `RUN`); they are
  baked into the `.wasm`, so the `programs/` folder does not need uploading.
- Graphics/audio need a user gesture (the page's Run click) before the browser
  starts WebGL/WebAudio - that is automatic in the IDE.
