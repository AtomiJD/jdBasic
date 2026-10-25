# review_commit.ps1 - PreToolUse hook (matcher: Bash)
# Before a commit of new code, a reviewer agent (claude -p, read-only tools)
# reads the staged diff and looks for memory and security defects: leaks,
# use after free, double free, ownership slips, out-of-bounds access,
# injection, unsafe paths. HIGH findings block the commit; MEDIUM and LOW
# come back as a warning. "[review-ack]" in the message lets a commit through
# whose HIGH findings were checked and judged wrong. A verdict is kept per
# diff, so a second attempt on the same change does not ask again.
if ($env:JDB_REVIEW_ACTIVE) { exit 0 }
$inp = [Console]::In.ReadToEnd() | ConvertFrom-Json
$cmd = $inp.tool_input.command
if ($null -eq $cmd) { exit 0 }
if ($cmd -notmatch 'git\s+commit') { exit 0 }
if ($cmd -match '\[review-ack\]') { exit 0 }

$proj = $env:CLAUDE_PROJECT_DIR
if (-not $proj) { exit 0 }

$all = $cmd -match 'commit\s+(-\S*a|--all)'
$names = @(git -C $proj diff --cached --name-only 2>$null)
if ($all) { $names += @(git -C $proj diff --name-only 2>$null) }
$code = @($names | Where-Object { $_ -and $_ -match '\.(cpp|cc|c|h|hpp|jdb|ps1|sh|py)$' -and $_ -notmatch '^tests/' } |
          Select-Object -Unique)
if ($code.Count -eq 0) { exit 0 }

$diffArgs = @('-C', $proj, 'diff', '-U6')
if (-not $all) { $diffArgs += '--cached' } else { $diffArgs += 'HEAD' }
$diffArgs += '--'
$diffArgs += $code
$diff = (& git @diffArgs 2>$null) -join "`n"
if (-not $diff) { exit 0 }
$limit = 160000
$cut = ""
if ($diff.Length -gt $limit) {
  $diff = $diff.Substring(0, $limit)
  $cut = "`n(The diff was cut at $limit characters; read the rest of the changed files yourself.)"
}

$sha = [System.Security.Cryptography.SHA256]::Create()
$hash = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($diff))) -replace '-', '').Substring(0, 16)
$cacheDir = Join-Path $proj ".claude\.reviews"
New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null
$cache = Join-Path $cacheDir "$hash.txt"

if (Test-Path $cache) {
  $verdict = Get-Content -LiteralPath $cache -Raw
} else {
  $prompt = @"
You review a commit to jdBasic, a BASIC interpreter and native compiler in C++
(src/: the VM, the LLVM code generator llvm_codegen.cpp, the runtime
jdb_runtime.cpp that compiled programs link against, the bridge vm_bridge.cpp)
plus jdBasic library modules (lib/*.jdb) and tools. The working directory is the
repository; use Read, Grep and Glob to check the code around each change before
you report anything.

Look only at what this diff introduces or changes, and only for defects:
- memory: leaks (an allocation nobody frees on some path), use after free,
  double free, freeing something borrowed (a literal, a string another slot
  holds, a bridge buffer freed with the wrong allocator), ownership handed to
  two owners, a refcount registration missing or doubled
- in the code generator: a value registered under an escape scope, a pointer
  stored without a copy where the old owner frees it, a temporary dropped
  while a later use still reads it, a type assumed from a name ($) that the
  value does not have
- bounds: out-of-range index, unchecked length, integer overflow in a size
- security: command or SQL injection, path traversal, unvalidated input from
  the network or files reaching the file system or a shell, secrets in code
- concurrency: shared state touched from a second thread without a guard

Report each finding on one line, nothing else, in this exact form:
SEVERITY | file:line | what goes wrong and on which input
SEVERITY is HIGH (a crash, corruption or exploitable hole on a reachable path),
MEDIUM (a leak that grows, or a defect on an unusual path) or LOW (a smell worth
a look). Report only what you verified in the code; no style remarks, no praise.
If you find nothing, answer exactly: NO FINDINGS
$cut

The diff:
$diff
"@
  $env:JDB_REVIEW_ACTIVE = "1"
  Push-Location $proj
  $verdict = $prompt | & claude -p --model claude-opus-5-5 --setting-sources user `
      --allowedTools "Read,Grep,Glob" --disallowedTools "Bash,Edit,Write,PowerShell" `
      --no-session-persistence 2>&1 | Out-String
  Pop-Location
  Remove-Item Env:JDB_REVIEW_ACTIVE
  if ($LASTEXITCODE -ne 0 -or -not $verdict.Trim()) {
    $msg = "Code review agent could not run (exit $LASTEXITCODE); the commit goes ahead unreviewed."
    @{ systemMessage = $msg } | ConvertTo-Json -Compress
    exit 0
  }
  Set-Content -LiteralPath $cache -Value $verdict
}

$lines = @($verdict -split "`n" | ForEach-Object { $_.Trim() } |
           Where-Object { $_ -match '^(HIGH|MEDIUM|LOW)\s*\|' })
if ($lines.Count -eq 0) { exit 0 }

$high = @($lines | Where-Object { $_ -match '^HIGH' })
$text = $lines -join "`n"
if ($high.Count -gt 0) {
  [Console]::Error.WriteLine("Blocked by the code review agent: $($high.Count) HIGH finding(s) in the staged code. Fix them, or check them and commit with [review-ack] in the message if they are wrong.`n$text")
  exit 2
}
$out = @{
  systemMessage = "Code review agent: $($lines.Count) finding(s), none HIGH:`n$text"
  hookSpecificOutput = @{
    hookEventName = "PreToolUse"
    additionalContext = "The code review agent reported for this commit (not blocking):`n$text`nLook at each before moving on."
  }
}
$out | ConvertTo-Json -Compress -Depth 4
exit 0
