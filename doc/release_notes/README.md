# Release notes

The source of truth for what went into which jdBasic release. The release
ceremony (`jdbrelease` skill) reads and writes this folder.

| File | Content |
|---|---|
| `INDEX.md` | one row per release: tag, date, builds, commit range, tickets |
| `unreleased.md` | what has landed since the last release, curated as it lands |
| `vX.Y.Z.md` | the notes of one release, frozen when it is published |

## Flow

1. **While working**: a change worth a release note gets a line in
   `unreleased.md` under its heading (New, Changed, Fixed), with the trakr
   ticket and the commit. Facts only.
2. **At the release build**: `unreleased.md` becomes `vX.Y.Z.md`, the
   GitHub release body is built from it (plus downloads and hashes), a new
   row goes into `INDEX.md` with the commit range `<previous tag>..<new tag>`
   and the asset build numbers, and `unreleased.md` starts empty again.
3. **Asset refresh of the same version**: append a "Refresh, Build N" section
   to `vX.Y.Z.md` and extend the INDEX row; the range grows to the refresh
   commit.

"What went into release X" is answered by the INDEX range
(`git log <range>`) and the ticket list in the row.
