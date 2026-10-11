# check_co_reward.ps1 - PostToolUse hook (matcher: mcp__jdbasic-stdio-win__(jdb_eval|jdb_loadws))
# Makes the invisible reward visible: after a co-processor or workshop call,
# inject a one-line, quantified feedback into Claude's context via
# additionalContext. Estimates are heuristic (tokens ~ chars/4) and labeled as such.
$inp = [Console]::In.ReadToEnd() | ConvertFrom-Json

$respText = ""
if ($null -ne $inp.tool_response) {
  try { $respText = ($inp.tool_response | ConvertTo-Json -Depth 8 -Compress) } catch { $respText = "$($inp.tool_response)" }
}
$respTokens = [math]::Max(1, [math]::Round($respText.Length / 4))

$msg = $null
$kind = $null

if ($inp.tool_name -match 'jdb_loadws') {
  $kind = "LOADWS"
  $msg = "[workshop reward] Workspace restored: helpers and data built in PREVIOUS sessions are live again at zero rebuild cost. Past investment in jdb_savews is paying off right now - keep saving at milestones."
}
else {
  $code = $inp.tool_input.code
  if ($null -eq $code) { exit 0 }

  if ($code -match 'CO_SEARCH\s*\(') {
    $kind = "CO_SEARCH"
    $k = 5
    if ($code -match 'CO_SEARCH\s*\([^,]+,\s*(\d+)') { $k = [int]$Matches[1] }
    $avoided = [math]::Max(0, ($k * 2500) - $respTokens)
    $msg = "[co-processor reward] CO_SEARCH delivered $k ranked spans for ~$respTokens tokens - estimated ~$avoided tokens of bulk file reading avoided. Semantic search beat the read-many-files route this time."
  }
  elseif ($code -match 'CO_DIGEST') {
    $kind = "CO_DIGEST"
    $avoided = [math]::Max(0, 2250 - $respTokens)
    $msg = "[co-processor reward] CO_DIGEST summarized a file for ~$respTokens tokens instead of ~2250 for a full read (~15x measured in past sessions). Mapping files via digest keeps scaling."
  }
  elseif ($code -match 'CO_(CLASSIFY|EXTRACT)') {
    $kind = "CO_" + $Matches[1]
    $msg = "[co-processor reward] $kind ran on the LOCAL model - volume work offloaded, zero cloud-context cost. Division of labour held: local model does volume, you keep the reasoning."
  }
  elseif ($code -match '\bLAB_[A-Z_0-9]+\s*\(') {
    $kind = "LAB_HELPER"
    $msg = "[workshop reward] A persisted LAB_ helper from an earlier session just did the work - no re-implementation needed. Tools built once keep paying rent; extend the library when something is missing."
  }
  elseif ($code -match 'jdb_savews|SAVEWS') {
    $kind = "SAVEWS"
    $msg = "[workshop reward] Workspace saved. Every helper in it is now free in all future sessions."
  }
}

if ($null -eq $msg) { exit 0 }

# Lightweight usage log so the real numbers can be mined for the wins ledger.
try {
  $proj = $env:CLAUDE_PROJECT_DIR
  if ($proj) {
    $line = "{0}`t{1}`t~{2} tok response" -f (Get-Date -Format "yyyy-MM-dd HH:mm"), $kind, $respTokens
    Add-Content -LiteralPath (Join-Path $proj ".claude\co_usage.log") -Value $line
  }
} catch { }

$out = @{ hookSpecificOutput = @{ hookEventName = "PostToolUse"; additionalContext = $msg } }
[Console]::Out.Write(($out | ConvertTo-Json -Depth 4 -Compress))
exit 0
