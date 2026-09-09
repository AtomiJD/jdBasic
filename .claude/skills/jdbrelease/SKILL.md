---
name: jdbrelease
description: Cut or refresh a jdBasic Windows release - build the four redistributable packs (core / mcp-native / vibe-game-pack / vb6) with version bump, code-sign with the Certum SimplySign cert, and publish the assets to BOTH GitHub releases (jdbasic-mcp and v1.0.0). Use when Atomi says "make a release", "new release build", "sign and publish", or "update the git release".
---

# jdBasic Release - build, sign, version, publish

Use when **Atomi** wants to cut or refresh a public Windows release. This is the heavy
release ceremony; for a plain dev build use `jdbbuild`, for the test matrix use `jdbgate`.

Working dir: `/d/usr/dev/cc`. Two GitHub releases carry the packs (repo `AtomiJD/jdBasic`):
**`v1.0.0`** is the one marked Latest that users see, **`jdbasic-mcp`** is the pre-release.
Every upload goes to both.
`gh` lives at `/c/Program Files/GitHub CLI/gh.exe` (may also be on PATH).

**Order matters:** build (versions + zips) → **sign** (re-zips + re-hashes) → upload. Do
not upload the zips produced by the build step; the sign step rewrites them.

## 0 - Pre-flight

- Gate must be green first - run `jdbgate`. Don't release red.
- Decide if this is really a release: the `RELEASE` flag **bumps `build_number.txt`**
  (gitignored, lives only on this machine) and embeds build num+date into the binary.
  Only build with RELEASE when actually publishing - see [[feedback_release_only_on_push]].
- For signing, **SimplySign Desktop must be running and logged in** (mobile token), so the
  cloud cert mounts into `Cert:\CurrentUser\My`. A PIN dialog appears during signing. See
  [[reference_code_signing_certum]].

## 1 - Build the packs

Each script wraps `build.bat … RELEASE`, then assembles `release\<bundle>\` + zips it +
writes `<bundle>.zip.sha256`.

| Script                  | build.bat flags                          | Bundle (`release\…`)                | Contents |
|-------------------------|------------------------------------------|-------------------------------------|----------|
| `build_mcp.bat`         | `MCPSERVER HTTP RELEASE`                  | `jdbasic-core-windows-x64`          | core MCP EXE + openssl + doc |
| `build_mcp_native.bat`  | `MCPSERVER HTTP GFX IMGUI NATIVEC RELEASE` | `jdbasic-mcp-native-windows-x64`  | EXE + jdbrt.dll/.lib + LLVM-C + SDL3* + doc (native `-c` toolchain) |
| `build_vibe.bat`        | `GFX IMGUI HTTP MCPSERVER RELEASE`       | `jdbasic-vibe-game-pack-windows-x64`| EXE + SDL3* + game demos |
| `build_vb6.bat`         | `MCPSERVER HTTP GFX IMGUI COM FORMS SQLITE RELEASE` | `jdbasic-vb6-windows-x64` | EXE + SDL3* + forms demos (no NATIVEC, no jdbrt.dll) |

Run them from Bash as `./build_x.bat`, never `cmd //c` - the scripts are LF-only and
cmd mis-parses them into garbage tokens:

```bash
./build_mcp.bat
./build_mcp_native.bat
./build_vibe.bat
./build_vb6.bat
```

Refreshing one pack is normal (a fix that only that pack ships, e.g. a COM fix for the
VB6 pack): build that one script, sign with `-Only`, upload that zip to both releases.

**Version-bump caveat:** each script calls `build.bat RELEASE`, and each RELEASE
increments `build_number.txt`. Running all four bumps the number **four times** - the
*last* build's number is what the binaries carry and what goes in the release notes. If you
want a single bump for the set, build the EXE once with RELEASE and reuse it, or just accept
the multi-bump and read the final `build_number.txt` for the notes. On LNK1104 lock,
`taskkill //F //IM jdBasic.exe` and retry (standing permission).

## 2 - Sign + repackage

`sign_release.ps1` signs ONLY our binaries (the four `jdBasic.exe` + the mcp-native
`jdbrt.dll`) with `signtool /fd SHA256 /tr http://time.certum.pl /td SHA256`, verifies the
chain, then **rebuilds every zip it signed and regenerates `.sha256`** (signed bytes differ from
the build-step zips). Third-party DLLs (SDL3*, LLVM-C, libssl/libcrypto) are vendor-signed -
never re-signed.

```bash
powershell -ExecutionPolicy Bypass -File ./sign_release.ps1
```

**Refreshing only some packs?** Pass `-Only` (substring match against the bundle
names, comma-separated for several) so the others keep the signature and hash
they were published with:

```bash
powershell -ExecutionPolicy Bypass -File ./sign_release.ps1 -Only vibe
powershell -ExecutionPolicy Bypass -File ./sign_release.ps1 -Only vibe,vb6
powershell -ExecutionPolicy Bypass -File ./sign_release.ps1 -Only vibe -WhatIf   # show, sign nothing
```

This matters because signing is not idempotent: a second run gives the binary a
fresh timestamp, which changes the zip bytes and the sha256. Re-signing a pack
you are not republishing silently desyncs the release notes from the asset. An
unknown or ambiguous `-Only` value aborts instead of guessing.

It prints `OK <bundle>.zip  sha256=<hash>` for each - **capture those hashes**, they
go in both release bodies. (Override the cert with `-Thumbprint <40-hex>` if auto-pick grabs
the wrong one.)

## 3 - Publish to BOTH GitHub releases

Upload the freshly **signed** zips + their `.sha256` to `jdbasic-mcp` AND `v1.0.0`,
clobbering the old assets. List every pack you rebuilt (one shown):

```bash
GH="/c/Program Files/GitHub CLI/gh.exe"
for t in jdbasic-mcp v1.0.0; do
  "$GH" release upload $t -R AtomiJD/jdBasic --clobber \
    release/jdbasic-vb6-windows-x64.zip release/jdbasic-vb6-windows-x64.zip.sha256
done
```

**Then update BOTH release bodies** so the SHA256 hashes and the build number in the text
match the just-uploaded zips. This is the step that's easy to forget - Atomi has caught a
stale SHA256 / "(Build N)" in the notes before. The two bodies differ in layout
(`jdbasic-mcp` lists the hash under each asset, `v1.0.0` has a hash table and a
`## Downloads (Windows x64, Build N; VB6 Pack Build M)` heading), so replace the old hash
string and the build phrase with `sed` instead of editing by line number:

```bash
for t in jdbasic-mcp v1.0.0; do
  "$GH" release view $t -R AtomiJD/jdBasic --json body -q .body > /tmp/body_$t.md
  sed -i "s/OLDHASH/NEWHASH/g; s/VB6 Pack Build 83/VB6 Pack Build 84/g" /tmp/body_$t.md
  "$GH" release edit $t -R AtomiJD/jdBasic --notes-file /tmp/body_$t.md
done
```

Verify on both: `"$GH" release view <tag> -R AtomiJD/jdBasic --json assets -q '.assets[].name'`
shows 8 assets (4 zips + 4 sha256), and the body greps for the new hash.

## Notes / gotchas

- The repo's source push is separate and follows the normal rule - **local-first, no push
  without Atomi asking** ([[feedback_commit_workflow_local_first]], [[feedback_no_coauthored_by]]).
  Uploading release *assets* via `gh` is the publish action here; confirm before clobbering.
- Cross-platform release artifacts (Linux/Mac) are a different path - the `release/` dir has
  had `jdbasic-core-linux-*` tarballs built on the farm; see [[jdbfarm]] for building there.
- If `gh` reports auth issues, `"$GH" auth status`; Atomi logs in interactively
  (`! "/c/Program Files/GitHub CLI/gh.exe" auth login`).
