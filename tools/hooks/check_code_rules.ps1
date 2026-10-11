# check_code_rules.ps1 - PostToolUse hook (matcher: Edit|Write)
# Scans the just-edited source file. Exit 2 feeds stderr back to Claude as
# feedback so it fixes the file in the next step (PostToolUse cannot undo).
#
# Saved as UTF-8 WITH BOM and kept ASCII-only on purpose: umlauts and the
# em-dash are built from codepoints ([char]0x..), never typed as literals, so
# Windows PowerShell 5.1 cannot mis-decode the patterns under a stray codepage.
# Read stdin as raw UTF-8 regardless of the console codepage, so an em-dash
# (or any non-ASCII) in the tool payload survives intact for the checks below.
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
$reader = New-Object System.IO.StreamReader([Console]::OpenStandardInput(), [System.Text.Encoding]::UTF8)
$inp = $reader.ReadToEnd() | ConvertFrom-Json
$path = $inp.tool_input.file_path
if ($null -eq $path) { exit 0 }

$ext = [System.IO.Path]::GetExtension($path).ToLower()
$codeExts = @('.jdb', '.cpp', '.h', '.hpp', '.c', '.cc')
$textExts = @('.md', '.txt', '.json', '.csv')
# Shell scripts get the em-dash rule too. cmd and PowerShell scripts get more
# than that below, because for them any non-ASCII byte is a real hazard.
$shellExts = @('.bat', '.cmd', '.ps1', '.sh')
$asciiOnlyExts = @('.bat', '.cmd', '.ps1')
$known = $codeExts + $textExts + $shellExts
if ($known -notcontains $ext) { exit 0 }

$problems = @()

# Em-dash guard: em-dashes (U+2014) are codepage poison and must NEVER be
# introduced - in code comments, scripts, .md/.txt/.json, anywhere. Only the
# text this edit ADDS is checked (new_string minus old_string), so the legacy
# em-dashes already living in C++ comments are not re-flagged on every edit.
$emdash = [char]0x2014
$new = $inp.tool_input.new_string
$content = $inp.tool_input.content
if ($null -ne $new) {
  $old = $inp.tool_input.old_string
  $nc = ([regex]::Matches($new, [regex]::Escape($emdash))).Count
  $oc = if ($null -ne $old) { ([regex]::Matches($old, [regex]::Escape($emdash))).Count } else { 0 }
  if ($nc -gt $oc) {
    $problems += "em-dash (U+2014) added in your edit to $path - use a plain hyphen '-' or ': '. Em-dashes cause codepage errors; never use them."
  }
} elseif ($null -ne $content -and $content.Contains($emdash)) {
  $problems += "em-dash (U+2014) in content written to $path - use a plain hyphen '-' or ': '. Em-dashes cause codepage errors; never use them."
}

if (-not (Test-Path $path)) {
  if ($problems.Count -gt 0) { $problems | ForEach-Object { [Console]::Error.WriteLine($_) }; exit 2 }
  exit 0
}

# A .bat/.cmd/.ps1 must be pure ASCII. cmd resolves goto and call by BYTE
# offset, so one multi-byte character above such a jump moves the resume
# position and the interpreter restarts mid-line - the failure then surfaces
# far from its cause. A U+2192 in a REM cost build.bat its entire argument
# loop, silently dropping every flag (fixed 2026-08-07, commit 89144b9).
# Scanned as raw bytes so no console codepage can hide anything; a leading
# UTF-8 BOM is skipped, since these scripts are stored with one.
if ($asciiOnlyExts -contains $ext) {
  $bytes = [System.IO.File]::ReadAllBytes($path)
  $i = 0
  if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) { $i = 3 }
  $lineNo = 1
  while ($i -lt $bytes.Length) {
    if ($bytes[$i] -eq 0x0A) { $lineNo++ }
    elseif ($bytes[$i] -gt 0x7F) {
      $problems += "non-ASCII byte in $path line $lineNo - a .bat/.cmd/.ps1 must stay pure ASCII. Box drawing, arrows and dashes belong in a .md, not in a script cmd parses by byte offset. Use plain '-', '->' and '='."
      break
    }
    $i++
  }
}

# Code-only rules (3 / 5 / 2) operate on the whole file.
if ($codeExts -contains $ext) {
  $text = Get-Content -Raw -LiteralPath $path

  # Rule 3: no personal names / AI attribution in source
  foreach ($pname in (Get-PrivateNames)) {
    if ($text -match ('(?i)\b' + [regex]::Escape($pname) + '\b')) {
      $problems += "personal name '$pname' found in $path - remove it (rule: no personal data in code)"
    }
  }
  if ($text -match '(?i)co-authored-by') { $problems += "AI attribution found in $path - remove it" }

  # Rule 5: comments must be English. Scan ONLY comment text (string literals
  # are skipped, so intentionally-localized UI strings are fine).
  function Get-CommentText($txt, $cext) {
    $out = New-Object System.Collections.Generic.List[string]
    if ($cext -eq '.jdb') {
      foreach ($line in ($txt -split "`n")) {
        $inStr = $false; $ci = -1
        for ($k = 0; $k -lt $line.Length; $k++) {
          $ch = $line[$k]
          if ($ch -eq '"') { $inStr = -not $inStr }
          elseif ($ch -eq "'" -and -not $inStr) { $ci = $k; break }
        }
        if ($ci -ge 0) { $out.Add($line.Substring($ci + 1)) }
      }
    } else {
      $noStr = [regex]::Replace($txt, '"(\\.|[^"\\])*"', '""')
      foreach ($m in [regex]::Matches($noStr, '//[^\r\n]*|/\*[\s\S]*?\*/')) { $out.Add($m.Value) }
    }
    return ($out -join "`n")
  }

  $comments = Get-CommentText $text $ext
  # Unambiguous German function words (no English whole-word collisions) plus
  # umlauts. The umlaut class is assembled from codepoints so the script is
  # pure ASCII and matches real umlauts only - never high-byte fragments of
  # UTF-8 punctuation (em-dash, arrows, box-drawing) under an ANSI read.
  $de = 'und|der|fuer|wird|werden|wenn|eine|einen|oder|auch|noch|mit|von|aus|beim|zum|zur|ist|sind|kein|keine|nach|auf|ueber|sich|nicht|dann|wieder'
  $umlauts = -join @(0xE4, 0xF6, 0xFC, 0xC4, 0xD6, 0xDC, 0xDF | ForEach-Object { [char]$_ })
  $umlautClass = "[$umlauts]"
  if ($comments -match "(?i)\b($de)\b" -or $comments -match $umlautClass) {
    $problems += "German text found in a comment in $path - comments must be English (rule 5). On-screen strings in quotes are exempt."
  }

  # Rule 2: reserved identifiers used as variable names (jdBasic only)
  if ($ext -eq '.jdb') {
    $reserved = 'PI|E|TAU|CLS|STEP|LINE|ON|TICK|VAL|LEN|SUM|MIN|MAX|DAY|MONTH|YEAR|SORT|COUNT|MAP|EXIT|STRING|INTEGER|DOUBLE'
    $hits = [regex]::Matches($text, "(?im)^\s*(DIM|LET)\s+($reserved)\b")
    foreach ($h in $hits) {
      $problems += "reserved identifier '$($h.Groups[2].Value)' used as a variable in $path - rename it (identifiers are case-insensitive)"
    }

    # NONE-comparison trap: `= NONE` is TRUE for ANY value and `<> NONE` is
    # always FALSE, so equality against NONE never does what it looks like. Flag
    # only comparison contexts (<> anywhere; = NONE inside IF/ELSEIF/WHILE/UNTIL/
    # RETURN) so legitimate assignments like `result = NONE` are not touched.
    # The comment portion of each line is stripped before matching.
    foreach ($line in ($text -split "`n")) {
      $code = $line; $inStr = $false
      for ($k = 0; $k -lt $line.Length; $k++) {
        $ch = $line[$k]
        if ($ch -eq '"') { $inStr = -not $inStr }
        elseif ($ch -eq "'" -and -not $inStr) { $code = $line.Substring(0, $k); break }
      }
      $bad = $false
      if ($code -match '(?i)<>\s*NONE\b') { $bad = $true }
      elseif ($code -match '(?i)^\s*(IF|ELSEIF)\b.*=\s*NONE\b.*\bTHEN\b') { $bad = $true }
      elseif ($code -match '(?i)^\s*(WHILE|UNTIL|RETURN)\b.*=\s*NONE\b') { $bad = $true }
      if ($bad) {
        $problems += "comparison to NONE in $path near: $($code.Trim()) - '= NONE' is TRUE for any value and '<> NONE' is always FALSE; test a missing key with TYPEOF(v) = `"NONE`" instead"
        break
      }
    }
  }
}

if ($problems.Count -gt 0) {
  $problems | ForEach-Object { [Console]::Error.WriteLine($_) }
  exit 2
}
exit 0
