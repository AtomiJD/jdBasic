# check_agent_scope.ps1 - PreToolUse hook (matcher: Bash|PowerShell|Read|Grep|Glob|Edit|Write)
# Keeps a subagent inside its job. The main session passes untouched: the
# hook acts only when the call carries an agent_type.
#   every subagent : no git push, no credential files, no change to what
#                    judges the code (hooks, gate runner, baselines, the
#                    suites under tests/gate, the agent settings)
#   test-author    : reads only the documentation and tests, writes only
#                    under tests/ and tmp/, and runs only jdBasic on a file
#                    under tests/ or tmp/ and the program compiled from it
# File tools are checked by path, with . and .. resolved. A shell command is
# text: for test-author it has to consist of the few shapes listed below;
# for the other agents the checks name what must not appear in it, which
# stops a slip and not an agent that sets out to get around them.
# Exit 2 hands the reason back to the agent.
$raw = [Console]::In.ReadToEnd()
try { $inp = $raw | ConvertFrom-Json } catch {
  [Console]::Error.WriteLine("Blocked: check_agent_scope could not read the hook input, so it cannot tell who is calling.")
  exit 2
}
$agent = "$($inp.agent_type)"
if (-not $agent) { exit 0 }

$tool = "$($inp.tool_name)"
$ti = $inp.tool_input

function Deny([string]$why) {
  [Console]::Error.WriteLine("Blocked for agent '$agent': $why Report this to the caller instead of working around it.")
  exit 2
}

# Forward slashes, a drive letter in place of /d/, and . and .. resolved.
function Canon([string]$p) {
  $n = $p -replace '\\', '/'
  if ($n -match '^/([a-zA-Z])/(.*)$') { $n = "$($Matches[1]):/$($Matches[2])" }
  $lead = ""
  if ($n -match '^([a-zA-Z]:)/(.*)$') { $lead = $Matches[1] + '/'; $n = $Matches[2] }
  elseif ($n.StartsWith('/')) { $lead = '/'; $n = $n.TrimStart('/') }
  $kept = New-Object System.Collections.Generic.List[string]
  foreach ($seg in ($n -split '/')) {
    if ($seg -eq '' -or $seg -eq '.') { continue }
    if ($seg -eq '..') { if ($kept.Count -gt 0) { $kept.RemoveAt($kept.Count - 1) }; continue }
    $kept.Add($seg)
  }
  return $lead + ($kept -join '/')
}

$proj = $env:CLAUDE_PROJECT_DIR
if (-not $proj) { $proj = "$($inp.cwd)" }
if (-not $proj) { Deny "the repository root is unknown, so no path can be judged." }
$root = Canon $proj
$here = "$($inp.cwd)"
if (-not $here) { $here = $proj }

# A path as the repository names it. One outside the repository keeps its
# full form, which none of the patterns below accepts.
function Rel([string]$p) {
  if (-not $p) { return "" }
  $n = $p -replace '\\', '/'
  $absolute = $n -match '^([a-zA-Z]:)?/'
  if (-not $absolute) { $n = (Canon $here) + '/' + $n }
  $n = Canon $n
  if ($n.ToLower() -eq $root.ToLower()) { return "." }
  if ($n.ToLower().StartsWith($root.ToLower() + '/')) { return $n.Substring($root.Length + 1) }
  return $n
}

$cmd = ""
if ($tool -in 'Bash', 'PowerShell') { $cmd = "$($ti.command)" }
$target = ""
if ($tool -in 'Read', 'Edit', 'Write') { $target = Rel "$($ti.file_path)" }
if ($tool -in 'Grep', 'Glob') { $target = Rel "$($ti.path)" }

$secret = '(?i)jdtrakr_key|JDTRAKR_API|[/\\]\.ssh([/\\]|$)|\.credentials|id_rsa|id_ed25519|\.netrc|private_terms\.txt|private_names\.txt'
$judge = '^(tools/hooks(/|$)|tools/gate_run\.jdb$|tools/verify\.sh$|tools/farm_gate\.jdb$|tests/gate(/|$)|tests/parity\.sh$|tests/parity_baseline[^/]*\.tsv$|tests/(sanitize_gate|memory_check|leak_check)\.sh$|tests/selfhost/coverage_baseline\.tsv$|\.github/workflows(/|$)|\.claude/settings[^/]*\.json$|\.claude/agents(/|$)|\.claude/session_rules\.md$)'
# The same places as they appear inside a command line.
$judgeInCmd = '(?i)(tools[/\\]hooks|gate_run\.jdb|verify\.sh|farm_gate\.jdb|tests[/\\]gate|parity\.sh|parity_baseline|coverage_baseline|sanitize_gate\.sh|memory_check\.sh|leak_check\.sh|\.github[/\\]workflows|\.claude[/\\](settings|agents|session_rules))'
$writes = '(?i)(>|\b(tee|sed\s+-i|rm|mv|cp|del|truncate|chmod|install|dd)\b|Set-Content|Add-Content|Out-File|Remove-Item|Move-Item|Copy-Item|Rename-Item|New-Item|Clear-Content|TXTWRITER|WriteAllText|git\s+(checkout|restore|apply|stash))'

if ($cmd) {
  if ($cmd -match '(?i)\bgit\b[^|;&]*\bpush\b') { Deny "an agent does not push." }
  if ($cmd -match $secret) { Deny "an agent does not touch credentials." }
  if ($cmd -match '(?i)\bgit\b[^|;&]*\b(commit|reset|rebase|checkout|restore|stash|clean|apply|am|merge)\b') { Deny "an agent does not change the repository's history or working tree through git." }
  # Redirections of a stream to a stream or to the null device write no file.
  $plain = $cmd -replace '\d*>&\d+', '' -replace '\d*>\s*/dev/null', '' -replace '\d*>\s*\$null', ''
  if ($plain -match $judgeInCmd -and $plain -match $writes) { Deny "the command names a file that judges the code and can write." }
}
if ($target -and $target -match $secret) { Deny "an agent does not touch credentials." }
if ($tool -in 'Edit', 'Write' -and $target -match $judge) { Deny "$target is part of what judges the code." }

if ($agent -eq 'test-author') {
  $readable = '^(doc|tests|tmp|\.claude/skills)(/|$)'
  if ($tool -eq 'Read' -and $target -notmatch $readable) { Deny "the test author reads doc/, tests/, tmp/ and the skills, not $target." }
  if ($tool -in 'Grep', 'Glob') {
    if (-not "$($ti.path)") { Deny "name a path under doc/ or tests/ for the search; a search of the whole repository reaches the implementation." }
    if ($target -notmatch $readable) { Deny "the test author searches doc/ and tests/, not $target." }
    foreach ($shape in @("$($ti.pattern)", "$($ti.glob)")) {
      if ($tool -eq 'Grep' -and $shape -eq "$($ti.pattern)") { continue }   # a regex, not a path
      if ($shape -match '\.\.' -or $shape -match '^([a-zA-Z]:)?[/\\]') { Deny "a file pattern stays below the path it is given with." }
    }
  }
  if ($tool -in 'Edit', 'Write' -and $target -notmatch '^(tests|tmp)/') { Deny "the test author writes under tests/ and tmp/, not $target." }
  if ($tool -eq 'PowerShell') { Deny "the test author runs its test through Bash." }
  if ($cmd) {
    if ($cmd -match '\.\.|\$|`|[<>]\(|\r|\n') { Deny "a command of the test author has no .., no variable and no substitution." }
    $file = '(tests|tmp)/[\w./-]+'
    $shapes = @(
      "^(\./)?build/jdBasic(\.exe)?( (-c|--lint))? $file\.jdb( 2>&1)?$",
      "^\./$file( 2>&1)?$",
      "^rm -f $file\.exe$",
      "^(tail|head) -(n )?\d+$",
      "^echo [\w .:=-]*$"
    )
    foreach ($part in ($cmd -split '&&|\|\||;|\|')) {
      $p = ($part.Trim() -replace '\s+', ' ')
      if (-not $p) { continue }
      $known = $false
      foreach ($s in $shapes) { if ($p -match $s) { $known = $true; break } }
      if (-not $known) { Deny "'$p' is not one of the commands of the test author: jdBasic on a file under tests/ or tmp/ (plain, -c or --lint), the program compiled from it, rm -f of that program, tail, head, echo." }
    }
  }
}
exit 0
