# mark_tests_ok.ps1 - PostToolUse hook (matcher: Bash)
# When a Bash command ran the gate tests and the output shows no failure,
# touch .claude/.tests_ok - the freshness marker check_commit.ps1 verifies.
$inp = [Console]::In.ReadToEnd() | ConvertFrom-Json
$cmd = $inp.tool_input.command
if ($null -eq $cmd) { exit 0 }
if ($cmd -notmatch 'tests[/\\]gate') { exit 0 }

$out = ""
if ($null -ne $inp.tool_response) {
  try { $out = ($inp.tool_response | ConvertTo-Json -Depth 6 -Compress) } catch { $out = "$($inp.tool_response)" }
}
# Heuristic failure detection - adjust patterns to the gate tests' real output.
# The suites report their score as "0 failed" and "FAIL:     0", both of which
# the patterns below match, so a fully green run could never set the marker.
# Drop the zero counts before scanning; any non-zero count still trips it.
$scan = $out -replace '(?i)\b0\s+failed\b', '' -replace '(?i)\bFAIL\b\s*:?\s*0\b', ''
if ($scan -match '(?i)assert(ion)?\s+failed|FAILED|\bFAIL\b|runtime error|syntax error') { exit 0 }

$proj = $env:CLAUDE_PROJECT_DIR
if (-not $proj) { exit 0 }
$marker = Join-Path $proj ".claude\.tests_ok"
Set-Content -LiteralPath $marker -Value (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
exit 0
