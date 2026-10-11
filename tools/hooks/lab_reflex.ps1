# SessionStart hook: inject the persistent-lab inventory and recent episode
# wins into the session context. Reads plain files only (no MCP roundtrip):
#   .claude/lab_manifest.txt - one-line inventory, updated on every jdb_savews
#   .claude/episodes.log     - dated one-line wins, appended at milestones
#   .claude/co_usage.log     - usage ledger written by check_co_reward.ps1
# Always exits 0.
$ErrorActionPreference = 'SilentlyContinue'
$proj = $env:CLAUDE_PROJECT_DIR
if (-not $proj) { $proj = 'D:\usr\dev\cc' }

$manifest = ''
$mf = Join-Path $proj '.claude\lab_manifest.txt'
if (Test-Path $mf) { $manifest = (Get-Content $mf -Raw).Trim() }

$uses = 0
$ul = Join-Path $proj '.claude\co_usage.log'
if (Test-Path $ul) { $uses = (Get-Content $ul | Measure-Object -Line).Lines }

Write-Output "[LAB REFLEX] Persistent workshop (MCP VM, LOADWS mylab): $manifest"
Write-Output "[LAB REFLEX] Before writing a jdBasic helper or loading data in chat: check the lab first (jdb_loadws mylab, then jdb_funcs). Extend the lab (LAB_ FUNC + jdb_savews + update .claude/lab_manifest.txt) instead of building per-episode throwaways. Ledger so far: $uses logged lab/co-processor uses."

$el = Join-Path $proj '.claude\episodes.log'
if (Test-Path $el) {
    $tailLines = Get-Content $el -Tail 5
    if ($tailLines) {
        Write-Output "[LAB REFLEX] Recent wins (episodes.log, append your own at milestones):"
        $tailLines | ForEach-Object { Write-Output "  $_" }
    }
}
exit 0
