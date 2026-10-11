# PreToolUse guard for Bash / PowerShell: flag destructive commands and ask
# the user to confirm before they run. Emits a PreToolUse "ask" decision so
# the command is paused for an explicit yes/no instead of running blind.
$ErrorActionPreference = 'Stop'

try {
    $raw = [Console]::In.ReadToEnd()
    if (-not $raw) { exit 0 }
    $in = $raw | ConvertFrom-Json
} catch {
    exit 0
}

$cmd = ''
if ($in.tool_input -and $in.tool_input.command) { $cmd = [string]$in.tool_input.command }
if ([string]::IsNullOrWhiteSpace($cmd)) { exit 0 }

# pattern -> human reason. Scoped to genuinely destructive shapes so normal
# single-file cleanup (rm -f one.png) does not nag.
$rules = @(
    @{ rx = '(?i)\brm\s+(-\S*r\S*|--recursive)';                 why = 'recursive rm (deletes a whole directory tree)' },
    @{ rx = '(?i)\bremove-item\b[^|;]*-recurse';                 why = 'Remove-Item -Recurse (deletes a directory tree)' },
    @{ rx = '(?i)\b(rmdir|rd)\b[^|;]*\s/s';                      why = 'rmdir /s (recursive directory delete)' },
    @{ rx = '(?i)\bdel\b[^|;]*\s/[sq]';                          why = 'del /s or /q (recursive/forced delete)' },
    @{ rx = '(?i)\bgit\s+reset\s+--hard';                        why = 'git reset --hard (discards uncommitted work)' },
    @{ rx = '(?i)\bgit\s+clean\s+-\S*f';                         why = 'git clean -f (deletes untracked files)' },
    @{ rx = '(?i)\bgit\s+push\b[^|;]*(--force|\s-f\b)';          why = 'git push --force (rewrites remote history)' },
    @{ rx = '(?i)\bgit\s+checkout\s+(--\s|\.(\s|$))';            why = 'git checkout -- / . (discards local changes)' },
    @{ rx = '(?i)(format-volume|clear-disk|\bformat\s+[a-z]:|\bmkfs|\bdd\s+if=)'; why = 'disk format / raw write (data loss)' }
)

$hits = @()
foreach ($r in $rules) {
    if ($cmd -match $r.rx) { $hits += $r.why }
}

if ($hits.Count -eq 0) { exit 0 }

$snippet = $cmd
if ($snippet.Length -gt 200) { $snippet = $snippet.Substring(0, 200) + ' ...' }

$reason = "Potentially destructive command detected: " + ($hits -join '; ') +
          ". Confirm this is intended and the target is correct. Prefer deleting" +
          " specific files over recursive/forced deletes. Command: " + $snippet

$out = @{
    hookSpecificOutput = @{
        hookEventName          = 'PreToolUse'
        permissionDecision     = 'ask'
        permissionDecisionReason = $reason
    }
}
$out | ConvertTo-Json -Compress -Depth 5
exit 0
