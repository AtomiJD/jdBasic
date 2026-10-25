# commit_files.ps1 - dot-sourced by the commit hooks.
# A PreToolUse hook runs before the whole command, so in
# `git add a b && git commit -m ...` nothing is staged yet when it looks.
# Get-CommitFiles answers what the commit will hold: what is staged, what
# the same command adds, and with -a/--all every tracked change. It notes in
# $script:CommitFromIndex which files go in as they stand in the index.

# Changed paths as git status reports them, one per file (-uall), read from
# the NUL-separated form so spaces, quotes and renames come through intact.
function Get-StatusPaths([string]$proj, [string[]]$pathspecs, [bool]$trackedOnly) {
  $gitArgs = @('-C', $proj, 'status', '--porcelain=v1', '-z')
  if ($trackedOnly) { $gitArgs += '-uno' }  else { $gitArgs += '-uall' }
  if ($pathspecs.Count -gt 0) { $gitArgs += '--'; $gitArgs += $pathspecs }
  $raw = (& git @gitArgs 2>$null) -join "`n"
  $entries = $raw -split "`0"
  $out = @()
  for ($i = 0; $i -lt $entries.Count; $i++) {
    $e = $entries[$i]
    if ($e.Length -lt 4) { continue }
    $out += $e.Substring(3)
    if ($e[0] -in 'R', 'C' -or $e[1] -in 'R', 'C') { $i++ }   # the entry after a rename is its source
  }
  return $out
}

function Get-CommitFiles([string]$cmd, [string]$proj) {
  $cached = @(git -C $proj diff --cached --name-only 2>$null)
  $all = $cmd -match 'commit\s+(-\S*a|--all)'
  $files = @($cached)
  if ($all) { $files += @(git -C $proj diff --name-only 2>$null) }
  $added = @()
  foreach ($seg in ($cmd -split '&&|\|\||;')) {
    if ($seg -notmatch '^\s*git\s+add\s+(.+)$') { continue }
    $everything = $false
    $tracked = $false
    $paths = @()
    foreach ($m in [regex]::Matches($Matches[1], '"([^"]*)"|''([^'']*)''|(\S+)')) {
      $t = if ($m.Groups[1].Success) { $m.Groups[1].Value }
           elseif ($m.Groups[2].Success) { $m.Groups[2].Value }
           else { $m.Groups[3].Value }
      if (-not $t) { continue }
      if ($t -in '.', '-A', '--all') { $everything = $true; continue }
      if ($t -in '-u', '--update') { $tracked = $true; continue }
      if ($t.StartsWith('-')) { continue }
      $paths += $t
    }
    if ($everything) { $added += Get-StatusPaths $proj @() $false }
    elseif ($tracked) { $added += Get-StatusPaths $proj $paths $true }
    elseif ($paths.Count -gt 0) { $added += Get-StatusPaths $proj $paths $false }
  }
  $files += $added
  $script:CommitFromIndex = @()
  if (-not $all) {
    $script:CommitFromIndex = @($cached | Where-Object { $_ -and $added -notcontains $_ })
  }
  return @($files | Where-Object { $_ } | ForEach-Object { $_ -replace '\\', '/' } | Select-Object -Unique)
}

# The change a file brings into the commit: from the index when it was staged
# before this command, from the working tree when the command stages it, and
# from nothing for a file git does not know yet.
function Get-CommitDiff([string[]]$files, [string]$proj) {
  $parts = @()
  foreach ($f in $files) {
    git -C $proj ls-files --error-unmatch -- $f 2>$null | Out-Null
    $known = $LASTEXITCODE -eq 0
    if ($script:CommitFromIndex -contains $f) {
      $parts += ((git -C $proj diff -U6 --cached -- $f 2>$null) -join "`n")
    } elseif ($known) {
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
