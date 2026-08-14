---
name: jdbrelease
description: Cut a jdBasic Windows release - build the three redistributable variants (core / mcp-native / vibe-game-pack) with version bump, code-sign with the Certum SimplySign cert, and publish/refresh the GitHub release assets. Use when Atomi says "make a release", "new release build", "sign and publish", or "update the git release".
---

# jdBasic Release - build, sign, version, publish

Use when **Atomi** wants to cut or refresh a public Windows release. This is the heavy
release ceremony; for a plain dev build use `jdbbuild`, for the test matrix use `jdbgate`.

Working dir: `/d/usr/dev/cc`. GitHub release tag: **`jdbasic-mcp`** (repo `AtomiJD/jdBasic`).
`gh` lives at `/c/Program Files/GitHub CLI/gh.exe` (may also be on PATH).

**Order matters:** build (versions + zips) → **sign** (re-zips + re-hashes) → upload. Do
not upload the zips produced by the build step; the sign step rewrites them.

## 0 - Pre-flight

- Gate must be green first - run `jdbgate`. Don't release red.
- Decide if this is really a release: the `RELEASE` flag **bumps `build_number.txt`**
  (currently tracked there) and embeds build num+date into the binary. Only build with
  RELEASE when actually publishing - see [[feedback_release_only_on_push]].
- For signing, **SimplySign Desktop must be running and logged in** (mobile token), so the
  cloud cert mounts into `Cert:\CurrentUser\My`. A PIN dialog appears during signing. See
  [[reference_code_signing_certum]].

## 1 - Build the three variants

Each script wraps `build.bat … RELEASE`, then assembles `release\<bundle>\` + zips it +
writes `<bundle>.zip.sha256`.

| Script                  | build.bat flags                          | Bundle (`release\…`)                | Contents |
|-------------------------|------------------------------------------|-------------------------------------|----------|
| `build_mcp.bat`         | `MCPSERVER HTTP RELEASE`                  | `jdbasic-core-windows-x64`          | core MCP EXE + openssl + doc |
| `build_mcp_native.bat`  | `MCPSERVER HTTP GFX IMGUI NATIVEC RELEASE` | `jdbasic-mcp-native-windows-x64`  | EXE + jdbrt.dll/.lib + LLVM-C + SDL3* + doc (native `-c` toolchain) |
| `build_vibe.bat`        | `GFX IMGUI HTTP MCPSERVER RELEASE`       | `jdbasic-vibe-game-pack-windows-x64`| EXE + SDL3* + game demos |

```bash
cmd //c build_mcp.bat
cmd //c build_mcp_native.bat
cmd //c build_vibe.bat
```

**Version-bump caveat:** each script calls `build.bat RELEASE`, and each RELEASE
increments `build_number.txt`. Running all three bumps the number **three times** - the
*last* build's number is what the binaries carry and what goes in the release notes. If you
want a single bump for the set, build the EXE once with RELEASE and reuse it, or just accept
the multi-bump and read the final `build_number.txt` for the notes. On LNK1104 lock,
`taskkill //F //IM jdBasic.exe` and retry (standing permission).

## 2 - Sign + repackage

`sign_release.ps1` signs ONLY our binaries (the three `jdBasic.exe` + the mcp-native
`jdbrt.dll`) with `signtool /fd SHA256 /tr http://time.certum.pl /td SHA256`, verifies the
chain, then **rebuilds all three zips and regenerates `.sha256`** (signed bytes differ from
the build-step zips). Third-party DLLs (SDL3*, LLVM-C, libssl/libcrypto) are vendor-signed -
never re-signed.

```bash
powershell -ExecutionPolicy Bypass -File ./sign_release.ps1
```

It prints `OK <bundle>.zip  sha256=<hash>` for each - **capture those three hashes**, they
go in the release notes. (Override the cert with `-Thumbprint <40-hex>` if auto-pick grabs
the wrong one.)

## 3 - Publish to the GitHub release

Upload the freshly **signed** zips + their `.sha256` to the `jdbasic-mcp` release,
clobbering the old assets:

```bash
GH="/c/Program Files/GitHub CLI/gh.exe"
"$GH" release upload jdbasic-mcp -R AtomiJD/jdBasic --clobber \
  release/jdbasic-core-windows-x64.zip          release/jdbasic-core-windows-x64.zip.sha256 \
  release/jdbasic-mcp-native-windows-x64.zip     release/jdbasic-mcp-native-windows-x64.zip.sha256 \
  release/jdbasic-vibe-game-pack-windows-x64.zip release/jdbasic-vibe-game-pack-windows-x64.zip.sha256
```

**Then update the release BODY** so the SHA256 hashes and the build number in the text match
the just-uploaded zips. This is the step that's easy to forget - Atomi has caught a stale
SHA256 / "(Build N)" in the notes before. Pull → edit → push the body:

```bash
"$GH" release view jdbasic-mcp -R AtomiJD/jdBasic --json body -q .body > /tmp/rel_body.md
# edit /tmp/rel_body.md: replace the three sha256 lines + the build number
"$GH" release edit jdbasic-mcp -R AtomiJD/jdBasic --notes-file /tmp/rel_body.md
```

Verify: `"$GH" release view jdbasic-mcp -R AtomiJD/jdBasic` - confirm 6 assets (3 zips + 3
sha256), correct build number, matching hashes.

## Notes / gotchas

- The repo's source push is separate and follows the normal rule - **local-first, no push
  without Atomi asking** ([[feedback_commit_workflow_local_first]], [[feedback_no_coauthored_by]]).
  Uploading release *assets* via `gh` is the publish action here; confirm before clobbering.
- Cross-platform release artifacts (Linux/Mac) are a different path - the `release/` dir has
  had `jdbasic-core-linux-*` tarballs built on the farm; see [[jdbfarm]] for building there.
- If `gh` reports auth issues, `"$GH" auth status`; Atomi logs in interactively
  (`! "/c/Program Files/GitHub CLI/gh.exe" auth login`).
