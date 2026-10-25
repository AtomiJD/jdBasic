# check_memory.ps1 - PreToolUse hook (matcher: Bash)
# A commit that touches the native compiler or its runtime has to pass
# tests/memory_check.sh first: no leak pattern grows the heap, and the
# ownership regressions run green as they are and under JDB_RC_POISON.
# A green run leaves .claude/.memcheck_ok; a later commit attempt skips the
# check while nothing it covers changed since.
$inp = [Console]::In.ReadToEnd() | ConvertFrom-Json
$cmd = $inp.tool_input.command
if ($null -eq $cmd) { exit 0 }
if ($cmd -notmatch 'git\s+commit') { exit 0 }

$proj = $env:CLAUDE_PROJECT_DIR
if (-not $proj) { exit 0 }

. (Join-Path $PSScriptRoot "commit_files.ps1")
$staged = Get-CommitFiles $cmd $proj
$guarded = '^src/(llvm_codegen\.(cpp|h)|jdb_runtime\.cpp|vm_bridge\.cpp|builtin_sigs\.h)$'
$hits = @($staged | Where-Object { $_ -match $guarded } | Select-Object -Unique)
if ($hits.Count -eq 0) { exit 0 }

$marker = Join-Path $proj ".claude\.memcheck_ok"
if (Test-Path $marker) {
  $markerTime = (Get-Item $marker).LastWriteTime
  $newer = @(Get-ChildItem -Path (Join-Path $proj "src") -File |
             Where-Object { $_.Extension -in '.cpp', '.h' -and $_.LastWriteTime -gt $markerTime })
  $newer += @(Get-ChildItem -Path (Join-Path $proj "tests\leak"), (Join-Path $proj "tests\regression") -File -ErrorAction SilentlyContinue |
              Where-Object { $_.LastWriteTime -gt $markerTime })
  if ($newer.Count -eq 0) { exit 0 }
}

$bash = "C:\Program Files\Git\bin\bash.exe"
if (-not (Test-Path $bash)) { $bash = "bash" }
Push-Location $proj
$out = & $bash tests/memory_check.sh 2>&1 | Out-String
$code = $LASTEXITCODE
Pop-Location

if ($code -eq 0 -and $out -match 'MEMCHECK PASSED') {
  Set-Content -LiteralPath $marker -Value (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
  exit 0
}
$tail = ($out -split "`n" | Select-Object -Last 25) -join "`n"
[Console]::Error.WriteLine("Blocked: the commit touches the native compiler or runtime ($($hits -join ', ')) and tests/memory_check.sh did not pass. Fix the leak or the ownership error, then commit again.`n$tail")
exit 2
