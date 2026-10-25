# commit_files.ps1 - dot-sourced by the commit hooks.
# A PreToolUse hook runs before the whole command, so in
# `git add a b && git commit -m ...` nothing is staged yet when it looks.
# Get-CommitFiles answers what the commit will hold: what is staged, what
# the same command adds, and with -a/--all every tracked change.
function Get-CommitFiles([string]$cmd, [string]$proj) {
  $files = @(git -C $proj diff --cached --name-only 2>$null)
  if ($cmd -match 'commit\s+(-\S*a|--all)') {
    $files += @(git -C $proj diff --name-only 2>$null)
  }
  foreach ($seg in ($cmd -split '&&|\|\||;')) {
    if ($seg -notmatch '^\s*git\s+add\s+(.+)$') { continue }
    $everything = $false
    $paths = @()
    foreach ($tok in ($Matches[1] -split '\s+')) {
      $t = $tok.Trim('"', "'")
      if (-not $t) { continue }
      if ($t -in '.', '-A', '--all') { $everything = $true; continue }
      if ($t.StartsWith('-')) { continue }
      $paths += $t
    }
    if ($everything) {
      $files += @(git -C $proj status --porcelain 2>$null | ForEach-Object { $_.Substring(3).Trim('"') })
    } elseif ($paths.Count -gt 0) {
      $files += @(git -C $proj status --porcelain -- @paths 2>$null | ForEach-Object { $_.Substring(3).Trim('"') })
    }
  }
  return @($files | Where-Object { $_ } | ForEach-Object { $_ -replace '\\', '/' } | Select-Object -Unique)
}

# The change a file brings into the commit, as a diff against HEAD; a file
# git does not know yet comes as a diff from nothing.
function Get-CommitDiff([string[]]$files, [string]$proj) {
  $parts = @()
  foreach ($f in $files) {
    git -C $proj ls-files --error-unmatch -- $f 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) {
      $parts += ((git -C $proj diff -U6 HEAD -- $f 2>$null) -join "`n")
    } else {
      $full = Join-Path $proj $f
      if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { continue }
      $body = (Get-Content -LiteralPath $full | ForEach-Object { "+$_" }) -join "`n"
      $parts += "diff --git a/$f b/$f`nnew file`n--- /dev/null`n+++ b/$f`n$body"
    }
  }
  return ($parts | Where-Object { $_ }) -join "`n"
}
