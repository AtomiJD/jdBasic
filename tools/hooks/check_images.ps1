# check_images.ps1 - PreToolUse hook (matcher: Bash)
# Images that would give personal data away stay out of the repository.
# Every image a `git commit` would take is checked by image_privacy.jdb (GPS
# position, photographer / author / owner names, camera serials, the names
# and terms in .claude/private_names.txt and private_terms.txt, and formats
# the check cannot read). A flagged image that git does not track yet is
# taken out of the index and written into .gitignore; a flagged image that
# is tracked already is only reported, since .gitignore does nothing there.
# Either way the commit is blocked, so it is run again knowingly. The report
# names what was found, never the value.
#
# jdBasic is $env:JDBASIC_EXE, else build/jdBasic.exe of the repository this
# script lives in, so another repository can wire the same file into its
# own settings with its own $CLAUDE_PROJECT_DIR.

$inp = [Console]::In.ReadToEnd() | ConvertFrom-Json
$cmd = $inp.tool_input.command
if ($null -eq $cmd -or $cmd -notmatch 'git\s+commit') { exit 0 }
$proj = $env:CLAUDE_PROJECT_DIR
if (-not $proj) { exit 0 }

# Names with letters beyond ASCII come from git as they are, and git and
# jdBasic are read as UTF-8.
$env:GIT_CONFIG_COUNT = "1"
$env:GIT_CONFIG_KEY_0 = "core.quotePath"
$env:GIT_CONFIG_VALUE_0 = "false"
[Console]::OutputEncoding = New-Object Text.UTF8Encoding $false
$OutputEncoding = [Console]::OutputEncoding

. (Join-Path $PSScriptRoot "commit_files.ps1")
$files = @(Get-CommitFiles $cmd $proj)
$imageExt = '(?i)\.(jpe?g|png|gif|bmp|ico|svg|heic|heif|webp|tiff?|dng|avif|jxl|cr2|nef|arw)$'
$images = @($files | Where-Object { $_ -match $imageExt })
if ($images.Count -eq 0) { exit 0 }

function Stop-Commit([string]$msg) {
  [Console]::Error.WriteLine($msg)
  exit 2
}

$exe = $env:JDBASIC_EXE
if (-not $exe) { $exe = Join-Path $PSScriptRoot "..\..\build\jdBasic.exe" }
$checker = Join-Path $PSScriptRoot "image_privacy.jdb"
if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $checker)) {
  Stop-Commit "Blocked: the commit takes images, and the image privacy check cannot run (no jdBasic at $exe). Build jdBasic or set JDBASIC_EXE, then commit again."
}

# An image that goes in as it stands in the index is checked in that form:
# the working copy may differ from what the commit takes, or be gone.
$list = [IO.Path]::GetTempFileName()
$staged = Join-Path ([IO.Path]::GetTempPath()) ("jdb_images_" + [Guid]::NewGuid().ToString("N"))
try {
  $lines = @()
  foreach ($img in $images) {
    if ($script:CommitFromIndex -contains $img) {
      New-Item -ItemType Directory -Force -Path $staged | Out-Null
      git -C $proj checkout-index -f "--prefix=$($staged.Replace('\', '/'))/" -- $img 2>$null | Out-Null
      $copy = Join-Path $staged $img
      if (Test-Path -LiteralPath $copy) { $lines += "$img`t$copy"; continue }
    }
    $lines += $img
  }
  [IO.File]::WriteAllLines($list, [string[]]$lines, (New-Object Text.UTF8Encoding $false))
  $out = @(& $exe $checker $proj $list 2>&1 | ForEach-Object { "$_" })
  $code = $LASTEXITCODE
} finally {
  Remove-Item -LiteralPath $list -ErrorAction SilentlyContinue
  if (Test-Path -LiteralPath $staged) { Remove-Item -LiteralPath $staged -Recurse -Force -ErrorAction SilentlyContinue }
}
$bad = @($out | Where-Object { $_ -and $_ -notmatch "^(OK|FLAG)`t" })
if ($code -ne 0 -or $bad.Count -gt 0) {
  Stop-Commit ("Blocked: the image privacy check failed (exit $code), so the images of this commit count as unchecked. Its output: " + (($out | Select-Object -First 5) -join ' | '))
}

$flagged = @($out | Where-Object { $_ -match "^FLAG`t" })
if ($flagged.Count -eq 0) { exit 0 }

$ignored = @()
$tracked = @()
foreach ($line in $flagged) {
  $parts = $line -split "`t"
  $path = $parts[1]
  $why = if ($parts.Count -gt 2) { $parts[2] } else { "" }
  git -C $proj ls-files --error-unmatch -- $path 2>$null | Out-Null
  if ($LASTEXITCODE -eq 0) {
    $git_head = git -C $proj ls-tree --name-only HEAD -- $path 2>$null
    if ($git_head) { $tracked += "  $path ($why)"; continue }
    git -C $proj rm --cached -f -q -- $path 2>$null | Out-Null
  }
  git -C $proj check-ignore -q -- $path 2>$null
  if ($LASTEXITCODE -ne 0) {
    $pattern = "/" + ($path -replace '([\\*?\[\]!# ])', '\$1')
    $gi = Join-Path $proj ".gitignore"
    $nl = ""
    if ((Test-Path -LiteralPath $gi) -and (Get-Item -LiteralPath $gi).Length -gt 0) {
      $bytes = [IO.File]::ReadAllBytes($gi)
      if ($bytes[$bytes.Length - 1] -ne 10) { $nl = "`n" }
    }
    [IO.File]::AppendAllText($gi, $nl + $pattern + "`n", (New-Object Text.UTF8Encoding $false))
  }
  $ignored += "  $path ($why)"
}

$msg = "Blocked: images in this commit carry personal data."
if ($ignored.Count -gt 0) {
  $msg += "`nTaken out of the index and written into .gitignore:`n" + ($ignored -join "`n")
  $msg += "`n" + 'Commit again without them (stage .gitignore with it). To publish one, write a clean copy first, e.g. IMAGE.STRIP(src$, dst$), and remove its line from .gitignore.'
}
if ($tracked.Count -gt 0) {
  $msg += "`nAlready in the repository, so .gitignore does not help:`n" + ($tracked -join "`n")
  $msg += "`nTell the human; replace each with a clean copy (IMAGE.STRIP) or remove it, and mind that the history still holds the old file."
}
Stop-Commit $msg
