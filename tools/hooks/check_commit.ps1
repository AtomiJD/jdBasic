# check_commit.ps1 - PreToolUse hook (matcher: Bash)
# Gate 1: no AI trailers / personal names in commit messages.
# Gate 2: code commits need (a) a fresh green gate-test run and (b) a test
#         change alongside the code change. Escape hatch: "[no-test]" in the
#         commit message skips gate 2 (deliberate exceptions only).
# Personal names stay out of the repository: the hook reads them from
# .claude/private_names.txt, one per line, which the checkout ignores.
function Get-PrivateNames {
  $base = $env:CLAUDE_PROJECT_DIR
  if (-not $base) { $base = (Get-Location).Path }
  $f = Join-Path $base ".claude/private_names.txt"
  if (-not (Test-Path $f)) { return @() }
  return @(Get-Content -LiteralPath $f |
           ForEach-Object { $_.Trim() } |
           Where-Object { $_ -and -not $_.StartsWith("#") })
}
$inp = [Console]::In.ReadToEnd() | ConvertFrom-Json
$cmd = $inp.tool_input.command
if ($null -eq $cmd) { exit 0 }
if ($cmd -notmatch 'git\s+commit') { exit 0 }

if ($cmd -match '(?i)co-authored-by') {
  [Console]::Error.WriteLine("Blocked: no Co-Authored-By trailer in commits (project rule). Re-run the commit without it.")
  exit 2
}
foreach ($pname in (Get-PrivateNames)) {
  if ($cmd -match ('(?i)\b' + [regex]::Escape($pname) + '\b')) {
    [Console]::Error.WriteLine("Blocked: no personal names in commit messages (project rule). Rephrase without the name.")
    exit 2
  }
}

$proj = $env:CLAUDE_PROJECT_DIR
if (-not $proj) { exit 0 }

. (Join-Path $PSScriptRoot "commit_files.ps1")
$staged = Get-CommitFiles $cmd $proj

# Gate 0: internal URLs and customer names stay out of the public repository.
# Terms come from .claude/private_terms.txt (ignored by the checkout); every
# added line of the commit is checked, docs included, [no-test] or not.
$termFile = Join-Path $proj ".claude/private_terms.txt"
if ((Test-Path $termFile) -and $staged.Count -gt 0) {
  $terms = @(Get-Content -LiteralPath $termFile |
             ForEach-Object { $_.Trim() } |
             Where-Object { $_ -and -not $_.StartsWith("#") })
  $diff = (& git -C $proj diff --cached -U0 -- $staged) + (& git -C $proj diff -U0 -- $staged)
  $added = @($diff | Where-Object { $_ -match '^\+' -and $_ -notmatch '^\+\+\+' })
  foreach ($term in $terms) {
    foreach ($ln in $added) {
      if ($ln -match ('(?i)\b' + [regex]::Escape($term) + '\b')) {
        [Console]::Error.WriteLine("Blocked: private term '$term' in an added line: $ln - internal URLs and customer names stay out of the public repository.")
        exit 2
      }
    }
    if ($cmd -match ('(?i)\b' + [regex]::Escape($term) + '\b')) {
      [Console]::Error.WriteLine("Blocked: private term '$term' in the commit message.")
      exit 2
    }
  }
}

# Gate 1b: what judges the code is not changed in passing. A commit that
# touches the hooks, the gate runner, a baseline or a workflow, or that
# removes or rewrites lines of a gate suite, needs [gate-change] in its
# message, so the change is a stated decision and shows in the log. Adding
# lines to a gate suite (a new test) needs nothing.
if ($staged.Count -gt 0 -and $cmd -notmatch '\[gate-change\]') {
  $judgePattern = '^(tools/hooks/|tools/gate_run\.jdb$|tools/verify\.sh$|tools/farm_gate\.jdb$|tests/parity\.sh$|tests/parity_baseline[^/]*\.tsv$|tests/(sanitize_gate|memory_check|leak_check)\.sh$|tests/selfhost/coverage_baseline\.tsv$|\.github/workflows/)'
  $touched = @()
  foreach ($f in $staged) {
    $p = $f -replace '\\', '/'
    if ($p -match $judgePattern) { $touched += $p; continue }
    if ($p -match '^tests/gate/') {
      $removed = 0
      foreach ($row in @(& git -C $proj diff HEAD --numstat -- $f 2>$null)) {
        $cols = $row -split "`t"
        if ($cols.Count -ge 2 -and $cols[1] -match '^\d+$') { $removed += [int]$cols[1] }
        elseif ($cols.Count -ge 2) { $removed += 1 }   # binary file
      }
      if ($removed -gt 0) { $touched += "$p ($removed line(s) removed or rewritten)" }
    }
  }
  if ($touched.Count -gt 0) {
    [Console]::Error.WriteLine("Blocked: this commit changes what judges the code: " + ($touched -join ', ') + ". If that is the intent of the task, say so to the human and put [gate-change] in the commit message; do not change an expectation to make a red test green.")
    exit 2
  }
}

if ($cmd -match '\[no-test\]') { exit 0 }
if ($staged.Count -eq 0) { exit 0 }

$codeFiles = @($staged | Where-Object { $_ -match '\.(jdb|cpp|cc|c|h|hpp)$' })
if ($codeFiles.Count -eq 0) { exit 0 }   # docs/config-only commit - no gate

# Gate 2a: a code change must come with a test change (regression-test rule)
$testFiles = @($staged | Where-Object { $_ -match '^tests/' })
if ($testFiles.Count -eq 0) {
  [Console]::Error.WriteLine("Blocked: code changes staged ($($codeFiles.Count) file(s)) but no test under tests/ in this commit. Project rule: every change ships with a regression test. Stage the test, or use [no-test] in the message for a deliberate exception.")
  exit 2
}

# Gate 2b: the gate tests must have run green AFTER the last code edit
$marker = Join-Path $proj ".claude\.tests_ok"
if (-not (Test-Path $marker)) {
  [Console]::Error.WriteLine("Blocked: no green gate-test run recorded. Run the gate tests (./build/jdBasic.exe tests/gate/...) and commit afterwards.")
  exit 2
}
$markerTime = (Get-Item $marker).LastWriteTime
foreach ($f in $codeFiles) {
  $full = Join-Path $proj $f
  if ((Test-Path $full) -and ((Get-Item $full).LastWriteTime -gt $markerTime)) {
    [Console]::Error.WriteLine("Blocked: $f was modified AFTER the last green test run. Re-run the gate tests, then commit.")
    exit 2
  }
}
exit 0
