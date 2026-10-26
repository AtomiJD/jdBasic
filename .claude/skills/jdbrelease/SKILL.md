---
name: jdbrelease
description: Cut or refresh a jdBasic release - build the four Windows packs (core / mcp-native / vibe-game-pack / vb6) plus the three POSIX tarballs on the farm, bump the version, code-sign with the Certum SimplySign cert, and publish to the current GitHub release. Use when Atomi says "make a release", "new release build", "sign and publish", or "update the git release".
---

# jdBasic Release - build, sign, version, publish

Use when **Atomi** wants to cut or refresh a public Windows release. This is the heavy
release ceremony; for a plain dev build use `jdbbuild`, for the test matrix use `jdbgate`.

Working dir: `/d/usr/dev/cc`. **The current release is the only upload target.** Since
1.2 (2026-10-26) that is **`v1.2.0`**, marked Latest; `v1.1.0`, `v1.0.0` and the `jdbasic-mcp`
pre-release are frozen history and carry a "Superseded by" line at the top of their
body. `README.md` and `doc/MCP.md` link `/releases/latest`, so a new Latest needs no
doc edit. Find the target with `"$GH" release list -R AtomiJD/jdBasic` rather than
assuming a tag.
`gh` lives at `/c/Program Files/GitHub CLI/gh.exe` (may also be on PATH).

**A new version number** (1.0 -> 1.1) means one line in `src/version.h`
(`JDBASIC_VERSION`, the single source; `build.bat`/`build.sh` only inject the build
number and date), a commit with `[no-test]`, an annotated tag `vX.Y.0`, and
`"$GH" release create <tag> --title "jdBasic X.Y" --notes-file <file> --latest
--verify-tag`. A refresh of the same version skips all of that and uploads with
`--clobber`.

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
- Build `build_vb6.bat` **last**: it carries the widest flag set, so the binary it leaves
  in `build/` can run the gate afterwards, which is a gate on a real release binary.
  For a version release also run the module library through both backends -
  `for f in tests/jdlibs/*_selftest.jdb` interpreted and again through `-c` - and sum the
  `N passed` lines; that number goes in the notes. Delete the `.exe` files afterwards.

## 1 - Build the packs

Each script wraps `build.bat … RELEASE`, then assembles `release\<bundle>\` + zips it +
writes `<bundle>.zip.sha256`.

| Script                  | build.bat flags                          | Bundle (`release\…`)                | Contents |
|-------------------------|------------------------------------------|-------------------------------------|----------|
| `build_mcp.bat`         | `MCPSERVER HTTP NET RELEASE`              | `jdbasic-core-windows-x64`          | core MCP EXE + openssl + doc |
| `build_mcp_native.bat`  | `MCPSERVER HTTP NET GFX IMGUI NATIVEC RELEASE` | `jdbasic-mcp-native-windows-x64`  | EXE + jdbrt.dll/.lib + LLVM-C + SDL3* + doc (native `-c` toolchain) |
| `build_vibe.bat`        | `GFX IMGUI HTTP NET MCPSERVER SQLITE FX RELEASE` | `jdbasic-vibe-game-pack-windows-x64`| EXE + SDL3* + game demos |
| `build_vb6.bat`         | `MCPSERVER HTTP NET GFX IMGUI COM FORMS SQLITE NATIVEC RELEASE` | `jdbasic-vb6-windows-x64` | EXE + jdbrt.dll/.lib + jdb_runtime.obj + LLVM-C + SDL3* + forms demos (native `-c`, FORMS included) |

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
go in the release body. (Override the cert with `-Thumbprint <40-hex>` if auto-pick grabs
the wrong one.)

Before uploading, check what ships: extract each signed zip into its **own** empty
directory with `Expand-Archive` (the zips carry backslash separators, which Git-Bash
`unzip` turns into literal filenames), run `jdBasic.exe --version` from there and read
back the version, the build number and the feature list, plus
`Get-AuthenticodeSignature`. One PowerShell loop in a `.ps1` file; writing it inline
across `powershell -Command` quoting collapses the per-bundle paths into one directory.

## 3 - The POSIX tarballs (on the farm)

The three non-Windows assets are built on the farm ([[jdbfarm]]) and packed by
`tools/pack_posix.sh`, which stages binary + runtime + `jdb_runtime.o` + `help.txt` +
`doc/` + `lib/` + a generated `README.txt`, tars it and hashes it:

```bash
# per machine: render -> jdbasic-linux-x64, cortex -> jdbasic-linux-x64-cortex,
#              mac    -> jdbasic-macos-arm64   (mac also needs LLVM_CONFIG)
ssh <tgt> 'cd ~/dev/cc && git pull --ff-only origin main -q &&
  BUILD_NUM=<n> HTTP=1 GFX=1 IMGUI=1 NATIVEC=1 ./build.sh 2>&1 | tail -3 &&
  bash tools/pack_posix.sh <bundle-name>'
scp <tgt>:dev/cc/dist/<bundle-name>.tar.gz release/
```

`BUILD_NUM=<n>` is what stamps a POSIX build (`build.sh` has no RELEASE flag); without
it the tarball says "Build 0, dev". Give all three the same number, one above the last
Windows pack. Then verify: unpack each tarball into an empty directory **on its own
machine**, run `--version`, an `IMPORT`, and a `-c` compile from inside it, and re-hash
locally after the `scp` to confirm the transfer.

## 4 - Publish

Upload the freshly **signed** zips and the tarballs with their `.sha256` to the current
release, clobbering the old assets:

```bash
GH="/c/Program Files/GitHub CLI/gh.exe"
"$GH" release upload v1.2.0 -R AtomiJD/jdBasic --clobber \
  release/jdbasic-vb6-windows-x64.zip release/jdbasic-vb6-windows-x64.zip.sha256
```

**Then make the body match the assets** - every SHA256 and every "(Build N)". This is
the step that's easy to forget; Atomi has caught a stale hash in the notes before. For a
refresh, replace the old hash strings with `sed` rather than editing by line number:

```bash
"$GH" release view v1.2.0 -R AtomiJD/jdBasic --json body -q .body > /tmp/body.md
sed -i "s/OLDHASH/NEWHASH/g; s/Build 99/Build 101/g" /tmp/body.md
"$GH" release edit v1.2.0 -R AtomiJD/jdBasic --notes-file /tmp/body.md
```

Verify: `"$GH" release view <tag> -R AtomiJD/jdBasic --json assets -q '.assets[].name'`
shows 14 assets (4 zips + 3 tarballs + 7 sha256), the body greps for every new hash, and
`"$GH" release download <tag> -p '<one asset>*'` into an empty directory hashes to what
the body claims. A size match alone does not prove the upload.

## Notes / gotchas

- The repo's source push is separate and follows the normal rule - **local-first, no push
  without Atomi asking** ([[feedback_commit_workflow_local_first]], [[feedback_no_coauthored_by]]).
  Uploading release *assets* via `gh` is the publish action here; confirm before clobbering.
- The POSIX tarballs are step 3 above, not a separate ceremony: since 1.1 they ship on
  the same release as the Windows zips and are built on the farm ([[jdbfarm]]).
- If `gh` reports auth issues, `"$GH" auth status`; Atomi logs in interactively
  (`! "/c/Program Files/GitHub CLI/gh.exe" auth login`).
